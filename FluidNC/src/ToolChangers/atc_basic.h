// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Config.h"

#include "src/Configuration/Configurable.h"

#include "src/Channel.h"
#include "src/Module.h"
#include "atc.h"
#include "../Machine/Macros.h"

namespace ATCs {
    const int TOOL_COUNT = 8;

    class Basic_ATC : public ATC {
    public:
        Basic_ATC(const char* name) : ATC(name) {}

        Basic_ATC(const Basic_ATC&)            = delete;
        Basic_ATC(Basic_ATC&&)                 = delete;
        Basic_ATC& operator=(const Basic_ATC&) = delete;
        Basic_ATC& operator=(Basic_ATC&&)      = delete;

        virtual ~Basic_ATC() = default;

    private:
        // config items
        float              _safe_z           = 50.0;
        float              _probe_seek_rate  = 200.0;
        float              _probe_feed_rate  = 80.0;
        std::vector<float> _ets_mpos         = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
        float              _ets_rapid_z_mpos = 0;
        std::vector<float> _tool_mpos[TOOL_COUNT];

        uint8_t _prev_tool               = 0;  // TODO This could be a NV setting
        bool    _have_tool_setter_offset = false;
        float   _tool_setter_offset      = 0.0;  // have we established an offset.
        float   _tool_setter_position[MAX_N_AXIS];

        void  move_to_safe_z();
        void  move_over_toolsetter();
        void  ets_probe();
        void  get_ets_offset();
        void  set_tool_position(uint8_t tool_index);
        void  move_to_start_position();

        Macro _macro;
        Macro _toolreturn_macro;
        Macro _toolpickup_macro;
        Macro _atc_activate_macro;
        Macro _atc_deactivate_macro;

    public:
        void init() override;
        void probe_notification() override;
        bool tool_change(uint8_t value, bool pre_select, bool set_tool) override;

        void validate() override {}

        void group(Configuration::HandlerBase& handler) override {
            handler.item("safe_z_mpos_mm", _safe_z, -100000, 100000);
            handler.item("probe_seek_rate_mm_per_min", _probe_seek_rate, 1, 10000);
            handler.item("probe_feed_rate_mm_per_min", _probe_feed_rate, 1, 10000);
            handler.item("ets_mpos_mm", _ets_mpos);
            handler.item("ets_rapid_z_mpos_mm", _ets_rapid_z_mpos);
            handler.item("toolreturn_macro", _toolreturn_macro);
            handler.item("toolpickup_macro", _toolpickup_macro);
            handler.item("atc_activate_macro", _atc_activate_macro);
            handler.item("atc_deactivate_macro", _atc_deactivate_macro);
            handler.item("tool1_mpos_mm", _tool_mpos[0]);
            handler.item("tool2_mpos_mm", _tool_mpos[1]);
            handler.item("tool3_mpos_mm", _tool_mpos[2]);
            handler.item("tool4_mpos_mm", _tool_mpos[3]);
            handler.item("tool5_mpos_mm", _tool_mpos[4]);
            handler.item("tool6_mpos_mm", _tool_mpos[5]);
            handler.item("tool7_mpos_mm", _tool_mpos[6]);
            handler.item("tool8_mpos_mm", _tool_mpos[7]);
        }
    };
}
