// Copyright (c) 2021 -  Marco Wagner
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "GenericProtocol.h"

#include "../VFDSpindle.h"

#include "src/string_util.h"
#include <algorithm>

namespace Spindles {
    namespace VFD {
        bool GenericProtocol::split(std::string_view& input, std::string_view& token, const char* delims) {
            if (input.size() == 0) {
                return false;
            }
            auto pos = input.find_first_of(delims);
            if (pos != std::string_view::npos) {
                token = input.substr(0, pos);
                input = input.substr(pos + 1);
            } else {
                token = input;
                input = "";
            }
            return true;
        }

        void GenericProtocol::scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM) {
            int32_t divider = 1;
            if (scale_str.empty()) {
                return;
            }
            if (scale_str[0] == '%') {
                scale_str.remove_prefix(1);
                n *= 100;
                divider *= maxRPM;
            }
            if (scale_str[0] == '*') {
                std::string_view numerator_str;
                scale_str = scale_str.substr(1);
                split(scale_str, numerator_str, "/");
                uint32_t numerator;
                if (string_util::from_decimal(numerator_str, numerator)) {
                    n *= numerator;
                } else {
                    log_error(spindle->name() << ": bad decimal number " << numerator_str);
                    return;
                }
                if (!scale_str.empty()) {
                    uint32_t denominator;
                    if (string_util::from_decimal(scale_str, denominator)) {
                        divider *= denominator;
                    } else {
                        log_error(spindle->name() << ": bad decimal number " << scale_str);
                        return;
                    }
                }
            } else if (scale_str[0] == '/') {
                std::string_view denominator_str(scale_str.substr(1));
                uint32_t         denominator;
                if (string_util::from_decimal(denominator_str, denominator)) {
                    divider *= denominator;
                } else {
                    log_error(spindle->name() << ": bad decimal number " << scale_str);
                    return;
                }
            }

            n /= divider;
        }

        bool GenericProtocol::set_data(std::string_view token, std::basic_string_view<uint8_t>& response_view, const char* name, uint32_t& data) {
            if (string_util::starts_with_ignore_case(token, name)) {
                uint32_t rval  = (response_view[0] << 8) + (response_view[1] & 0xff);
                uint32_t orval = rval;
                scale(rval, token.substr(strlen(name)), 1);
                data = rval;
                response_view.remove_prefix(2);
                return true;
            }
            return false;
        }
        bool GenericProtocol::parser(const uint8_t* response, VFDSpindle* spindle, GenericProtocol* instance) {
            // This routine does not know the actual length of the response array
            std::basic_string_view<uint8_t> response_view(response, VFD_RS485_MAX_MSG_SIZE);
            response_view.remove_prefix(1);  // Remove the modbus ID which has already been checked

            std::string_view token;
            std::string_view format(_response_format);
            while (split(format, token, " ")) {
                uint8_t val;
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }

                // Sync must be in a temporary because it's volatile!
                uint32_t sync = spindle->_sync_dev_speed;
                if (set_data(token, response_view, "rpm", sync)) {
                    spindle->_sync_dev_speed = sync;
                    continue;
                }
                uint32_t ignore;
                if (set_data(token, response_view, "ignore", ignore)) {
                    continue;
                }
                if (set_data(token, response_view, "minrpm", instance->_minRPM)) {
                    log_debug(spindle->name() << ": got minRPM " << instance->_minRPM);
                    continue;
                }
                if (set_data(token, response_view, "maxrpm", instance->_maxRPM)) {
                    log_debug(spindle->name() << ": got maxRPM " << instance->_maxRPM);
                    continue;
                }
                if (string_util::from_hex(token, val)) {
                    if (val != response_view[0]) {
                        log_debug(spindle->name() << ": response mismatch - expected " << to_hex(val) << " got " << to_hex(response_view[0]));
                        return false;
                    }
                    response_view.remove_prefix(1);
                    continue;
                }
                log_error(spindle->name() << ": bad response token " << token);
                return false;
            }
            return true;
        }
        void GenericProtocol::send_vfd_command(const std::string cmd, ModbusCommand& data, uint32_t out) {
            data.tx_length = 1;
            data.rx_length = 1;
            if (cmd.empty()) {
                return;
            }

            std::string_view out_view;
            std::string_view in_view(cmd);
            split(in_view, out_view, ">");
            _response_format = in_view;  // Remember the response format for the parser

            std::string_view token;
            while (data.tx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && split(out_view, token, " ")) {
                if (token == "") {
                    // Ignore repeated blanks
                    continue;
                }
                if (string_util::starts_with_ignore_case(token, "rpm")) {
                    uint32_t oout = out;
                    scale(out, token.substr(strlen("rpm")), _maxRPM);
                    data.msg[data.tx_length++] = out >> 8;
                    data.msg[data.tx_length++] = out & 0xff;
                } else if (string_util::from_hex(token, data.msg[data.tx_length])) {
                    ++data.tx_length;
                } else {
                    log_error(spindle->name() << ":Bad hex number " << token);
                    return;
                }
            }
            while (data.rx_length < (VFD_RS485_MAX_MSG_SIZE - 3) && split(in_view, token, " ")) {
                if (token == "") {
                    // Ignore repeated spaces
                    continue;
                }
                uint8_t x;
                if (string_util::equal_ignore_case(token, "echo")) {
                    data.rx_length = data.tx_length;
                    break;
                }
                if (string_util::starts_with_ignore_case(token, "rpm") || string_util::starts_with_ignore_case(token, "minrpm") ||
                    string_util::starts_with_ignore_case(token, "maxrpm") || string_util::starts_with_ignore_case(token, "ignore")) {
                    data.rx_length += 2;
                } else if (string_util::from_hex(token, x)) {
                    ++data.rx_length;
                } else {
                    log_error(spindle->name() << ": bad hex number " << token);
                }
            }
        }
        void GenericProtocol::direction_command(SpindleState mode, ModbusCommand& data) {
            switch (mode) {
                case SpindleState::Cw:
                    send_vfd_command(_cw_cmd, data, 0);
                    break;
                case SpindleState::Ccw:
                    send_vfd_command(_ccw_cmd, data, 0);
                    break;
                default:  // SpindleState::Disable
                    send_vfd_command(_off_cmd, data, 0);
                    break;
            }
        }

        void GenericProtocol::set_speed_command(uint32_t speed, ModbusCommand& data) {
            send_vfd_command(_set_rpm_cmd, data, speed);
        }

        VFDProtocol::response_parser GenericProtocol::get_current_speed(ModbusCommand& data) {
            send_vfd_command(_get_rpm_cmd, data, 0);
            return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                auto instance = static_cast<GenericProtocol*>(protocol);
                return instance->parser(response, spindle, instance);
            };
        }

        void GenericProtocol::setup_speeds(VFDSpindle* vfd) {
            vfd->shelfSpeeds(_minRPM, _maxRPM);
            vfd->setupSpeeds(_maxRPM);
            vfd->_slop = 300;
        }
        VFDProtocol::response_parser GenericProtocol::initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) {
            // BUG:
            //
            // If we do:
            // _get_min_rpm_cmd = "03 00 0B 00 01 >  03 02 maxrpm*60";
            // _get_max_rpm_cmd = "03 00 05 00 01 >  03 02 maxrpm*60";
            //
            // then the minrpm will never be assigned, and we end up in an infinite loop.
            //
            // NOT FIXED. I'm not really sure what the best approach is. Perhaps check if after the sequence
            // something changed?

            this->spindle = vfd;
            if (_maxRPM == 0xffffffff && !_get_max_rpm_cmd.empty()) {
                send_vfd_command(_get_max_rpm_cmd, data, 0);
                return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                    auto instance = static_cast<GenericProtocol*>(protocol);
                    return instance->parser(response, spindle, instance);
                };
            }
            if (_minRPM == 0xffffffff && !_get_min_rpm_cmd.empty()) {
                send_vfd_command(_get_min_rpm_cmd, data, 0);
                return [](const uint8_t* response, VFDSpindle* spindle, VFDProtocol* protocol) -> bool {
                    auto instance = static_cast<GenericProtocol*>(protocol);
                    return instance->parser(response, spindle, instance);
                };
            }
            if (vfd->_speeds.size() == 0) {
                setup_speeds(vfd);
            }
            return nullptr;
        }

        struct VFDtype {
            const char* name;
            uint32_t    min_rpm;
            uint32_t    max_rpm;
            const char* cw_cmd;
            const char* ccw_cmd;
            const char* off_cmd;
            const char* set_rpm_cmd;
            const char* get_rpm_cmd;
            const char* get_min_rpm_cmd;
            const char* get_max_rpm_cmd;
        } VFDtypes[] = {
            {
                "YL620",
                0xffffffff,
                0xffffffff,
                "06 20 00 00 12 > echo",
                "06 20 00 00 22 > echo",
                "06 20 00 00 01 > echo",
                "06 20 01 rpm*10/60 > echo",
                "03 20 0b 00 01 > 03 02 rpm*6",
                "",
                "03 03 08 00 02 > 03 04 minrpm*60/10 maxrpm*6",
            },
            {
                "Huanyang",
                0xffffffff,
                0xffffffff,
                "03 01 01 > echo",
                "03 01 11 > echo",
                "03 01 08 > echo",
                "05 02 rpm*100/60 > echo",
                "04 03 01 00 00 > 04 03 01 rpm*60/100",
                "01 03 0b 00 00 > 01 03 0B minRPM*60/100",
                "01 03 05 00 00 > 01 03 05 maxRPM*60/100",
            },
            {
                "H2A",
                6000,
                0xffffffff,
                "06 20 00 00 01 > echo",
                "06 20 00 00 02 > echo",
                "06 20 00 00 06 > echo",
                "06 10 00 rpm%*100 > echo",
                "03 70 0C 00 02 > 03 00 04 rpm 00 00",
                "",
                "03 B0 05 00 02 >  03 00 04 maxrpm 03 F6",
            },
            {
                "H100",
                0xffffffff,
                0xffffffff,
                "05 00 49 ff 00 > echo",
                "05 00 4A ff 00 > echo",
                "05 00 4B ff 00 > echo",
                "06 02 01 rpm%*4 > echo",
                "04 00 00 00 02 > 04 04 rpm%*4 ignore",
                "03 00 0B 00 01 > 03 02 minrpm*60",
                "03 00 05 00 01 > 03 02 maxrpm*60",
            },
            {
                "NowForever",
                0xffffffff,
                0xffffffff,
                "10 09 00 00 01 02 00 01 > echo",
                "10 09 00 00 01 02 00 03 > echo",
                "10 09 00 00 01 02 00 00 > echo",
                "10 09 01 00 01 02 rpm%*4 > echo",
                "03 05 02 00 01 > 03 02 rpm%*4",
                "",
                "03 00 07 00 02 >  03 04 maxrpm*6 00 00",
            },
            {
                "SiemensV20",
                0,
                24000,
                "06 00 63 0C 7F > echo",
                "06 00 63 04 7F > echo",
                "06 00 63 0C 7E > echo",
                "06 00 64 rpm%*16384/100 > echo",
                "03 00 6E 00 01 > 03 ignore rpm%*16384/100",
                "",
                "",
            },
        };
        void GenericProtocol::afterParse() {
            for (auto const& vfd : VFDtypes) {
                if (string_util::equal_ignore_case(_model, vfd.name)) {
                    log_debug("Using predefined ModbusVFD " << vfd.name);
                    if (_cw_cmd.empty()) {
                        _cw_cmd = vfd.cw_cmd;
                    }
                    if (_ccw_cmd.empty()) {
                        _ccw_cmd = vfd.ccw_cmd;
                    }
                    if (_off_cmd.empty()) {
                        _off_cmd = vfd.off_cmd;
                    }
                    if (_set_rpm_cmd.empty()) {
                        _set_rpm_cmd = vfd.set_rpm_cmd;
                    }
                    if (_get_rpm_cmd.empty()) {
                        _get_rpm_cmd = vfd.get_rpm_cmd;
                    }
                    if (_get_max_rpm_cmd.empty()) {
                        _get_max_rpm_cmd = vfd.get_max_rpm_cmd;
                    }
                    if (_get_min_rpm_cmd.empty()) {
                        _get_min_rpm_cmd = vfd.get_min_rpm_cmd;
                    }
                    return;
                }
            }
        }

        // Configuration registration
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, GenericProtocol> registration("ModbusVFD");
        }
    }
}
