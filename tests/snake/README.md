# Snake ABI tests

`test_snake.c` 通过公开 `init/event/render/stop` 回调驱动生产 guest。同一组场景运行于原生 C 与真实 TinyRT/WAMR；测试只替代宿主时钟、KV 和原生绘图捕获，不读取或修改 guest 私有状态。WAMR 使用原始 Wasm 和与设备相同的 classic interpreter、指令计量、无 WASI/threads/AOT/JIT/bulk-memory 配置。

```powershell
python tests/snake/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/snake --cc path/to/zig.exe
```

Windows x64 MSVC 开发者终端中构建 WAMR 测试：

```powershell
cmake -S tests/snake -B build/snake-wamr -G Ninja -DCMAKE_BUILD_TYPE=Release -DTINYRT_CORE_DIR=path/to/tinyrt -DWAMR_ROOT_DIR=path/to/wamr -DSNAKE_WASM=path/to/sdk/examples/snake/build/demo.snake.wasm
cmake --build build/snake-wamr
ctest --test-dir build/snake-wamr --output-on-failure -V
```

WAMR checkout 必须匹配 TinyRT `dependencies.lock.json`，本轮为 `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`。测试策略固定为 ABI 1、permissions 15、2 页内存、每回调 100000 指令。每次 reopen 销毁并创建新 Wasm 实例；最终验证追踪分配全部释放，峰值低于 2MiB。

覆盖行为：

- READY/开始/暂停/继续/重置/结束重开，初始方向、食物、分数和最高分。
- 不规则毫秒差、400ms 边界、uint32 回绕、延迟最多补两步且没有待处理积压；迟到触摸不改变过去的移动方向。
- 直接反向无效、单步只接受一次转弯、暂停方向输入无效、按钮圆角和宿主返回区域不误触。
- 食物不与蛇身重叠、吃食增长、撞墙、撞身体、移动到将离开的尾格合法。
- 只靠可见帧与方向输入，沿覆盖整个棋盘的环路吃满 80 格；检验每一帧的蛇头唯一、食物唯一、格子不重叠、圆屏边界和绘图数量。
- 新最高分保存、相同最高分不写、重开与时钟归零后恢复、坏存档回默认、stop 推进最后移动、写失败不覆盖旧存档。

2026-10-01 的结果：原生 1,139,378 项断言，WAMR 1,141,620 项断言，均零失败；其中多数断言是每帧的几何与格子检查。满棋盘环路耗用 1487 次移动，最大 98 条绘制命令；WAMR 峰值追踪分配 98,864B，结束归零。这不代表设备物理堆或线程开销。

## 实际帧导出

```powershell
mkdir path/to/frames
build/snake-wamr/test_snake.exe examples/snake/build/demo.snake.wasm --frames path/to/frames
```

输出 `ready.bin`、`running.bin`、`paused.bin`、`game-over.bin`、`restored.bin`、`win.bin`。每份为当前宿主 `tinyrt_frame_t` 布局的 14340B little-endian 测试夹具，不是跨版本稳定传输协议。

EEBadge 的现有 `tools/ui_tests` 预览器读取固定名称 `ready/running/break/restored`；预览 paused 时复制 `paused.bin` 为临时 `break.bin`。要预览 game-over/win，可在另一个临时目录把它们分别复制为 `ready.bin/running.bin`，其余名称复制已有帧。设置 `TINYRT_PREVIEW_FRAMES` 后从 EEBadge `esp32` 目录运行 `ui_tests.exe`；输出 BMP 仅做无损 PNG 转换。本示例截图经过实际生产字体与圆形遮罩检查。

本测试没有触摸控制器、BLE、Flash 断电或设备性能证据；这些由产品集成验收单独记录。
