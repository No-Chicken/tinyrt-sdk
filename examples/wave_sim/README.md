# Wave v8

600 个立体小球在 466×466 圆屏中运动。透视投影、远暗近亮和高光提供深度感；速度增大时颜色趋近白色。保留 OCEAN、NEON、TOXIC、LAVA、MONO 五种配色，触摸长按 500 ms 切换，触摸取消不触发动作；轻点或短按 KEY1 扰动小球，长按 KEY1 由宿主退出。

正式清单使用 `balls_main.c`、`wave_balls.c` 和 `WAVE_BALL_COUNT=600`。旧水面模型保留在 `main.c`、`wave_physics.c`，供原有回归测试使用。应用版本唯一来源为 `app.json.version`，仅最终分发时递增。

应用使用已有 ABI 1 的 `SPRITE_BATCH`，要求宿主支持对应能力。初始化生成 16 级深度、64 级速度颜色表；驻留 INDEX8 图集采用 7 档速度，包含球体和高光。深度桶从远到近提交，投影直接使用 466 坐标。前后覆盖区域并集擦除旧像素，第一帧完整刷新。

物理状态采用 12 字节紧凑粒子、连续邻居网格和实际 dt（最多 50 ms）；17/17/16 ms 出帧节奏平均为 60 Hz。宿主负责双核调度、不可变帧租约、圆屏裁剪、双条带 DMA 缓冲和退出清理。应用不创建系统任务，也不直接访问面板或 IMU。BSP 统一新旧板的 IMU 安装方向，应用使用 X=ax、Y=-ay。

设备交付使用目标芯片的 AOT；桌面 Wasm 分片运行只验证功能，不代表真机性能。S31 三种合成姿态的 30 秒窗口约 60 fps；真实手感、物理方向和撕裂仍需人工观察。S3 性能尚未在本轮实测。完整数据保存在 SDK 源码的 `examples/wave_sim/VALIDATION-v8.md`。

从 SDK 根目录执行：

```powershell
python tests/wave_sim/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/wave_sim --cc path/to/zig.exe
python tools/tinyrt.py run examples/wave_sim --runner path/to/tinyrt-run.exe --events examples/wave_sim/events.json --frames 990,3300,6000
python tools/tinyrt.py pack examples/wave_sim --aot --target esp32s31 --development-key --wamrc path/to/wamrc.exe
python tools/tinyrt.py validate examples/wave_sim/build/demo.wave-sim.trpkg --development-key
```

S3 打包时选择 `--target esp32s3`。两种 AOT 机器码不能互换；桌面预览不执行设备 AOT。
