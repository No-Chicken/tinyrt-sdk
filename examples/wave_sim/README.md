# Wave Simulator · V1

466×466 圆屏中的重力像素水，40×40 LED 点阵、外环方向指示，五套主题
OCEAN / NEON / TOXIC / LAVA / MONO。显示缓冲为 233×233 小端 RGB565，
每格 4×4 灯珠和 1 px 间隔；Host 最近邻放大 2 倍。

## 操作

- KEY1 短按或触摸轻点掀浪。
- 触摸按住 500 ms 切换配色，一次按住只切一次；松开不掀浪。
- KEY1 长按或宿主返回控件退出，重新进入恢复初始水体。
- 设备接近平放（屏幕平面加速度小于 150 mg）时保留上一次重力方向。

需要支持运动事件扩展的宿主，见 [运动契约](../../specs/motion.md)。
默认轴映射是未验收配置，不能把桌面方向键响应当成设备轴向验证。
先安装 `demo.wave-axis`：平放、右倾、向自己倾，记录 AX/AY/AZ，
再设置清单 defines `WAVE_SWAP_AXES`、`WAVE_X_SIGN`、`WAVE_Y_SIGN`。
代码不包含任何已实测的轴向结论。

## 构建与预览

从 SDK 根目录执行，工具路径替换为本机匹配版本：

```powershell
python examples/wave_sim/tools/make_cover.py examples/wave_sim
python tests/wave_sim/run_native.py --cc <zig.exe>
python tools/tinyrt.py build examples/wave_sim --cc <zig.exe>
python tools/tinyrt.py run examples/wave_sim --runner <tinyrt-run.exe> --events examples/wave_sim/events.json --frames 1000,2500,3200,6000
python examples/wave_sim/tools/live_preview.py --runner <tinyrt-run.exe>
python tools/tinyrt.py pack examples/wave_sim --aot --development-key --wamrc <wamrc.exe>
python tools/tinyrt.py validate examples/wave_sim/build/demo.wave-sim.trpkg --development-key
```

方向键模拟倾斜，Space 模拟 KEY1，鼠标短按/长按模拟触摸。
轴向调试包由 `python examples/wave_sim/tools/build_debug.py` 生成清单，
随后 build/pack `examples/wave_sim/build/axis-app`。

同一脚本生成 `examples/wave_sim/build/perf-app`（`demo.wave-perf`）。
此版本每 2 秒更新 FPS、P（每个完整物理步平均 ms）和 R（每张完整帧合成平均 ms）。
计时使用宿主实时 `now_ms()`，分辨率为 1 ms；小于 1 ms 的结果可能显示 0.0。
R 不包含宿主图像缩放和屏幕刷新，FPS 统计应用提交的完整帧。
真机请使用 AOT 包；桌面 runner 的虚拟时间不能用于耗时验收。

## 物理与预算

520 个粒子，Verlet 积分，两个位置约束迭代，空间哈希搜索 3×3 邻格，
圆形容器半径 19.4 格，重力 38，阻尼 0.995，边界反弹 0.276。
AOT 请求 16 ms 时钟，每秒真实时间累计 120 步，每步 1/60 秒；
每回调最多 8 步，超量丢弃。33 ms 绘制节拍保留余数，避免 16 ms 时钟
把绘制固定拖成 48 ms。长暂停最多累计 100 ms。

解释器为满足每回调 100000 指令上限，请求 1 ms 时钟，分片推进相同的
邻居推挤和画面合成；只提交完整帧，不减少粒子、网格或改变色阶。
它的运行节奏较慢，不能代表 AOT 的 2 倍模拟速度或真机性能。
没有启用性能降级；需要真机确认 AOT ≥25 FPS 后才能验收。

颜色按密度、逆重力方向的空邻格、速度大于 0.35 的水花和有序闪烁分级。
颜色来自用户提供的设计原型；代码独立实现。四次浮点 Newton 迭代计算平方根，
不依赖 libc、WASI、动态分配或新增 Host import。

## 验证边界

原生测试保留断言，包含五分钟模拟晃动、粒子数量、有限值、圆形边界，
触摸取消、长按阈值、KEY1 超时不掀浪、时钟回绕与完整图像尺寸。
桌面 WAMR 可以验证 Guest 契约和截图，不能证明真机帧率、触摸或轴向正确。
实测状态、静态内存和包大小记录在 VALIDATION.md。
