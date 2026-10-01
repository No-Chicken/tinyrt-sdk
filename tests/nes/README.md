# NES N1 验证

测试分三层：`test_oracle.py` 独立计算预期帧 CRC，并验证原创 NROM 布局及固定上游哈希；`native.c` 执行真实 CPU/PPU、检查全部像素和状态；`test_wamr.c` 使用 TinyRT 真实 WAMR runtime 验证 ABI 回调、预算、帧文本、输入及生命周期。

主机测试目前使用 Windows/MSVC 及 `tinyrt/cmake/HostWamr.cmake`，不会访问硬件。从 SDK 根目录，在 MSVC x64 开发环境中执行：

```powershell
python examples/nes/build.py --cc <zig> --native
examples/nes/build/nes-native.exe
python tests/nes/test_oracle.py
python examples/nes/build.py --cc <zig>
cmake -S tests/nes -B tests/nes/build -G Ninja -DTINYRT_ROOT=<absolute-tinyrt-core> -DWAMR_ROOT_DIR=<absolute-wamr> -DNES_WASM=<absolute-nes.wasm> -DCMAKE_BUILD_TYPE=Release
cmake --build tests/nes/build
ctest --test-dir tests/nes/build --output-on-failure
tests/nes/build/test_nes_wamr.exe examples/nes/build/nes.wasm 100000 260
```

传入的 WAMR 使用 core 要求的固定 revision；核心的解释器配置、8 KiB 执行栈和预算计数实现均由 `tinyrt_runtime` 提供。

## 红绿证据

- `red-native.txt`：step 未推进时无法到达第 8 帧；CRC 早于 frame 发布时一致性检查失败。
- `red-wamr.txt`：Wasm ABI 时钟事件未转入 engine 时真实 runtime 无法推进帧。
- `red-wamr-stack.txt`：未分组的上游 CPU switch 在真实 WAMR 发生 `wasm operand stack overflow`。16 组分派修复无需扩大运行时栈。
- `green-native.txt`：16 项断言，包括整帧像素比较、NMI/算术状态、A 改色与释放、重新开始。
- `green-wamr.txt`：第 13 帧和新实例第 8 帧；原始有限轨迹共 6010 次 tick、12028 次回调。附加载入检查会使 assertion 计数增加 2。
- `green-wamr-long.txt`：首实例第 260 帧再创建新实例到第 8 帧，检查 8 位 NMI 回绕。实际帧 CRC 来自 guest 内 CPU/PPU 结果。

每个 init/event/render 都由真实 runtime 以给定指令预算调用。失败即终止该测试进程；未在 budget trap 后续跑 guest。render 验证 7 条绘图命令并解析 frame/CRC/CPU RAM 派生状态。创建、销毁后的受追踪运行时内存回到基线，system shutdown 后为 0。

## 预算和性能数据

```powershell
python tests/nes/budget_scan.py --runner tests/nes/build/test_nes_wamr.exe --wasm examples/nes/build/nes.wasm --output tests/nes/budget-scan.json
```

`budget-scan.json` 记录每次独立进程的结果。当前有限轨迹最低 passing budget 为 39244，39243 在首帧后的扫描线 0 报 instruction limit exceeded；正式 manifest 保持 100000。它不构成任意 ROM、sprite 内容、mapper 或全部 opcode 的最坏执行上界。

记录的 p50/p95/max 是桌面上混合 init/event/render 单回调耗时；`total_ms` 还包含加载、建实例、解析绘图命令等主机开销。`tracked_peak` 是 TinyRT 分配器记录值，不是进程 RSS 或板上 PSRAM 峰值。这些结果不能外推 ESP32-S3 帧率，设备调度和性能需单独测量。
