// Copyright (c) 2024 -	Bart Dring
// Copyright (c) 2024 -	Nicolas Lang
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "atc_basic.h"
#include "../Machine/MachineConfig.h"
#include <cstdio>
#include <iostream>

namespace ATCs {
    void Basic_ATC::init() {
        log_info("ATC:" << name());
    }

    void Basic_ATC::probe_notification() {}

    bool Basic_ATC::tool_change(uint8_t new_tool, bool pre_select, bool set_tool) {
        std::string message = "ATC:tool_change: from " + std::to_string(_prev_tool) + " to " + std::to_string(new_tool);
        log_info(message);

        protocol_buffer_synchronize();  // wait for all motion to complete
        _macro.erase();                 // clear previous gcode

        if (pre_select) {  // not implemented
            log_debug("ATC: Preselect");
            return true;
        }

        // save current location, so we can return after the tool change.
        _macro.addf("#<start_x >= #<_x>");
        _macro.addf("#<start_y >= #<_y>");
        _macro.addf("#<start_z >= #<_z>");

        //save machine states
        Macro set_state;
        Macro restore_state;
        set_state.addf("M9");  // Disable coolant
        if (gc_state.modal.coolant.Mist) {
            restore_state.addf("M7");
        }
        if (gc_state.modal.coolant.Flood) {
            restore_state.addf("M8");
        }
        set_state.addf("G21");  // become Metric
        if (gc_state.modal.units == Units::Inches) {
            restore_state.addf("G20");
        }
        set_state.addf("G90");  // become absolute
        if (gc_state.modal.distance != Distance::Absolute) {
            restore_state.addf("G91");
        }
        set_state.addf("M5");  // turn off the spindle
        if (gc_state.modal.spindle != SpindleState::Disable) {
            restore_state.addf("M3");
        }

        _macro.addf(set_state._gcode.c_str());

        // set_tool is used to update the current tool and reset the TLO to 0
        // if we dont have_tool_setter_offset then we also do an initial probe
        if (set_tool) {
            log_debug("ATC: Set Tool");
            _prev_tool = new_tool;
            _macro.addf("G4P0 0.0");
            if (!_have_tool_setter_offset) {
                get_ets_offset();
            }
            _macro.addf(restore_state._gcode.c_str());
            move_to_start_position();
            _macro.run(nullptr);
            return true;
        }

        try {
            _macro.addf(_atc_activate_macro._gcode.c_str());
            _macro.addf(set_state._gcode.c_str());
            
            if (_prev_tool > 0) {
                log_debug("ATC: return tool");
                move_to_safe_z();
                set_tool_position(_prev_tool);
                _macro.addf(_toolreturn_macro._gcode.c_str()); // use macro with G91 movements or the _tc_tool_* variables to to return tool, operating the ATC using M62 & M63
                _macro.addf(set_state._gcode.c_str()); // ensure the previous user macro didn't change modes
            }

            if (new_tool > 0) {
                log_debug("ATC: pickup tool");
                //if this is the 1st pickup ever, we also probe the tool_setter_offset
                move_to_safe_z();
                set_tool_position(new_tool);
                _macro.addf(_toolpickup_macro._gcode.c_str()); // use macro with G91 movements or the _tc_tool_* variables to to pickup tool, operating the ATC using M62 & M63
                _macro.addf(set_state._gcode.c_str()); // ensure the previous user macro didn't change modes
                if (!_have_tool_setter_offset) {
                    get_ets_offset();
                    _macro.addf("#<_my_tlo_z >=0.0"); //first tool is reference with 0 offset
                } else {
                    ets_probe();  // probe the new tool
                    // TLO is simply the difference between the tool1 probe and the new tool probe.
                    _macro.addf("#<_my_tlo_z >=[#5063 - #<_ets_tool1_z>]");
                }
                _macro.addf("(print,ATC: New TLO #<_my_tlo_z>)");
                _macro.addf("G43.1Z#<_my_tlo_z>");
            }

            move_to_start_position();

            _macro.addf(_atc_deactivate_macro._gcode.c_str());
            _macro.addf(set_state._gcode.c_str());

            _macro.addf(restore_state._gcode.c_str());
            _macro.run(nullptr);

            _prev_tool = new_tool;

            return true;
        } catch (...) { log_info("Exception caught"); }

        return false;
    }

    void Basic_ATC::move_to_start_position(){
        // return to location before the tool change
        move_to_safe_z();
        _macro.addf("G0 X#<start_x>Y#<start_y>");
        _macro.addf("G0 Z#<start_z>");
    }

    void Basic_ATC::set_tool_position(uint8_t tool_index) {
        tool_index -= 1;
        _macro.addf("#<_tc_tool_x >=%0.3f", _tool_mpos[tool_index][X_AXIS]);
        _macro.addf("#<_tc_tool_y >=%0.3f", _tool_mpos[tool_index][Y_AXIS]);
        _macro.addf("#<_tc_tool_z >=%0.3f", _tool_mpos[tool_index][Z_AXIS]);
    }

    void Basic_ATC::move_to_safe_z() {
        _macro.addf("G53 G0 Z%0.3f", _safe_z);
    }

    void Basic_ATC::move_over_toolsetter() {
        move_to_safe_z();
        _macro.addf("G53 G0 X%0.3fY%0.3f", _ets_mpos[0], _ets_mpos[1]);
    }

    void Basic_ATC::get_ets_offset() {
        ets_probe();
        _macro.addf("#<_ets_tool1_z>=[#5063]");  // save the value of the tool1 ETS Z
        _have_tool_setter_offset = true;
    }

    void Basic_ATC::ets_probe() {
        move_to_safe_z();
        move_over_toolsetter();
        _macro.addf("G53 G0 Z #</ atc_basic / ets_rapid_z_mpos_mm>");  // rapid down

        // do a fast probe if there is a seek that is faster than feed
        if (_probe_seek_rate > _probe_feed_rate) {
            _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_seek_rate);
            _macro.addf("G0 Z[#<_z> + 5]");  // retract before next probe
        }
        // do the feed rate probe
        _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_feed_rate);
    }

    namespace {
        ATCFactory::InstanceBuilder<Basic_ATC> registration("atc_basic");
    }
}
