# Pomodoro ABI tests

同一份 `test_pomodoro.c` 在 native 和真实 TinyRT/WAMR 中通过公开 ABI 驱动生产 guest；可控时钟与 KV 宿主回调是测试夹具，不替换应用逻辑。WAMR 模式每次 reopen 销毁并重新创建实例，最后检查所有追踪分配释放。

原生：

```powershell
python tests/pomodoro/run_native.py --cc path/to/zig.exe
python tests/pomodoro/run_native.py --cc path/to/zig.exe --fast
python tests/pomodoro/run_native.py --cc path/to/zig.exe --round
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

每回调上限 100000、线性内存最大 2 页。WAMR 程序打印当前断言数和追踪分配峰值；这不包括设备物理堆与操作系统线程开销。

覆盖：默认/暂停/开始/继续/重置；不规则毫秒差；uint32 回绕；完整 25m/5m；快进 25s/5s；长延迟只切换一次；暂停恰逢阶段结束；暂停恰逢检查点仅一次写入；100 tick 零额外写入；60 秒检查点；销毁重建与重启时钟归零；坏状态回默认；正式/快进存档配置隔离；正常 stop 精确保存最后毫秒差、停止恰逢阶段切换、stop 写失败不覆盖存档；圆屏关键图形与文字包围框均在 R=215 的安全圆内，系统返回热区和透明圆角不触发按钮。

本测试不包含硬件、BLE、背景计时或通用 SDK 模拟器。

## 实际 Wasm 帧导出

在现有目录中输出真实 WAMR 执行生产 guest 得到的完整 `tinyrt_frame_t`，不重画应用：

```powershell
mkdir path/to/frames
build/wamr/test_pomodoro.exe examples/pomodoro/build/demo.pomodoro.wasm --frames path/to/frames
```

生成 `ready.bin`、`running.bin`、`break.bin`、`restored.bin`，分别为初始暂停、运行、阶段完成等候、正常停止后恢复暂停。每文件 14340 字节（当前 host C frame 布局，little-endian；只作匹配版本渲染器的测试夹具，不是稳定传输格式）。`--fast` 可与 `--frames` 同用。该程序照常执行全部行为测试，并检查每次真实 Wasm 帧的圆屏边界与大字号时间；设备画面/字体实际裁切仍由 LVGL 渲染和产品验收验证。