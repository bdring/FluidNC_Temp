// Copyright (c) 2024 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDProtocol.h"
#include <string_view>

namespace Spindles {
    class VFDSpindle;

    namespace VFD {
        class GenericProtocol : public VFDProtocol, Configuration::Configurable {
        private:
            bool split(std::string_view& input, std::string_view& token, const char* delims);
            void scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM);
            bool set_data(std::string_view token, std::basic_string_view<uint8_t>& response_view, const char* name, uint32_t& data);

        protected:
            void direction_command(SpindleState mode, ModbusCommand& data) override;
            void set_speed_command(uint32_t dev_speed, ModbusCommand& data) override;

            response_parser initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) override;
            response_parser get_current_speed(ModbusCommand& data) override;
            response_parser get_current_direction(ModbusCommand& data) override { return nullptr; };
            response_parser get_status_ok(ModbusCommand& data) override { return nullptr; }

            std::string _cw_cmd;
            std::string _ccw_cmd;
            std::string _off_cmd;
            std::string _set_rpm_cmd;
            std::string _get_min_rpm_cmd;
            std::string _get_max_rpm_cmd;
            std::string _get_rpm_cmd;

            bool use_delay_settings() const override { return _get_rpm_cmd.empty(); }
            bool safety_polling() const override { return false; }

        private:
            std::string _model;  // VFD Model name
            uint32_t*   _response_data;
            uint32_t    _minRPM = 0xffffffff;
            uint32_t    _maxRPM = 0xffffffff;

            VFDSpindle* spindle;

            bool        parser(const uint8_t* response, VFDSpindle* spindle, GenericProtocol* protocol);
            void        send_vfd_command(const std::string cmd, ModbusCommand& data, uint32_t out);
            std::string _response_format;
            void        setup_speeds(VFDSpindle* vfd);

        public:
            void afterParse() override;
            void group(Configuration::HandlerBase& handler) override {
                handler.item("model", _model);
                handler.item("min_RPM", _minRPM, 0xffffffff);
                handler.item("max_RPM", _maxRPM, 0xffffffff);
                handler.item("cw_cmd", _cw_cmd);
                handler.item("ccw_cmd", _ccw_cmd);
                handler.item("off_cmd", _off_cmd);
                handler.item("set_rpm_cmd", _set_rpm_cmd);
                handler.item("get_min_rpm_cmd", _get_min_rpm_cmd);
                handler.item("get_max_rpm_cmd", _get_max_rpm_cmd);
                handler.item("get_rpm_cmd", _get_rpm_cmd);
            }
        };

    }
}
