# Wave v7

圆屏粒子水面应用，正式配置为 20×20 网格、130 粒子。五主题为 OCEAN、NEON、TOXIC、LAVA、MONO。倾斜改变重力，触摸长按 500 ms 切换主题，短按 KEY1 掀浪，长按 KEY1 退出；触摸取消不触发换色。S3 安装角 180°、X 正向、Y 反向；S31 安装角 0°、X 反向、Y 正向。

宿主绘制 400 字节 GRID 索引与调色板，整帧几何为 1484 字节。逻辑灯面 9 像素、间隔 1 像素、中心 16 像素，宿主放大两倍。加速路径不分配 Guest 完整图像；能力不足时动态使用旧完整帧路径。图形接口见 [graphics-v1](../../specs/graphics-v1.md)，输入见 [motion](../../specs/motion.md)。

物理使用固定步 Verlet、两轮邻域约束和 3×3 哈希邻域。网格比例 `WAVE_GRID/40` 缩放圆域、重力、掀浪、扰动及速度白色阈值；单元邻接、密度、抖色和调色板规则保留。20 网格半径为 9.7 单元、重力 19，阻尼 0.995、反射系数 0.276。AOT 每秒 60 个真实步，H=1/30，以两倍时间推进；暂停最多累积 100 ms，每次最多八步。解释器分片用于控制回调长度，不能据其耗时推断设备性能。

源码默认 `WAVE_GRID=40`，正式清单覆盖为 20/130；比较时覆盖 `WAVE_GRID=40,WAVE_N=400`。应用发布版本唯一来源是 app.json.version，不随构建次数增加；正式 ZIP 为 release/demo.wave-sim-v7-wasm-aot.zip。

从 SDK 根目录执行：

```powershell
python tests/wave_sim/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/wave_sim --cc path/to/zig.exe
python tools/tinyrt.py run examples/wave_sim --runner path/to/tinyrt-run.exe --events examples/wave_sim/events.json --frames 990,3300,6000
python tools/tinyrt.py pack examples/wave_sim --aot --development-key --wamrc path/to/wamrc.exe
python tools/tinyrt.py validate examples/wave_sim/build/demo.wave-sim.trpkg --development-key
```

临时性能包使用同一应用 ID 和 revision，增加 metrics 显示：

```powershell
python examples/wave_sim/tools/build_debug.py
python tools/tinyrt.py build build/diagnostic-wave --cc path/to/zig.exe
python tools/tinyrt.py pack build/diagnostic-wave --aot --development-key --wamrc path/to/wamrc.exe
```

测量结束恢复普通包，不安装额外应用。metrics 的 P/R 为毫秒分辨率的 Guest 物理和组合时间，提交 FPS 与真实面板帧率不同。最终 build13 连续运动真实面板约 29.77 FPS，平稳及主题约 30.07–30.34 FPS，达到已测连续运动场景的 25 FPS 目标。原生绘制稳定约 4.58–4.72 ms。输入软件注入与五主题验证通过，人工手感仍待确认；完整证据和限制见 [VALIDATION](VALIDATION.md)，设备操作见 [HARDWARE_TEST](HARDWARE_TEST.md)。
