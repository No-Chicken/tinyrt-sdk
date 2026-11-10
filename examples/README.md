# S31 示例

| 示例 | 最低 ABI / API Level | Wasm 实际导入 |
|---|---|---|
| [flappy](flappy/README.md) | ABI 1 / Level 1 | `asset_read`, `audio_play`, `clock_interval`, `draw_clear`, `draw_rgb565_scaled`, `draw_skip`, `gfx_begin`, `gfx_caps`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `gfx_tex_upload`, `input_events`, `kv_get`, `kv_set`, `now_ms` |
| [pixel_quest](pixel_quest/README.md) | ABI 1 / Level 1 | `asset_read`, `audio_play`, `clock_interval`, `draw_skip`, `draw_text_box`, `gfx_begin`, `gfx_caps`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `gfx_tex_upload`, `haptic_pulse`, `input_events`, `kv_get`, `kv_set`, `now_ms` |
| [wave_sim](wave_sim/README.md) | ABI 1 / Level 1 | `clock_interval`, `draw_clear`, `draw_rgb565_scaled`, `draw_skip`, `draw_text_box`, `gfx_begin`, `gfx_caps`, `gfx_damage`, `gfx_end`, `gfx_pal_upload`, `gfx_submit`, `input_events`, `now_ms`, `runtime_backend` |
| [framebuffer](framebuffer/README.md) | ABI 1 / Level 1 | `fb_config`, `fb_present` |
| [blob_palette](blob_palette/README.md) | ABI 1 / Level 4 | `blob_read`, `blob_write`, `draw_clear`, `input_events` |
| [gyro_ball](gyro_ball/README.md) | ABI 1 / Level 3 | `draw_clear`, `draw_round_rect`, `imu_config`, `imu_read` |
| [device_status](device_status/README.md) | ABI 1 / Level 3 | `battery_info`, `display_brightness`, `draw_clear`, `draw_text_box`, `input_events`, `screen_keep_on`, `time_local`, `time_subscribe` |
| [rhythm_pad](rhythm_pad/README.md) | ABI 1 / Level 4 | `audio_queue_free`, `audio_tone`, `audio_write`, `draw_clear`, `draw_text`, `haptic_pattern`, `input_events` |
