# Pomodoro ABI tests

同一份 `test_pomodoro.c` 在 native 和真实 TinyRT/WAMR 中通过公开 ABI 驱动生产 guest；可控时钟与 KV 宿主回调是测试夹具，不替换应用逻辑。WAMR 模式每次 reopen 销毁并重新创建实例，最后检查所有追踪分配释放。

原生：

```powershell
python tests/pomodoro/run_native.py --cc path/to/zig.exe
python tests/pomodoro/run_native.py --cc path/to/zig.exe --fast
```

WAMR 可选集成（Windows x64，MSVC 开发者终端，CMake + Ninja）：

```powershell
python tools/tinyrt.py build examples/pomodoro --cc path/to/zig.exe
python tools/tinyrt.py build examples/pomodoro/app-fast.json --cc path/to/zig.exe
cmake -S tests/pomodoro -B build/wamr -G Ninja -DCMAKE_BUILD_TYPE=Release -DTINYRT_CORE_DIR=path/to/tinyrt -DWAMR_ROOT_DIR=path/to/wamr -DPOMODORO_WASM=path/to/sdk/examples/pomodoro/build/demo.pomodoro.wasm -DPOMODORO_FAST_WASM=path/to/sdk/examples/pomodoro/build/demo.pomodoro.fast.wasm
cmake --build build/wamr
ctest --test-dir build/wamr --output-on-failure -V
```

必须使用 TinyRT `dependencies.lock.json` 固定的干净 WAMR revision。通过核心的 `cmake/HostWamr.cmake` 使用与设备相同的 classic interpreter、指令计量、无 WASI/threads/AOT/JIT/bulk-memory 配置。

2026-10-01 主机结果：native 正式 486、快进 161 条断言；WAMR 正式 535、快进 182 条断言，全部通过。每回调上限 100000、线性内存最大 2 页。正式/快进运行时追踪内存峰值分别 93476/93482 字节；统计包括实例/frame/module，但不含操作系统线程/锁与分配器头。只记录测试读取点峰值，核心另有 2 MiB 硬限制；未统计设备物理堆峰值或精确指令余量。

覆盖：默认/暂停/开始/继续/重置；不规则毫秒差；uint32 回绕；完整 25m/5m；快进 25s/5s；长延迟只切换一次；暂停恰逢阶段结束；暂停恰逢检查点仅一次写入；100 tick 零额外写入；60 秒检查点；销毁重建与重启时钟归零；坏状态回默认；正式/快进存档配置隔离。

本测试不包含硬件、BLE、背景计时或通用 SDK 模拟器。
