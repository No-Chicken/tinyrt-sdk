# S31 示例

| 示例 | 最低 ABI / API Level | Wasm 实际导入 |
|---|---|---|
| [flappy](flappy/README.md) | ABI 1 / Level 1 | `asset_read`, `audio_play`, `clock_interval`, `draw_clear`, `draw_rgb565_scaled`, `draw_skip`, `gfx_begin`, `gfx_caps`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `gfx_tex_upload`, `input_events`, `kv_get`, `kv_set`, `now_ms` |
| [pixel_quest](pixel_quest/README.md) | ABI 1 / Level 1 | `asset_read`, `audio_play`, `clock_interval`, `draw_skip`, `draw_text_box`, `gfx_begin`, `gfx_caps`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `gfx_tex_upload`, `haptic_pulse`, `input_events`, `kv_get`, `kv_set`, `now_ms` |
| [puff](puff/README.md) | ABI 1 / Level 3 | `asset_read`, `audio_play`, `clock_interval`, `draw_skip`, `gfx_begin`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `gfx_tex_upload`, `haptic_pulse`, `input_events`, `kv_get`, `kv_set`, `mic_close`, `mic_features`, `mic_gain`, `mic_open`, `now_ms` |
| [wave_sim](wave_sim/README.md) | ABI 1 / Level 1 | `clock_interval`, `draw_clear`, `draw_rgb565_scaled`, `draw_skip`, `draw_text_box`, `gfx_begin`, `gfx_caps`, `gfx_damage`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `input_events`, `now_ms`, `runtime_backend` |
