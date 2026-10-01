# Pomodoro

前台工作 25 分钟、休息 5 分钟。屏幕下方左按钮开始/暂停，右按钮重置到工作阶段。工作结束进入休息并暂停；休息结束回工作并暂停。较大的时间跳变只切换一次，等待用户开始下一阶段。

```powershell
python tools/tinyrt.py build examples/pomodoro --cc path/to/zig.exe
python tools/tinyrt.py pack examples/pomodoro --key path/to/application-signing.pem --key-id 100
```

从 SDK 根目录执行。正式应用 ID 为 `demo.pomodoro`；验收快进包独立 ID 为 `demo.pomodoro.fast`，清单标题与画面均明确标记 TEST 25s/5s，使用完全相同的计时逻辑：

```powershell
python tools/tinyrt.py build examples/pomodoro/app-fast.json --cc path/to/zig.exe
python tools/tinyrt.py pack examples/pomodoro/app-fast.json --development-key --key-id 1
```

## 保存和计时

使用 `uint32_t now_ms()` 的实际无符号差值计时，可跨一次计时器回绕；连续两次调度的间隔必须小于 2^32 ms。不会按 tick 次数累减。重新打开始终暂停，不把跨重启的单调时钟值当作截止时间。

KV key 0 的单个 i32 同时保存版本/配置标记、阶段、剩余毫秒。开始、暂停、重置、阶段切换各保存一次；运行时每满 60 秒保存一次检查点，100 个未跨检查点的 tick 不保存。非法版本、保留位或越界剩余时间回到有效的默认暂停状态。

当前 ABI 没有退出回调。运行中退出/断电恢复最近检查点，正常调度下最多约丢失 60 秒；如果宿主长时间未调度，丢失范围可更大。要精确保存请先暂停。仅在前台计时，当前没有后台提醒、声音或振动。

## 验收状态

2026-10-01：正式与快进版本均通过原生可控时钟测试和同配置真实 WAMR 测试；包括完整阶段、暂停/恢复/重置、非规则时间、回绕、检查点、销毁/重建、坏档恢复与写次数。每回调预算 100000、最大 2 页内存；详见 [tests/pomodoro](../../tests/pomodoro/README.md)。

本目录的主机测试不构成 BLE/设备实测证据。BLE 安装、设备重启、屏幕和物理触摸需在 EEBadge 产品验收中另行记录。
