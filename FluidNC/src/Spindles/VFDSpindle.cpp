// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a VFD based spindles via RS485 Modbus. The details of the 
    VFD protocol heavily depend on the VFD in question here. We have some 
    implementations, but if yours is not here, the place to start is the 
    manual. This VFD class implements the modbus functionality.

                         WARNING!!!!
    VFDs are very dangerous. They have high voltages and are very powerful
    Remove power before changing bits.

    TODO:
      - We can report spindle_state and rpm better with VFD's that support 
        either mode, register RPM or actual RPM.
      - Destructor should break down the task.

*/
#include "VFDSpindle.h"
#include "VFD/VFDProtocol.h"

#include "../Machine/MachineConfig.h"
#include "../Protocol.h"  // rtAlarm
#include "../Report.h"    // hex message
#include "../Configuration/HandlerType.h"
#include "../Platform.h"

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

namespace Spindles {
    // number of commands that can be queued up.
    const int VFD_RS485_QUEUE_SIZE = 10;

    // ================== Class methods ==================================

    void VFDSpindle::init() {
        _sync_dev_speed = 0;
        _syncing        = false;

        // The following lets you have either a uart: section below the VFD section,
        // or "uart_num: N" referring to an externally defined uartN: section - but not both
        if (_uart) {
            _uart->begin();
        } else {
            _uart = config->_uarts[_uart_num];
            if (!_uart) {
                log_error("VFDSpindle: Missing uart" << _uart_num << " section");
                return;
            }
        }

        if (_uart->setHalfDuplex()) {
            log_info("VFD: RS485 UART set half duplex failed");
            return;
        }

        // These VFDs are always reversable, but most can be set via the operator panel
        // to only allow one direction.  In principle we could check that setting and
        // automatically set is_reversable.
        is_reversable = true;

        _current_state = SpindleState::Disable;

        // Initialization is complete, so now it's okay to run the queue task:
        if (!VFD::VFDProtocol::vfd_cmd_queue) {  // init can happen many times, we only want to start one task
            VFD::VFDProtocol::vfd_cmd_queue = xQueueCreate(VFD_RS485_QUEUE_SIZE, sizeof(VFD::VFDProtocol::VFDaction));
            xTaskCreatePinnedToCore(VFD::VFDProtocol::vfd_cmd_task,  // task
                                    "vfd_cmdTaskHandle",             // name for task
                                    2048,                            // size of task stack
                                    this,                            // parameters
                                    1,                               // priority
                                    &VFD::VFDProtocol::vfd_cmdTaskHandle,
                                    SUPPORT_TASK_CORE  // core
            );
        }

        init_atc();
        config_message();

        set_mode(SpindleState::Disable, true);
    }

    void VFDSpindle::config_message() {
        std::string usage(" Spindle");
        usage += atc_info();
        _uart->config_message(name(), usage.c_str());
    }

    void VFDSpindle::set_mode(SpindleState mode, bool critical) {
        _last_override_value = sys.spindle_speed_ovr;  // sync these on mode changes
        if (VFD::VFDProtocol::vfd_cmd_queue) {
            VFD::VFDProtocol::VFDaction action;
            action.action   = VFD::VFDProtocol::actionSetMode;
            action.arg      = uint32_t(mode);
            action.critical = critical;
            if (xQueueSend(VFD::VFDProtocol::vfd_cmd_queue, &action, 0) != pdTRUE) {
                log_info("VFD Queue Full");
            }
        }
    }

    void VFDSpindle::setState(SpindleState state, SpindleSpeed speed) {
        log_debug(name() << ": setState:" << uint8_t(state) << " SpindleSpeed:" << speed);
        if (sys.abort) {
            return;  // Block during abort.
        }

        bool critical = (state_is(State::Cycle) || state != SpindleState::Disable);

        uint32_t dev_speed = mapSpeed(speed);

        if (_current_state != state) {
            // Changing state
            set_mode(state, critical);  // critical if we are in a job

            setSpeed(dev_speed);
        } else {
            // Not changing state
            if (_current_dev_speed != dev_speed) {
                // Changing speed
                setSpeed(dev_speed);
            }
        }
        if (detail_->use_delay_settings()) {
            spindleDelay(state, speed);
        } else {
            // _sync_dev_speed is set by a callback that handles
            // responses from periodic get_current_speed() requests.
            // It changes as the actual speed ramps toward the target.

            _syncing = true;  // poll for speed

            auto minSpeedAllowed = dev_speed > _slop ? (dev_speed - _slop) : 0;
            auto maxSpeedAllowed = dev_speed + _slop;

            int       unchanged = 0;
            const int limit     = 20;  // 20 * 0.5s = 10 sec
            auto      last      = _sync_dev_speed;

            while ((_last_override_value == sys.spindle_speed_ovr) &&  // skip if the override changes
                   ((_sync_dev_speed < minSpeedAllowed || _sync_dev_speed > maxSpeedAllowed) && unchanged < limit)) {
                delay_ms(_poll_ms);
                if (_debug > 1) {
                    log_debug("Syncing speed. Requested: " << int(dev_speed) << " current:" << int(_sync_dev_speed));
                }
                // if (!mc_dwell(500)) {
                //     // Something happened while we were dwelling, like a safety door.
                //     unchanged = limit;
                //     last      = _sync_dev_speed;
                //     break;
                // }

                // unchanged counts the number of consecutive times that we see the same speed
                unchanged = (_sync_dev_speed == last) ? unchanged + 1 : 0;
                last      = _sync_dev_speed;
            }
            _last_override_value = sys.spindle_speed_ovr;

            if (_debug > 1) {
                log_debug("Synced speed. Requested:" << int(dev_speed) << " current:" << int(_sync_dev_speed));
            }

            if (unchanged == limit) {
                mc_critical(ExecAlarm::SpindleControl);
                log_error(name() << ": spindle did not reach device units " << dev_speed << ". Reported value is " << _sync_dev_speed);
            }

            _syncing = false;
            // spindleDelay() sets these when it is used
            _current_state = state;
            _current_speed = speed;
        }
        //        }
    }

    void IRAM_ATTR VFDSpindle::setSpeedfromISR(uint32_t dev_speed) {
        if (_current_dev_speed == dev_speed || _last_speed == dev_speed) {
            return;
        }

        _last_speed = dev_speed;

        if (VFD::VFDProtocol::vfd_cmd_queue) {
            VFD::VFDProtocol::VFDaction action;
            action.action   = VFD::VFDProtocol::actionSetSpeed;
            action.arg      = dev_speed;
            action.critical = (dev_speed == 0);
            // Ignore errors because reporting is not safe from an ISR.
            // Perhaps set a flag instead?
            xQueueSendFromISR(VFD::VFDProtocol::vfd_cmd_queue, &action, 0);
        }
    }

    void VFDSpindle::setSpeed(uint32_t dev_speed) {
        if (VFD::VFDProtocol::vfd_cmd_queue) {
            VFD::VFDProtocol::VFDaction action;
            action.action   = VFD::VFDProtocol::actionSetSpeed;
            action.arg      = dev_speed;
            action.critical = dev_speed == 0;
            if (xQueueSend(VFD::VFDProtocol::vfd_cmd_queue, &action, 0) != pdTRUE) {
                log_info("VFD Queue Full");
            }
        }
    }

    void VFDSpindle::validate() {
        Spindle::validate();
        Assert(_uart != nullptr || _uart_num != -1, "VFD: missing UART configuration");
    }

    void VFDSpindle::afterParse() {
        detail_->afterParse();
    }

    void VFDSpindle::group(Configuration::HandlerBase& handler) {
        if (handler.handlerType() == Configuration::HandlerType::Generator) {
            if (_uart_num == -1) {
                handler.section("uart", _uart, 1);
            } else {
                handler.item("uart_num", _uart_num);
            }
        } else {
            handler.section("uart", _uart, 1);
            handler.item("uart_num", _uart_num);
        }
        handler.item("modbus_id", _modbus_id, 0, 247);  // per https://modbus.org/docs/PI_MBUS_300.pdf
        handler.item("debug", _debug, 0, 5);
        handler.item("poll_ms", _poll_ms, 250, 20000);
        handler.item("retries", _retries);

        Spindle::group(handler);
        detail_->group(handler);
    }
}
