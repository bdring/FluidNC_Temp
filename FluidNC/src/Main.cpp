// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifndef UNIT_TEST

#    include "Main.h"
#    include "Machine/MachineConfig.h"

#    include "Config.h"
#    include "Report.h"
#    include "Settings.h"
#    include "SettingsDefinitions.h"
#    include "Limits.h"
#    include "Protocol.h"
#    include "System.h"
#    include "Uart.h"
#    include "MotionControl.h"
#    include "Platform.h"
#    include "StartupLog.h"

#    include "WebUI/TelnetServer.h"
#    include "WebUI/Serial2Socket.h"
#    include "WebUI/InputBuffer.h"

#    include "WebUI/WifiConfig.h"
#    include "LocalFS.h"

extern void make_user_commands();

// FOR TESTING:

#    include <esp32-hal-gpio.h>
#    include <Wire.h>

extern "C" void __pinMode(pinnum_t pin, uint8_t mode);

uint8_t I2CGetValue(uint8_t address, uint8_t reg) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    auto err = Wire.endTransmission();  // i2c_err_t

    if (Wire.requestFrom((int)address, 1) != 1) {
        Uart0.println("Error reading from i2c bus.");
        return 0;
    }
    uint8_t result = Wire.read();
    return result;
}

void I2CSetValue(uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t data[2];
    data[0] = reg;
    data[1] = uint8_t(value);

    Wire.beginTransmission(address);
    for (size_t i = 0; i < 2; ++i) {
        Wire.write(data[i]);
    }
    auto err = Wire.endTransmission();  // i2c_err_t ??

    if (err) {
        Uart0.println("Error writing to i2c bus; PCA9539 failed. Code: ");
        Uart0.println(int(err));
    }
}

volatile bool fired = false;

void isrHandler() {
    fired = true;
}

// --- Until here.

void setup() {
    try {
        uartInit();       // Setup serial port
        Uart0.println();  // create some white space after ESP32 boot info

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();

        WebUI::WiFiConfig::reset();

        /* TEST STUFF! */

        /*
        // THIS WORKS:
        {
            Uart0.println("Basic test of pin extender.");
            // Wire.begin(sda , scl, frequency);
            Wire.begin(13, 14, 100000);

            Uart0.println("Setup pins:");

            // 1. Setup pins:
            I2CSetValue(0x74, 6, 0xFF);  // All input pins
            I2CSetValue(0x74, 7, 0xFF);  // All input pins

            __pinMode(36, INPUT);
            attachInterrupt(36, isrHandler, CHANGE);

            // 2. Read input register:
            Uart0.println("Main loop:");
            while (true) {
                auto     r1 = I2CGetValue(0x74, 0);
                auto     r2 = I2CGetValue(0x74, 1);
                uint16_t v  = (uint16_t(r1) << 8) | uint16_t(r2);
                Uart0.print("Status: ");
                for (int i = 0; i < 16; ++i) {
                    uint16_t mask = uint16_t(1 << i);
                    Uart0.print((v & mask) ? '1' : '0');
                }

                if (fired) {
                    Uart0.print(" ** ISR");
                    fired = false;
                }

                Uart0.println();

                delay(1000);
            }
        }
        */
        /* END TEST STUFF */

        display_init();

        // Load settings from non-volatile storage
        settings_init();  // requires config

        log_info("FluidNC " << git_info);
        log_info("Compiled with ESP32 SDK:" << ESP.getSdkVersion());

        if (LocalFS.begin(true)) {
            log_info("Local filesystem type is " << LOCALFS_NAME);
        } else {
            log_error("Cannot mount the local filesystem " << LOCALFS_NAME);
        }

        bool configOkay = config->load();

        make_user_commands();

        if (configOkay) {
            log_info("Machine " << config->_name);
            log_info("Board " << config->_board);

            // The initialization order reflects dependencies between the subsystems
            if (config->_i2so) {
                config->_i2so->init();
            }
            if (config->_spi) {
                config->_spi->init();

                if (config->_sdCard != nullptr) {
                    config->_sdCard->init();
                }
            }
            if (config->_i2c) {
                config->_i2c->init();
            }

            // We have to initialize the extenders first, before pins are used
            if (config->_extenders) {
                config->_extenders->init();
            }

            config->_stepping->init();  // Configure stepper interrupt timers

            plan_init();

            config->_userOutputs->init();
            config->_axes->init();
            config->_control->init();
            config->_kinematics->init();

            auto n_axis = config->_axes->_numberAxis;
            for (size_t axis = 0; axis < n_axis; axis++) {
                set_motor_steps(axis, 0);  // Clear machine position.
            }

            machine_init();  // user supplied function for special initialization
        }

        // Initialize system state.
        if (sys.state != State::ConfigAlarm) {
            if (FORCE_INITIALIZATION_ALARM) {
                // Force ALARM state upon a power-cycle or hard reset.
                sys.state = State::Alarm;
            } else {
                sys.state = State::Idle;
            }

            limits_init();

            // Check for power-up and set system alarm if homing is enabled to force homing cycle
            // by setting alarm state. Alarm locks out all g-code commands, including the
            // startup scripts, but allows access to settings and internal commands. Only a homing
            // cycle '$H' or kill alarm locks '$X' will disable the alarm.
            // NOTE: The startup script will run after successful completion of the homing cycle, but
            // not after disabling the alarm locks. Prevents motion startup blocks from crashing into
            // things uncontrollably. Very bad.
            if (config->_start->_mustHome && Machine::Axes::homingMask) {
                // If there is an axis with homing configured, enter Alarm state on startup
                sys.state = State::Alarm;
            }
            for (auto s : config->_spindles) {
                s->init();
            }
            Spindles::Spindle::switchSpindle(0, config->_spindles, spindle);

            config->_coolant->init();
            config->_probe->init();
        }
    } catch (const AssertionFailed& ex) {
        // This means something is terribly broken:
        log_error("Critical error in main_init: " << ex.what());
        sys.state = State::ConfigAlarm;
    }

    if (!WebUI::wifi_config.begin()) {
        WebUI::bt_config.begin();
    }
    allChannels.deregistration(&startupLog);
}

static void reset_variables() {
    // Reset primary systems.
    system_reset();
    protocol_reset();
    gc_init();  // Set g-code parser to default state
    // Spindle should be set either by the configuration
    // or by the post-configuration fixup, but we test
    // it anyway just for safety.  We want to avoid any
    // possibility of crashing at this point.

    plan_reset();  // Clear block buffer and planner variables

    if (sys.state != State::ConfigAlarm) {
        if (spindle) {
            spindle->stop();
            report_ovr_counter = 0;  // Set to report change immediately
        }
        Stepper::reset();  // Clear stepper subsystem variables
    }

    // Sync cleared gcode and planner positions to current system position.
    plan_sync_position();
    gc_sync_position();
    allChannels.flushRx();
    report_init_message(allChannels);
    mc_init();
}

void loop() {
    static int tries = 0;
    try {
        reset_variables();
        // Start the main loop. Processes program inputs and executes them.
        // This can exit on a system abort condition, in which case run_once()
        // is re-executed by an enclosing loop.  It can also exit via a
        // throw that is caught and handled below.
        protocol_main_loop();
    } catch (const AssertionFailed& ex) {
        // If an assertion fails, we display a message and restart.
        // This could result in repeated restarts if the assertion
        // happens before waiting for input, but that is unlikely
        // because the code in reset_variables() and the code
        // that precedes the input loop has few configuration
        // dependencies.  The safest approach would be to set
        // a "reconfiguration" flag and redo the configuration
        // step, but that would require combining main_init()
        // and run_once into a single control flow, and it would
        // require careful teardown of the existing configuration
        // to avoid memory leaks. It is probably worth doing eventually.
        log_error("Critical error in run_once: " << ex.msg);
        log_error("Stacktrace: " << ex.stackTrace);
        sys.state = State::ConfigAlarm;
    }
    // sys.abort is a user-initiated exit via ^x so we don't limit the number of occurrences
    if (!sys.abort && ++tries > 1) {
        log_info("Stalling due to too many failures");
        while (1) {}
    }
}

void WEAK_LINK machine_init() {}

#    if 0
int main() {
    setup();  // setup()
    while (1) {   // loop()
        loop();
    }
    return 0;
}
#    endif

#endif
