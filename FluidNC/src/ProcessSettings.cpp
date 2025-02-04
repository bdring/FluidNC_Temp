// Copyright (c) 2020 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include "Machine/MachineConfig.h"
#include "Configuration/RuntimeSetting.h"
#include "Configuration/AfterParse.h"
#include "Configuration/Validator.h"
#include "Configuration/ParseException.h"
#include "Machine/Axes.h"
#include "Regex.h"
#include "WebUI/Authentication.h"
#include "Report.h"
#include "MotionControl.h"
#include "System.h"
#include "Limits.h"               // homingAxes
#include "SettingsDefinitions.h"  // build_info
#include "Protocol.h"             // LINE_BUFFER_SIZE
#include "UartChannel.h"          // Uart0.write()
#include "FileStream.h"           // FileStream()
#include "StartupLog.h"           // startupLog
#include "Driver/gpio_dump.h"     // gpio_dump()
#include "FileCommands.h"         // make_file_commands()

#include "FluidPath.h"
#include "HashFS.h"

#include <cstring>
#include <map>
#include <filesystem>

// WG Readable and writable as guest
// WU Readable and writable as user and admin
// WA Readable as user and admin, writable as admin

static Error switchInchMM(const char* value, AuthenticationLevel auth_level, Channel& out);

static Error fakeMaxSpindleSpeed(const char* value, AuthenticationLevel auth_level, Channel& out);

static Error report_init_message_cmd(const char* value, AuthenticationLevel auth_level, Channel& out);

// If authentication is disabled, auth_level will be LEVEL_ADMIN
static bool auth_failed(Word* w, const char* value, AuthenticationLevel auth_level) {
    permissions_t permissions = w->getPermissions();
    switch (auth_level) {
        case AuthenticationLevel::LEVEL_ADMIN:  // Admin can do anything
            return false;                       // Nothing is an Admin auth fail
        case AuthenticationLevel::LEVEL_GUEST:  // Guest can only access open settings
            return permissions != WG;           // Anything other than RG is Guest auth fail
        case AuthenticationLevel::LEVEL_USER:   // User is complicated...
            if (!value) {                       // User can read anything
                return false;                   // No read is a User auth fail
            }
            return permissions == WA;  // User cannot write WA
        default:
            return true;
    }
}

// Replace GRBL realtime characters with the corresponding URI-style
// escape sequence.
static std::string uriEncodeGrblCharacters(const char* clear) {
    std::string escaped;
    char        c;
    while ((c = *clear++) != '\0') {
        switch (c) {
            case '%':  // The escape character itself
                escaped += "%25";
                break;
            case '!':  // Cmd::FeedHold
                escaped += "%21";
                break;
            case '?':  // Cmd::StatusReport
                escaped += "%3F";
                break;
            case '~':  // Cmd::CycleStart
                escaped += "%7E";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

// Replace URI-style escape sequences like %HH with the character
// corresponding to the hex number HH.  This works with any escaped
// characters, not only those that are special to Grbl
static char* uriDecode(const char* s) {
    const int   dlen = 100;
    static char decoded[dlen + 1];
    char*       out = decoded;
    char        c;
    while ((c = *s++) != '\0') {
        if (c == '%') {
            if (strlen(s) < 2) {
                log_error("Bad % encoding - too short");
                goto done;
            }
            char escstr[3];
            escstr[0] = *s++;
            escstr[1] = *s++;
            escstr[2] = '\0';
            char*   endptr;
            uint8_t esc = strtol(escstr, &endptr, 16);
            if (endptr != &escstr[2]) {
                log_error("Bad % encoding - not hex");
                goto done;
            }
            c = (char)esc;
        }
        if ((out - decoded) == dlen) {
            log_error("String value too long");
            goto done;
        }
        *out++ = c;
    }
done:
    *out = '\0';
    return decoded;
}

static void show_setting(const char* name, const char* value, const char* description, Channel& out) {
    LogStream s(out, "$");
    s << name << "=" << uriEncodeGrblCharacters(value);
    if (description) {
        s << "    ";
        s << description;
    }
}

void settings_restore(uint8_t restore_flag) {
    if (restore_flag & SettingsRestore::Wifi) {
        for (Setting* s : Setting::List) {
            if (!s->getType() == WEBSET) {
                s->setDefault();
            }
        }
    }

    if (restore_flag & SettingsRestore::Defaults) {
        bool restore_startup = restore_flag & SettingsRestore::StartupLines;
        for (Setting* s : Setting::List) {
            if (!s->getDescription()) {
                const char* name = s->getName();
                if (restore_startup) {  // all settings get restored
                    s->setDefault();
                } else if ((strcmp(name, "Line0") != 0) && (strcmp(name, "Line1") != 0)) {  // non startup settings get restored
                    s->setDefault();
                }
            }
        }
        log_info("Settings reset done");
    }
    if (restore_flag & SettingsRestore::Parameters) {
        for (auto idx = CoordIndex::Begin; idx < CoordIndex::End; ++idx) {
            coords[idx]->setDefault();
        }
        coords[gc_state.modal.coord_select]->get(gc_state.coord_system);
        report_wco_counter = 0;  // force next report to include WCO
    }
    log_info("Position offsets reset done");
}

// Get settings values from non volatile storage into memory
static void load_settings() {
    for (Setting* s : Setting::List) {
        s->load();
    }
}

extern void make_settings();
extern void make_user_commands();

void settings_init() {
    make_settings();
    make_file_commands();
}

static Error show_help(const char* value, AuthenticationLevel auth_level, Channel& out) {
    log_string(out, "HLP:$$ $+ $# $S $L $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H $F $E=err ~ ! ? ctrl-x");
    return Error::Ok;
}

static Error report_gcode(const char* value, AuthenticationLevel auth_level, Channel& out) {
    report_gcode_modes(out);
    return Error::Ok;
}

static void show_settings(Channel& out, type_t type) {
    for (Setting* s : Setting::List) {
        if (s->getType() == type && s->getGrblName()) {
            // The following test could be expressed more succinctly with XOR,
            // but is arguably clearer when written out
            show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
        }
    }
    // Print Report/Inches
    switchInchMM(NULL, AuthenticationLevel::LEVEL_ADMIN, out);

    // need this per issue #1036
    fakeMaxSpindleSpeed(NULL, AuthenticationLevel::LEVEL_ADMIN, out);
}

static Error report_normal_settings(const char* value, AuthenticationLevel auth_level, Channel& out) {
    show_settings(out, GRBL);  // GRBL non-axis settings
    return Error::Ok;
}
static Error list_grbl_names(const char* value, AuthenticationLevel auth_level, Channel& out) {
    for (Setting* setting : Setting::List) {
        const char* gn = setting->getGrblName();
        if (gn) {
            log_stream(out, "$" << gn << " => $" << setting->getName());
        }
    }
    return Error::Ok;
}
static Error list_settings(const char* value, AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s : Setting::List) {
        const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
        if (s->getType() != PIN) {
            show_setting(s->getName(), displayValue, NULL, out);
        }
    }
    return Error::Ok;
}
static Error list_changed_settings(const char* value, AuthenticationLevel auth_level, Channel& out) {
    for (Setting* s : Setting::List) {
        const char* value = s->getStringValue();
        if (!auth_failed(s, value, auth_level) && strcmp(value, s->getDefaultString())) {
            if (s->getType() != PIN) {
                show_setting(s->getName(), value, NULL, out);
            }
        }
    }
    log_string(out, "(Passwords not shown)");
    return Error::Ok;
}
static Error list_commands(const char* value, AuthenticationLevel auth_level, Channel& out) {
    for (Command* cp : Command::List) {
        const char* name    = cp->getName();
        const char* oldName = cp->getGrblName();
        LogStream   s(out, "$");
        s << name;
        if (oldName) {
            s << " or $" << oldName;
        }
        const char* description = cp->getDescription();
        if (description) {
            s << " =" << description;
        }
    }
    return Error::Ok;
}
static Error toggle_check_mode(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (state_is(State::ConfigAlarm)) {
        return Error::ConfigurationInvalid;
    }

    // Perform reset when toggling off. Check g-code mode should only work when
    // idle and ready, regardless of alarm locks. This is mainly to keep things
    // simple and consistent.
    if (state_is(State::CheckMode)) {
        report_feedback_message(Message::Disabled);
        sys.abort = true;
    } else {
        if (!state_is(State::Idle)) {
            return Error::IdleError;  // Requires no alarm mode.
        }
        set_state(State::CheckMode);
        report_feedback_message(Message::Enabled);
    }
    return Error::Ok;
}
static Error isStuck() {
    // Block if a control pin is stuck on
    if (config->_control->safety_door_ajar()) {
        send_alarm(ExecAlarm::ControlPin);
        return Error::CheckDoor;
    }
    if (config->_control->stuck()) {
        log_info("Control pins:" << config->_control->report_status());
        send_alarm(ExecAlarm::ControlPin);
        return Error::CheckControlPins;
    }
    return Error::Ok;
}
static Error disable_alarm_lock(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (state_is(State::ConfigAlarm)) {
        return Error::ConfigurationInvalid;
    }
    if (state_is(State::Alarm)) {
        Error err = isStuck();
        if (err != Error::Ok) {
            return err;
        }
        Homing::set_all_axes_homed();
        config->_kinematics->releaseMotors(Axes::motorMask, Axes::hardLimitMask());
        report_feedback_message(Message::AlarmUnlock);
        set_state(State::Idle);
    }
    // Run the after_unlock macro even if no unlock was necessary
    config->_macros->_after_unlock.run(&out);
    return Error::Ok;
}
static Error report_ngc(const char* value, AuthenticationLevel auth_level, Channel& out) {
    report_ngc_parameters(out);
    return Error::Ok;
}
static Error msg_to_uart0(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        Channel* dest = allChannels.find("uart_channel0");
        if (dest) {
            log_msg_to(*dest, value);
        }
    }
    return Error::Ok;
}
static Error msg_to_uart1(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value && config->_uart_channels[1]) {
        log_msg_to(*(config->_uart_channels[1]), value);
    }
    return Error::Ok;
}
static Error cmd_log_msg(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_msg(value + 1);
        } else {
            log_msg_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_error(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_error(value + 1);
        } else {
            log_error_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_warn(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_warn(value + 1);
        } else {
            log_warn_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_info(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_info(value + 1);
        } else {
            log_info_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_debug(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_debug(value + 1);
        } else {
            log_debug_to(out, value);
        }
    }
    return Error::Ok;
}
static Error cmd_log_verbose(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        if (*value == '*') {
            log_verbose(value + 1);
        } else {
            log_verbose_to(out, value);
        }
    }
    return Error::Ok;
}
static Error home(AxisMask axisMask, Channel& out) {
    // see if blocking control switches are active
    if (config->_control->pins_block_unlock()) {
        return Error::CheckControlPins;
    }
    if (axisMask != Machine::Homing::AllCycles) {  // if not AllCycles we need to make sure the cycle is not prohibited
        // if there is a cycle it is the axis from $H<axis>
        auto n_axis = Axes::_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                auto axisConfig     = Axes::_axis[axis];
                auto homing         = axisConfig->_homing;
                auto homing_allowed = homing && homing->_allow_single_axis;
                if (!homing_allowed)
                    return Error::SingleAxisHoming;
            }
        }
    }

    if (state_is(State::ConfigAlarm)) {
        return Error::ConfigurationInvalid;
    }
    if (!Machine::Axes::homingMask) {
        return Error::SettingDisabled;
    }

    if (config->_control->safety_door_ajar()) {
        return Error::CheckDoor;  // Block if safety door is ajar.
    }

    Machine::Homing::run_cycles(axisMask);

    do {
        protocol_execute_realtime();
    } while (state_is(State::Homing));

    if (!Homing::unhomed_axes()) {
        config->_macros->_after_homing.run(&out);
    }

    return Error::Ok;
}
static Error home_all(const char* value, AuthenticationLevel auth_level, Channel& out) {
    AxisMask requestedAxes = Machine::Homing::AllCycles;
    auto     retval        = Error::Ok;

    // value can be a list of cycle numbers like "21", which will run homing cycle 2 then cycle 1,
    // or a list of axis names like "XZ", which will home the X and Z axes simultaneously
    if (value) {
        int        ndigits  = 0;
        const auto lenValue = strlen(value);
        for (int i = 0; i < lenValue; i++) {
            char cycleName = value[i];
            if (isdigit(cycleName)) {
                if (!Machine::Homing::axis_mask_from_cycle(cycleName - '0')) {
                    log_error("No axes for homing cycle " << cycleName);
                    return Error::InvalidValue;
                }
                ++ndigits;
            }
        }
        if (ndigits) {
            if (ndigits != lenValue) {
                log_error("Invalid homing cycle list");
                return Error::InvalidValue;
            } else {
                for (int i = 0; i < lenValue; i++) {
                    char cycleName = value[i];
                    requestedAxes  = Machine::Homing::axis_mask_from_cycle(cycleName - '0');
                    retval         = home(requestedAxes, out);
                    if (retval != Error::Ok) {
                        return retval;
                    }
                }
                return retval;
            }
        }
        if (!Axes::namesToMask(value, requestedAxes)) {
            return Error::InvalidValue;
        }
    }

    return home(requestedAxes, out);
}

static Error home_x(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(X_AXIS), out);
}
static Error home_y(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(Y_AXIS), out);
}
static Error home_z(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(Z_AXIS), out);
}
static Error home_a(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(A_AXIS), out);
}
static Error home_b(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(B_AXIS), out);
}
static Error home_c(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return home(bitnum_to_mask(C_AXIS), out);
}
static std::string limit_set(uint32_t mask) {
    const char* motor0AxisName = "xyzabc";
    std::string s;
    for (int axis = 0; axis < MAX_N_AXIS; axis++) {
        s += bitnum_is_true(mask, Machine::Axes::motor_bit(axis, 0)) ? char(motor0AxisName[axis]) : ' ';
    }
    const char* motor1AxisName = "XYZABC";
    for (int axis = 0; axis < MAX_N_AXIS; axis++) {
        s += bitnum_is_true(mask, Machine::Axes::motor_bit(axis, 1)) ? char(motor1AxisName[axis]) : ' ';
    }
    return s;
}
static Error show_limits(const char* value, AuthenticationLevel auth_level, Channel& out) {
    log_string(out, "Send ! to exit");
    log_stream(out, "Homing Axes : " << limit_set(Machine::Axes::homingMask));
    log_stream(out, "Limit Axes : " << limit_set(Machine::Axes::limitMask));
    log_string(out, "  PosLimitPins NegLimitPins Probe");

    const TickType_t interval = 500;
    TickType_t       limit    = xTaskGetTickCount();
    runLimitLoop              = true;
    do {
        TickType_t thisTime = xTaskGetTickCount();
        if (((long)(thisTime - limit)) > 0) {
            log_stream(out,
                       ": " << limit_set(Machine::Axes::posLimitMask) << " " << limit_set(Machine::Axes::negLimitMask)
                            << (config->_probe->get_state() ? " P" : ""));
            limit = thisTime + interval;
        }
        delay_ms(1);
        protocol_handle_events();
    } while (runLimitLoop);
    log_string(out, "");
    return Error::Ok;
}
static Error go_to_sleep(const char* value, AuthenticationLevel auth_level, Channel& out) {
    protocol_send_event(&sleepEvent);
    return Error::Ok;
}
static Error get_report_build_info(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        report_build_info(build_info->get(), out);
        return Error::Ok;
    }
    return Error::InvalidStatement;
}

const std::map<const char*, uint8_t, cmp_str> restoreCommands = {
    { "$", SettingsRestore::Defaults },   { "settings", SettingsRestore::Defaults },
    { "#", SettingsRestore::Parameters }, { "gcode", SettingsRestore::Parameters },
    { "*", SettingsRestore::All },        { "all", SettingsRestore::All },
    { "@", SettingsRestore::Wifi },       { "wifi", SettingsRestore::Wifi },
};
static Error restore_settings(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        return Error::InvalidStatement;
    }
    auto it = restoreCommands.find(value);
    if (it == restoreCommands.end()) {
        return Error::InvalidStatement;
    }
    settings_restore(it->second);
    return Error::Ok;
}

static Error showState(const char* value, AuthenticationLevel auth_level, Channel& out) {
    const char* name;
    const State state = sys.state;
    auto        it    = StateName.find(state);
    name              = it == StateName.end() ? "<invalid>" : it->second;

    log_stream(out, "State " << int(state) << " (" << name << ")");
    return Error::Ok;
}

static Error doJog(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (state_is(State::ConfigAlarm)) {
        return Error::ConfigurationInvalid;
    }

    // For jogging, you must give gc_execute_line() a line that
    // begins with $J=.  There are several ways we can get here,
    // including  $J, $J=xxx, [J]xxx.  For any form other than
    // $J without =, we reconstruct a $J= line for gc_execute_line().
    if (!value) {
        return Error::InvalidStatement;
    }
    char jogLine[LINE_BUFFER_SIZE];
    strcpy(jogLine, "$J=");
    strcat(jogLine, value);
    return gc_execute_line(jogLine);
}

static Error listAlarms(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (state_is(State::ConfigAlarm)) {
        log_string(out, "Configuration alarm is active. Check the boot messages for 'ERR'.");
    } else if (state_is(State::Alarm)) {
        log_stream(out, "Active alarm: " << int(lastAlarm) << " (" << alarmString(lastAlarm));
    }
    if (value) {
        char*   endptr      = NULL;
        uint8_t alarmNumber = uint8_t(strtol(value, &endptr, 10));
        if (*endptr) {
            log_stream(out, "Malformed alarm number: " << value);
            return Error::InvalidValue;
        }
        const char* alarmName = alarmString(static_cast<ExecAlarm>(alarmNumber));
        if (alarmName) {
            log_stream(out, alarmNumber << ": " << alarmName);
            return Error::Ok;
        } else {
            log_stream(out, "Unknown alarm number: " << alarmNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = AlarmNames.begin(); it != AlarmNames.end(); it++) {
        log_stream(out, static_cast<int>(it->first) << ": " << it->second);
    }
    return Error::Ok;
}

const char* errorString(Error errorNumber) {
    auto it = ErrorNames.find(errorNumber);
    return it == ErrorNames.end() ? NULL : it->second;
}

static Error listErrors(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        char* endptr      = NULL;
        int   errorNumber = strtol(value, &endptr, 10);
        if (*endptr) {
            log_stream(out, "Malformed error number: " << value);
            return Error::InvalidValue;
        }
        const char* errorName = errorString(static_cast<Error>(errorNumber));
        if (errorName) {
            log_stream(out, errorNumber << ": " << errorName);
            return Error::Ok;
        } else {
            log_stream(out, "Unknown error number: " << errorNumber);
            return Error::InvalidValue;
        }
    }

    for (auto it = ErrorNames.begin(); it != ErrorNames.end(); it++) {
        log_stream(out, static_cast<int>(it->first) << ": " << it->second);
    }
    return Error::Ok;
}

static Error motor_control(const char* value, bool disable) {
    if (state_is(State::ConfigAlarm)) {
        return Error::ConfigurationInvalid;
    }

    while (value && isspace(*value)) {
        ++value;
    }
    if (!value || *value == '\0') {
        log_info((disable ? "Dis" : "En") << "abling all motors");
        Axes::set_disable(disable);
        return Error::Ok;
    }

    auto axes = config->_axes;

    if (axes->_sharedStepperDisable.defined()) {
        log_error("Cannot " << (disable ? "dis" : "en") << "able individual axes with a shared disable pin");
        return Error::InvalidStatement;
    }

    for (int i = 0; i < Axes::_numberAxis; i++) {
        char axisName = axes->axisName(i);

        if (strchr(value, axisName) || strchr(value, tolower(axisName))) {
            log_info((disable ? "Dis" : "En") << "abling " << axisName << " motors");
            axes->set_disable(i, disable);
        }
    }
    return Error::Ok;
}
static Error motor_disable(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return motor_control(value, true);
}

static Error motor_enable(const char* value, AuthenticationLevel auth_level, Channel& out) {
    return motor_control(value, false);
}

static Error motors_init(const char* value, AuthenticationLevel auth_level, Channel& out) {
    Axes::config_motors();
    return Error::Ok;
}

static Error macros_run(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (value) {
        size_t macro_num = (*value) - '0';

        auto ok = config->_macros->_macro[macro_num].run(&out);
        return ok ? Error::Ok : Error::NumberRange;
    }
    log_error("$Macros/Run requires a macro number argument");
    return Error::InvalidStatement;
}

static Error dump_config(const char* value, AuthenticationLevel auth_level, Channel& out) {
    Channel* ss;
    if (value) {
        // Use a file on the local file system unless there is an explicit prefix like /sd/
        std::error_code ec;

        try {
            //            ss = new FileStream(std::string(value), "", "w");
            ss = new FileStream(value, "w", "");
        } catch (Error err) { return err; }
    } else {
        ss = &out;
    }
    try {
        Configuration::Generator generator(*ss);
        config->group(generator);
    } catch (std::exception& ex) { log_info("Config dump error: " << ex.what()); }
    if (value) {
        drain_messages();
        delete ss;
    }
    return Error::Ok;
}

static Error report_init_message_cmd(const char* value, AuthenticationLevel auth_level, Channel& out) {
    report_init_message(out);

    return Error::Ok;
}

static Error switchInchMM(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        log_stream(out, "$13=" << (config->_reportInches ? "1" : "0"));
    } else {
        config->_reportInches = ((value[0] == '1') ? true : false);
    }

    return Error::Ok;
}

static Error fakeMaxSpindleSpeed(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        log_stream(out, "$30=" << spindle->maxSpeed());
    }
    return Error::Ok;
}

static Error fakeLaserMode(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        log_stream(out, "$32=" << (spindle->isRateAdjusted() ? "1" : "0"));
    }
    return Error::Ok;
}

static Error showChannelInfo(const char* value, AuthenticationLevel auth_level, Channel& out) {
    allChannels.listChannels(out);
    return Error::Ok;
}

static Error showStartupLog(const char* value, AuthenticationLevel auth_level, Channel& out) {
    StartupLog::dump(out);
    return Error::Ok;
}

static Error showGPIOs(const char* value, AuthenticationLevel auth_level, Channel& out) {
    gpio_dump(out);
    return Error::Ok;
}

#include "UartTypes.h"

static Error uartPassthrough(const char* value, AuthenticationLevel auth_level, Channel& out) {
    Uart* downstream_uart = nullptr;
    int   uart_num;
    if (!value) {
        value = "auto";
    }
    if (strcasecmp(value, "auto") == 0) {
        // Find a UART device with a non-empty passthrough_baud config item
        for (uart_num = 1; uart_num < MAX_N_UARTS; ++uart_num) {
            downstream_uart = config->_uarts[uart_num];
            if (downstream_uart) {
                if (downstream_uart->_passthrough_baud != 0) {
                    break;
                }
            }
        }
        if (uart_num == MAX_N_UARTS) {
            log_error_to(out, "No uart has passthrough_baud configured");
            return Error::InvalidValue;
        }
    } else {
        // Find a UART device that matches the name
        for (uart_num = 1; uart_num < MAX_N_UARTS; ++uart_num) {
            downstream_uart = config->_uarts[uart_num];
            if (downstream_uart) {
                if (downstream_uart->name() == value) {
                    if (downstream_uart->_passthrough_baud == 0) {
                        log_error_to(out, value << " does not have passthrough_baud configured");
                        return Error::InvalidValue;
                    } else {
                        break;
                    }
                }
            }
        }
        if (uart_num == MAX_N_UARTS) {
            log_error_to(out, value << " does not exist");
            return Error::InvalidValue;
        }
    }

    out.pause();  // Stop input polling on the upstream channel

    UartChannel* channel = nullptr;
    for (size_t n = 0; (channel = config->_uart_channels[n]) != nullptr; ++n) {
        if (channel->uart_num() == uart_num) {
            break;
        }
        channel = nullptr;  // Leave channel null if not found
    }

    bool flow;
    int  xon_threshold;
    int  xoff_threshold;

    if (channel) {
        channel->pause();
    }
    downstream_uart->enterPassthrough();

    const int buflen = 256;
    uint8_t   buffer[buflen];
    size_t    upstream_len;
    size_t    downstream_len;

    TickType_t last_ticks = xTaskGetTickCount();
    int        timeout    = 2000;

    while (xTaskGetTickCount() - last_ticks < timeout) {
        size_t len;
        len = out.timedReadBytes((char*)buffer, buflen, 10);
        if (len > 0) {
            last_ticks = xTaskGetTickCount();
            downstream_uart->write(buffer, len);
        }
        len = downstream_uart->timedReadBytes((char*)buffer, buflen, 10);
        if (len > 0) {
            last_ticks = xTaskGetTickCount();
            out.write(buffer, len);
        }
    }

    downstream_uart->exitPassthrough();
    if (channel) {
        channel->resume();
    }
    out.resume();
    return Error::Ok;
}

static Error setReportInterval(const char* value, AuthenticationLevel auth_level, Channel& out) {
    if (!value) {
        uint32_t actual = out.getReportInterval();
        if (actual) {
            log_info_to(out, out.name() << " auto report interval is " << actual << " ms");
        } else {
            log_info_to(out, out.name() << " auto reporting is off");
        }
        return Error::Ok;
    }
    char*    endptr;
    uint32_t intValue = strtol(value, &endptr, 10);

    if (endptr == value || *endptr != '\0') {
        return Error::BadNumberFormat;
    }

    uint32_t actual = out.setReportInterval(intValue);
    if (actual) {
        log_info(out.name() << " auto report interval set to " << actual << " ms");
    } else {
        log_info(out.name() << " auto reporting turned off");
    }

    // Send a full status report immediately so the client has all the data
    out.notifyWco();
    out.notifyOvr();

    return Error::Ok;
}

static Error sendAlarm(const char* value, AuthenticationLevel auth_level, Channel& out) {
    int       intValue = value ? atoi(value) : 0;
    ExecAlarm alarm    = static_cast<ExecAlarm>(intValue);
    log_debug("Sending alarm " << intValue << " " << alarmString(alarm));
    send_alarm(alarm);
    return Error::Ok;
}

static Error showHeap(const char* value, AuthenticationLevel auth_level, Channel& out) {
    log_info("Heap free: " << xPortGetFreeHeapSize() << " min: " << heapLowWater);
    return Error::Ok;
}

// Commands use the same syntax as Settings, but instead of setting or
// displaying a persistent value, a command causes some action to occur.
// That action could be anything, from displaying a run-time parameter
// to performing some system state change.  Each command is responsible
// for decoding its own value string, if it needs one.
void make_user_commands() {
    new UserCommand("GD", "GPIO/Dump", showGPIOs, anyState);

    new UserCommand("CI", "Channel/Info", showChannelInfo, anyState);
    new UserCommand("CD", "Config/Dump", dump_config, anyState);
    new UserCommand("", "Help", show_help, anyState);
    new UserCommand("T", "State", showState, anyState);

    new UserCommand("$", "GrblSettings/List", report_normal_settings, cycleOrHold);
    new UserCommand("L", "GrblNames/List", list_grbl_names, cycleOrHold);
    new UserCommand("Limits", "Limits/Show", show_limits, cycleOrHold);
    new UserCommand("S", "Settings/List", list_settings, cycleOrHold);
    new UserCommand("SC", "Settings/ListChanged", list_changed_settings, cycleOrHold);
    new UserCommand("CMD", "Commands/List", list_commands, cycleOrHold);
    new UserCommand("A", "Alarms/List", listAlarms, anyState);
    new UserCommand("E", "Errors/List", listErrors, anyState);
    new UserCommand("C", "GCode/Check", toggle_check_mode, anyState);
    new UserCommand("X", "Alarm/Disable", disable_alarm_lock, anyState);
    new UserCommand("NVX", "Settings/Erase", Setting::eraseNVS, notIdleOrAlarm, WA);
    new UserCommand("V", "Settings/Stats", Setting::report_nvs_stats, notIdleOrAlarm);
    new UserCommand("#", "GCode/Offsets", report_ngc, notIdleOrAlarm);
    new UserCommand("MD", "Motor/Disable", motor_disable, notIdleOrAlarm);
    new UserCommand("ME", "Motor/Enable", motor_enable, notIdleOrAlarm);
    new UserCommand("MI", "Motors/Init", motors_init, notIdleOrAlarm);

    new UserCommand("RM", "Macros/Run", macros_run, nullptr);

    new UserCommand("H", "Home", home_all, allowConfigStates);
    new UserCommand("HX", "Home/X", home_x, allowConfigStates);
    new UserCommand("HY", "Home/Y", home_y, allowConfigStates);
    new UserCommand("HZ", "Home/Z", home_z, allowConfigStates);
    new UserCommand("HA", "Home/A", home_a, allowConfigStates);
    new UserCommand("HB", "Home/B", home_b, allowConfigStates);
    new UserCommand("HC", "Home/C", home_c, allowConfigStates);

    new UserCommand("MU0", "Msg/Uart0", msg_to_uart0, anyState);
    new UserCommand("MU1", "Msg/Uart1", msg_to_uart1, anyState);
    new UserCommand("LM", "Log/Msg", cmd_log_msg, anyState);
    new UserCommand("LE", "Log/Error", cmd_log_error, anyState);
    new UserCommand("LW", "Log/Warn", cmd_log_warn, anyState);
    new UserCommand("LI", "Log/Info", cmd_log_info, anyState);
    new UserCommand("LD", "Log/Debug", cmd_log_debug, anyState);
    new UserCommand("LV  ", "Log/Verbose", cmd_log_verbose, anyState);

    new UserCommand("SLP", "System/Sleep", go_to_sleep, notIdleOrAlarm);
    new UserCommand("I", "Build/Info", get_report_build_info, notIdleOrAlarm);
    new UserCommand("RST", "Settings/Restore", restore_settings, notIdleOrAlarm, WA);

    new UserCommand("SA", "Alarm/Send", sendAlarm, anyState);
    new UserCommand("Heap", "Heap/Show", showHeap, anyState);
    new UserCommand("SS", "Startup/Show", showStartupLog, anyState);
    new UserCommand("UP", "Uart/Passthrough", uartPassthrough, notIdleOrAlarm);

    new UserCommand("RI", "Report/Interval", setReportInterval, anyState);

    new UserCommand("13", "Report/Inches", switchInchMM, notIdleOrAlarm);
    new UserCommand("30", "FakeMaxSpindleSpeed", fakeMaxSpindleSpeed, notIdleOrAlarm);
    new UserCommand("32", "FakeLaserMode", fakeLaserMode, notIdleOrAlarm);

    new UserCommand("GS", "GRBL/Show", report_init_message_cmd, notIdleOrAlarm);

    new AsyncUserCommand("J", "Jog", doJog, notIdleOrJog);
    new AsyncUserCommand("G", "GCode/Modes", report_gcode, anyState);
};

// normalize_key puts a key string into canonical form -
// without whitespace.
// start points to a null-terminated string.
// Returns the first substring that does not contain whitespace.
// Case is unchanged because comparisons are case-insensitive.
char* normalize_key(char* start) {
    char c;

    // In the usual case, this loop will exit on the very first test,
    // because the first character is likely to be non-white.
    // Null ('\0') is not considered to be a space character.
    while (isspace(c = *start) && c != '\0') {
        ++start;
    }

    // start now points to either a printable character or end of string
    if (c == '\0') {
        return start;
    }

    // Having found the beginning of the printable string,
    // we now scan forward until we find a space character.
    char* end;
    for (end = start; (c = *end) != '\0' && !isspace(c); end++) {}

    // end now points to either a whitespace character or end of string
    // In either case it is okay to place a null there
    *end = '\0';

    return start;
}

// This is the handler for all forms of settings commands,
// $..= and [..], with and without a value.
Error do_command_or_setting(const char* key, const char* value, AuthenticationLevel auth_level, Channel& out) {
    // If value is NULL, it means that there was no value string, i.e.
    // $key without =, or [key] with nothing following.
    // If value is not NULL, but the string is empty, that is the form
    // $key= with nothing following the = .

    // Try to execute a command.  Commands handle values internally;
    // you cannot determine whether to set or display solely based on
    // the presence of a value.
    for (Command* cp : Command::List) {
        if ((strcasecmp(cp->getName(), key) == 0) || (cp->getGrblName() && strcasecmp(cp->getGrblName(), key) == 0)) {
            if (auth_failed(cp, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (cp->synchronous()) {
                protocol_buffer_synchronize();
            }
            return cp->action(value, auth_level, out);
        }
    }

    // First search the yaml settings by name. If found, set a new
    // value if one is given, otherwise display the current value
    try {
        Configuration::RuntimeSetting rts(key, value, out);
        config->group(rts);

        if (rts.isHandled_) {
            if (value) {
                // Validate only if something changed, not for display
                try {
                    Configuration::Validator validator;
                    config->validate();
                    config->group(validator);
                } catch (std::exception& ex) {
                    log_error("Validation error: " << ex.what());
                    return Error::ConfigurationInvalid;
                }

                Configuration::AfterParse afterParseHandler;
                config->afterParse();
                config->group(afterParseHandler);
            }
            return Error::Ok;
        }
    } catch (const Configuration::ParseException& ex) {
        log_error("Configuration parse error at line " << ex.LineNumber() << ": " << ex.What());
        return Error::ConfigurationInvalid;
    } catch (const AssertionFailed& ex) {
        log_error("Configuration change failed: " << ex.what());
        return Error::ConfigurationInvalid;
    }

    // Next search the settings list by text name. If found, set a new
    // value if one is given, otherwise display the current value
    for (Setting* s : Setting::List) {
        if (strcasecmp(s->getName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(uriDecode(value));
            } else {
                show_setting(s->getName(), s->getStringValue(), NULL, out);
                return Error::Ok;
            }
        }
    }

    // Then search the setting list by compatible name.  If found, set a new
    // value if one is given, otherwise display the current value in compatible mode
    for (Setting* s : Setting::List) {
        if (s->getGrblName() && strcasecmp(s->getGrblName(), key) == 0) {
            if (auth_failed(s, value, auth_level)) {
                return Error::AuthenticationFailed;
            }
            if (value) {
                return s->setStringValue(uriDecode(value));
            } else {
                show_setting(s->getGrblName(), s->getCompatibleValue(), NULL, out);
                return Error::Ok;
            }
        }
    }

    // If we did not find an exact match and there is no value,
    // indicating a display operation, we allow partial matches
    // and display every possibility.  This only applies to the
    // text form of the name, not to the nnn and ESPnnn forms.
    Error retval = Error::InvalidStatement;
    if (!value) {
        bool found = false;
        for (Setting* s : Setting::List) {
            auto test = s->getName();
            // The C++ standard regular expression library supports many more
            // regular expression forms than the simple one in Regex.cpp, but
            // consumes a lot of FLASH.  The extra capability is rarely useful
            // especially now that there are only a few NVS settings.
            if (regexMatch(key, test, false)) {
                const char* displayValue = auth_failed(s, value, auth_level) ? "<Authentication required>" : s->getStringValue();
                show_setting(test, displayValue, NULL, out);
                found = true;
            }
        }
        if (found) {
            return Error::Ok;
        }
    }
    return Error::InvalidStatement;
}

Error settings_execute_line(char* line, Channel& out, AuthenticationLevel auth_level) {
    remove_password(line, auth_level);

    char* value;
    if (*line++ == '[') {  // [ESPxxx] form
        value = strchr(line, ']');
        if (!value) {
            // Missing ] is an error in this form
            return Error::InvalidStatement;
        }
        // ']' was found; replace it with null and set value to the rest of the line.
        *value++ = '\0';
        // If the rest of the line is empty, replace value with NULL.
        if (*value == '\0') {
            value = NULL;
        }
    } else {
        // $xxx form
        value = strchr(line, '=');
        if (value) {
            // $xxx=yyy form.
            *value++ = '\0';
        }
    }

    char* key = normalize_key(line);

    // At this point there are three possibilities for value
    // NULL - $xxx without =
    // NULL - [ESPxxx] with nothing after ]
    // empty string - $xxx= with nothing after
    // non-empty string - [ESPxxx]yyy or $xxx=yyy
    return do_command_or_setting(key, value, auth_level, out);
}

Error execute_line(char* line, Channel& channel, AuthenticationLevel auth_level) {
    // Empty or comment line. For syncing purposes.
    if (line[0] == 0) {
        return Error::Ok;
    }
    // Skip leading whitespace
    while (isspace(*line)) {
        ++line;
    }
    // User '$' or WebUI '[ESPxxx]' command
    if (line[0] == '$' || line[0] == '[') {
        if (gc_state.skip_blocks) {
            return Error::Ok;
        }
        return settings_execute_line(line, channel, auth_level);
    }
    // Everything else is gcode. Block if in alarm or jog mode.
    if (state_is(State::Alarm) || state_is(State::ConfigAlarm) || state_is(State::Jog)) {
        return Error::SystemGcLock;
    }
    Error result = gc_execute_line(line);
    if (result != Error::Ok && result != Error::Reset) {
        log_debug_to(channel, "Bad GCode: " << line);
    }
    return result;
}
