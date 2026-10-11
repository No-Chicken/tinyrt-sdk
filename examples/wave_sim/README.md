# Wave v9

600 个独立立体小球在完整 466×466 圆屏内运动。容器前壁与屏幕对齐，投影半径 225 px，边缘保留约 8 px；后方侧壁随透视展开，后层也能到达圆屏边缘。物理包含 X/Y/Z 位置、重力与碰撞，深度范围 64 px。画面使用球面分层光照、近大远小、远暗近亮和远到近的遮挡。

触摸长按 500 ms 在 OCEAN、NEON、TOXIC、LAVA、MONO 五套主题间切换；轻点或短按 KEY1 扰动球体。KEY1 长按由宿主退出，触摸取消清除按住状态。板级 IMU 安装方向由固件统一，应用采用 X=ax、Y=-ay。

正式清单使用 `balls_main.c`、`wave_balls.c` 和 `WAVE_BALL_COUNT=600`。`app.json.version` 是唯一发布版本来源。应用使用 ABI 1 的 `SPRITE_BATCH`，无新增宿主接口；设备必须安装与芯片匹配的 AOT 包。

物理使用带稳定编号的粒子，XYZ 24 px 邻域核、双密度压力松弛、邻居粘性和修正位移回算速度。每次 1/30 s 更新包含两个 1/60 s 子步。密度计算缓存实际邻居；缓存不足时完整扫描，不丢弃相互作用。AOT 每个回调最多推进 40 ms，33/33/34 ms 出帧节奏优先完成物理。显示读取两次完整快照并插值，不读取正在排序的工作数组。解释器采用 1 ms 时钟并额外分片，桌面速度不代表设备性能。

S31 三组合成姿态各测 35 秒，实际送屏约 30 FPS，完成物理约 29.8–29.9 次/秒。此前 900 球配置虽有约 45 FPS 显示，实际物理只有约 6 次/秒，不能作为流畅依据。验收分别统计实际送屏与物理更新。

球面图集为 8 级深度 × 4 档速度 × 7 级光照，INDEX8 共 7200 字节。前后覆盖区域的并集擦除旧像素，初帧完整刷新。宿主管理显示任务、圆屏裁剪、DMA 与退出清理。

本轮 S31 AOT 的合成姿态实测与限制见 `VALIDATION-v9.md`；S3 仅通过编译和原生回归，尚未在本轮实测。真实倾斜手感与撕裂仍需目视验收。

从 SDK 根目录执行：

```powershell
python tests/wave_sim/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/wave_sim --cc path/to/zig.exe
python tools/tinyrt.py run examples/wave_sim --runner path/to/tinyrt-run.exe --events examples/wave_sim/events.json --frames 990,3300,6000
python tools/tinyrt.py pack examples/wave_sim --aot --target esp32s31 --development-key --wamrc path/to/wamrc.exe
python tools/tinyrt.py validate examples/wave_sim/build/demo.wave-sim.trpkg --development-key
```

S3 选择 `--target esp32s3` 与对应编译器，两种机器码不能互换。包内 Wasm 用于兼容与桌面预览；本项目设备验收要求 `selected_backend=2`、`fallback_reason=0`。
