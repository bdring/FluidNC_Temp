### Disclaimer: Use at Your Own Risk

The configuration and source code provided are made available "as is" without any guarantees or warranties, express or implied. By using this configuration and source code, you acknowledge that you do so at your own risk. The creator is not responsible for any damages, malfunctions, or issues that may arise from their use. Please review and modify the code according to your own requirements and ensure proper testing and safety measures are in place.

# Example cfg

```yaml
atc_basic:
  safe_z_mpos_mm: -1.000
  probe_seek_rate_mm_per_min: 400.000
  probe_feed_rate_mm_per_min: 100.000
  ets_mpos_mm: -72.500 -7.500 -30.000
  ets_rapid_z_mpos_mm: -10.000
  atc_activate_macro: $LocalFS/Run=/ATC/atc_activate_macro.g
  atc_deactivate_macro: $LocalFS/Run=/ATC/atc_deactivate_macro.g
  toolpickup_macro: $LocalFS/Run=/ATC/toolpickup_macro.g
  toolreturn_macro: $LocalFS/Run=/ATC/toolreturn_macro.g
  tool8_mpos_mm: -844.000 -2.000 -76.000
  tool7_mpos_mm: -844.000 -2.000 -76.000
  tool6_mpos_mm: -784.000 -2.000 -76.000
  tool5_mpos_mm: -724.000 -2.000 -76.000
  tool4_mpos_mm: -664.000 -2.000 -76.000
  tool3_mpos_mm: -604.000 -2.000 -76.000
  tool2_mpos_mm: -544.000 -2.000 -76.000
  tool1_mpos_mm: -484.000 -2.000 -76.000
```

the following macros need to be saved to `$LocalFS/ATC/`
```gcode
(/ATC/atc_activate_macro.g)
G53 G0 Z-1
G53 G0 X-694 Y-150
```

```gcode
(/ATC/atc_deactivate_macro.g)
```

```gcode
(/ATC/toolpickup_macro.g)
G91
G53 G0 X#<_tc_tool_x> Y#<_tc_tool_y>
M62 P6
G4 P0 1.0
G53 G0 Z#<_tc_tool_z>
M63 P6
G4 P0 1.0
G1 Y-15.0 F500
G0 Y-20.0
G53 G0 Z-1.0
```

```gcode
(/ATC/toolreturn_macro.g)
G91
G53 G0 X#<_tc_tool_x> Y[#<_tc_tool_y> -35.0]
G53 G0 Z#<_tc_tool_z>
G0 Y+20.0
G1 Y+15.0 F500
M62 P6
G4 P0 1.0
G53 G0 Z-1.0
M63 P6
```