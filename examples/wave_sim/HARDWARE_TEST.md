# 真机更新和验证

已识别目标：`EEBadge`，BLE 地址 `7C:4F:AD:BC:25:7A`。
广播窗口可能自动关闭，发送前在对应页面重新打开。

以下是用户手动执行的命令，从 `C:\projects\Git_Projects\EEBadge` 打开 PowerShell。
固件更新写入 OTA 应用槽并重启设备；随后安装调试应用会写入应用存储。
这些命令没有擦除、分区表或 bootloader 更新。
本轮只完成编译和包检查，未执行硬件写入。

## 1. 宿主固件

在设备打开固件传输/BLE OTA 页面，再运行：

```powershell
python esp32/tools/ota_push.py tmp/wave-aot-build/eebadge.bin --address 7C:4F:AD:BC:25:7A --pair
```

固件来自当前工作区，包含运动事件和音量零静音，也包含工作区既有的其他改动。
此目录单独开启 `CONFIG_TINYRT_AOT=y`，使用匹配 SDK 的来源 profile 与 demo. 开发 AOT 信任授权。
旧 `tmp/wave-host/eebadge.bin` 未开启 AOT，不再作为 Wave 验证固件。
失败后先检查设备身份与连接状态，不连续盲重试。

重启后打开应用接收窗口，先确认宿主 `aot_enabled=true` 和 `development_aot=true`：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A runtime
```

## 2. 轴向

重启后在应用大厅打开接收/安装窗口，在电脑运行：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A --pair install esp32/tinyrt-sdk/examples/wave_sim/build/axis-app/build/demo.wave-axis.trpkg
```

从应用大厅打开 Wave Axis Check，分别记录屏幕朝上平放、向右倾、向自己倾的 AX/AY/AZ。
先据此修正轴映射，再确认普通 Wave 中水向屏幕右、下方流动。

每个包安装后先查询详情，再运行。必须是 `selected_backend=2`、`fallback_reason=0`；
不能把传输完成、包中包含 AOT 或安装成功当成实际使用 AOT。
已配对时无需重复加 `--pair`。传输完成后 Windows 报取消时，重新打开接收窗口，先查安装身份：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A info --app-id demo.wave-axis
```

未确认安装前不重复发送。客户端已对 WinRT 的 `-2147023673` 加入只查询的结果核对，
仅当设备返回相同完整身份才能报成功；不会因该取消错误重新传输。

## 3. 性能

再次打开接收窗口，安装性能包：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A --pair install esp32/tinyrt-sdk/examples/wave_sim/build/perf-app/build/demo.wave-perf.trpkg
```

打开 Wave Performance Check，至少运行两秒后记录 FPS/P/R；分别记录静止、四向倾斜、掀浪时结果。
P 是平均单物理步 ms，R 是平均单帧合成 ms，FPS 是完整帧提交数。
计时 1 ms 分辨率，桌面虚拟时钟结果不用于验收。

## 4. 普通包

轴向确认并重新生成包后安装普通版：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A --pair install esp32/tinyrt-sdk/examples/wave_sim/build/demo.wave-sim.trpkg
```

另请把下拉栏音量调到零，检查 Bird 与其他声音是否静音，再调高确认恢复。
验收清单见 VALIDATION.md。
