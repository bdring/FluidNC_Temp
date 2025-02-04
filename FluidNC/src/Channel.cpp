// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Report.h"                 // report_gcode_modes
#include "Machine/MachineConfig.h"  // config
#include "RealtimeCmd.h"            // execute_realtime_command
#include "Limits.h"
#include "Logging.h"
#include "Job.h"
#include <string_view>
#include <algorithm>

Channel::Channel(const std::string& name, bool addCR) : _name(name), _linelen(0), _addCR(addCR) {}
Channel::Channel(const char* name, bool addCR) : _name(name), _linelen(0), _addCR(addCR) {}
Channel::Channel(const char* name, int num, bool addCR) : _name(name) {
    _name += std::to_string(num);
    _linelen = 0;
    _addCR   = addCR;
}

void Channel::pause() {
    _paused = true;
}

void Channel::resume() {
    _paused = false;
}

void Channel::flushRx() {
    _linelen   = 0;
    _lastWasCR = false;
    while (_queue.size()) {
        _queue.pop();
    }
}

bool Channel::lineComplete(char* line, char ch) {
    // The objective here is to treat any of CR, LF, or CR-LF
    // as a single line ending.  When we see CR, we immediately
    // complete the line, setting a flag to say that the last
    // character was CR.  When we see LF, if the last character
    // was CR, we ignore the LF because the line has already
    // been completed, otherwise we complete the line.
    if (ch == '\n') {
        if (_lastWasCR) {
            _lastWasCR = false;
            return false;
        }
        // if (_discarding) {
        //     _linelen = 0;
        //     _discarding = false;
        //     return nullptr;
        // }

        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    _lastWasCR = ch == '\r';
    if (_lastWasCR) {
        // Return the complete line
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    if (ch == '\b') {
        // Simple editing for interactive input - backspace erases
        if (_linelen) {
            --_linelen;
        }
        return false;
    }
    if (_linelen < (Channel::maxLine - 1)) {
        _line[_linelen++] = ch;
    } else {
        //  report_status_message(Error::Overflow, this);
        // _linelen = 0;
        // Probably should discard the rest of the line too.
        // _discarding = true;
    }
    return false;
}

uint32_t Channel::setReportInterval(uint32_t ms) {
    uint32_t actual = ms;
    if (actual) {
        actual = std::max(actual, uint32_t(50));
    }
    _reportInterval = actual;
    _nextReportTime = int32_t(xTaskGetTickCount());
    _lastTool       = 255;  // Force GCodeState report
    return actual;
}
static bool motionState() {
    return state_is(State::Cycle) || state_is(State::Homing) || state_is(State::Jog);
}

void Channel::autoReportGCodeState() {
    // When moving, we suppress $G reports in which the only change is the motion mode
    // (e.g. G0/G1/G2/G3 changes) because rapid-fire motion mode changes are fairly common.
    // We would rather not issue a $G report after every GCode line.
    // Similarly, F and S values can change rapidly, especially in laser programs.
    // F and S values are also reported in ? status reports, so they will show up
    // at the chosen periodic rate there.
    if (motionState()) {
        // Force the compare to succeed if the only change is the motion mode
        _lastModal.motion = gc_state.modal.motion;
    }
    if (memcmp(&_lastModal, &gc_state.modal, sizeof(_lastModal)) || _lastTool != gc_state.selected_tool ||
        (!motionState() && (_lastSpindleSpeed != gc_state.spindle_speed || _lastFeedRate != gc_state.feed_rate))) {
        report_gcode_modes(*this);
        memcpy(&_lastModal, &gc_state.modal, sizeof(_lastModal));
        _lastTool         = gc_state.selected_tool;
        _lastSpindleSpeed = gc_state.spindle_speed;
        _lastFeedRate     = gc_state.feed_rate;
    }
}
void Channel::autoReport() {
    if (_reportInterval) {
        auto thisProbeState = config->_probe->get_state();
        report_recompute_pin_string();
        const char* stateName = state_name();
        if (_reportOvr || _reportWco || stateName != _lastStateName || thisProbeState != _lastProbe || _lastPinString != report_pin_string ||
            (motionState() && (int32_t(xTaskGetTickCount()) - _nextReportTime) >= 0) || (_lastJobActive != Job::active())) {
            if (_reportOvr) {
                report_ovr_counter = 0;
                _reportOvr         = false;
            }
            if (_reportWco) {
                report_wco_counter = 0;
                _reportWco         = false;
            }
            _lastStateName = stateName;
            _lastProbe     = thisProbeState;
            _lastPinString = report_pin_string;
            _lastJobActive = Job::active();

            _nextReportTime = xTaskGetTickCount() + _reportInterval;
            report_realtime_status(*this);
        }
        if (_reportNgc != CoordIndex::End) {
            report_ngc_coord(_reportNgc, *this);
            _reportNgc = CoordIndex::End;
        }
        autoReportGCodeState();
    }
}

void Channel::pin_event(uint32_t pinnum, bool active) {
    try {
        auto event_pin       = _events.at(pinnum);
        *_pin_values[pinnum] = active;
        event_pin->trigger(active);
    } catch (std::exception& ex) {}
}

void Channel::handleRealtimeCharacter(uint8_t ch) {
    uint32_t cmd;

    int res = _utf8.decode(ch, cmd);
    if (res == -1) {
        // This can be caused by line noise on an unpowered pendant
        log_debug("UTF8 decoding error");
        _active = false;
        return;
    }
    if (res == 0) {
        return;
    }
    // Otherwise res==1 and we have decoded a sequence so proceed

    _active = true;
    if (cmd == PinACK) {
        // log_debug("ACK");
        _ackwait = false;
        return;
    }
    if (cmd == PinNAK) {
        log_error("Channel device rejected config");
        // log_debug("NAK");
        _ackwait = false;
        return;
    }

    if (cmd >= PinLowFirst && cmd < PinLowLast) {
        pin_event(cmd - PinLowFirst, false);
        return;
    }
    if (cmd >= PinHighFirst && cmd < PinHighLast) {
        pin_event(cmd - PinHighFirst, true);
        return;
    }
    execute_realtime_command(static_cast<Cmd>(cmd), *this);
}

void Channel::push(uint8_t byte) {
    if (is_realtime_command(byte)) {
        handleRealtimeCharacter(byte);
    } else {
        _queue.push(byte);
    }
}

Error Channel::pollLine(char* line) {
    if (_paused) {
        return Error::Ok;
    }
    handle();
    while (1) {
        int ch = -1;
        if (line && _queue.size()) {
            ch = _queue.front();
            _queue.pop();
        } else {
            ch = read();
            if (ch < 0) {
                break;
            }
            _active = true;
            if (realtimeOkay(ch) && is_realtime_command(ch)) {
                handleRealtimeCharacter((uint8_t)ch);
                continue;
            }
            if (!line) {
                _queue.push(ch);
                continue;
            }
            // Fall through if line is non-null and it is not a realtime character
        }

        if (lineComplete(line, ch)) {
            return Error::Ok;
        }
    }
    if (_active) {
        autoReport();
    }
    return Error::NoData;
}

void Channel::setAttr(int index, bool* value, const std::string& attrString, const char* tag) {
    if (value) {
        _pin_values[index] = value;
    }
    out_acked(attrString, tag);
}

void Channel::out(const char* s, const char* tag) {
    sendLine(MsgLevelNone, s);
}

void Channel::out(const std::string& s, const char* tag) {
    sendLine(MsgLevelNone, s);
}

void Channel::out_acked(const std::string& s, const char* tag) {
    out(s, tag);
}

void Channel::ready() {
#if 0
    // At the moment this is unnecessary because initializing
    // an input pin triggers an initial value event
    if (!_pin_values.empty()) {
        out("GET: io.*");
    }
#endif
}

void Channel::registerEvent(uint8_t code, EventPin* obj) {
    _events[code] = obj;
}

void Channel::ack(Error status) {
    if (status == Error::Ok) {
        sendLine(MsgLevelNone, "ok");
        return;
    }
    // With verbose errors, the message text is displayed instead of the number.
    // Grbl 0.9 used to display the text, while Grbl 1.1 switched to the number.
    // Many senders support both formats.
    {
        LogStream msg(*this, "error:");
        msg << static_cast<int>(status);
    }
    if (config->_verboseErrors) {
        log_error_to(*this, errorString(status));
    }
}

void Channel::print_msg(MsgLevel level, const char* msg) {
    if (_message_level >= level) {
        println(msg);
    }
}

// This overload is used primarily with fixed string
// values.  It sends a pointer to the string whose
// memory does not need to be reclaimed later.
// This is the most efficient form, but it only works
// with fixed messages.
void Channel::sendLine(MsgLevel level, const char* line) {
    if (outputTask) {
        LogMessage msg { this, (void*)line, level, false };
        while (!xQueueSend(message_queue, &msg, 10)) {}
    } else {
        print_msg(level, line);
    }
}

// This overload is used primarily with log_*() where
// a std::string is dynamically allocated with "new",
// and then extended to construct the message.  Its
// pointer is sent to the output task, which sends
// the message to the output channel and then "delete"s
// the pointer to reclaim the memory.
// This form has intermediate efficiency, as the string
// is allocated once and freed once.
void Channel::sendLine(MsgLevel level, const std::string* line) {
    if (outputTask) {
        LogMessage msg { this, (void*)line, level, true };
        while (!xQueueSend(message_queue, &msg, 10)) {}
    } else {
        print_msg(level, line->c_str());
        delete line;
    }
}

// This overload is used for many miscellaneous messages
// where the std::string is allocated in a code block and
// then extended with various information.  This send_line()
// copies that string to a newly allocated one and sends that
// via the std::string* version of send_line().  The original
// string is freed by the caller sometime after send_line()
// returns, while the new string is freed by the output task
// after the message is forwared to the output channel.
// This is the least efficient form, requiring two strings
// to be allocated and freed, with an intermediate copy.
// It is used only rarely.
void Channel::sendLine(MsgLevel level, const std::string& line) {
    if (outputTask) {
        sendLine(level, new std::string(line));
    } else {
        print_msg(level, line.c_str());
    }
}

bool Channel::is_visible(const std::string& stem, std::string extension, bool isdir) {
    if (stem.length() && stem[0] == '.') {
        // Exclude hidden files and directories
        return false;
    }
    if (stem == "System Volume Information") {
        // Exclude a common SD card metadata subdirectory
        return false;
    }
    if (isdir) {
        return true;
    }

    // Convert extension to canonical lower case format
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });

    // common gcode extensions
    std::string_view extensions(".g .gc .gco .gcode .nc .ngc .ncc .txt .cnc .tap");
    int              pos = 0;
    while (extensions.length()) {
        auto             next_pos       = extensions.find_first_of(' ', pos);
        std::string_view next_extension = extensions.substr(0, next_pos);
        if (extension == next_extension) {
            return true;
        }
        if (next_pos == extensions.npos) {
            break;
        }
        extensions.remove_prefix(next_pos + 1);
    }
    return false;
}
