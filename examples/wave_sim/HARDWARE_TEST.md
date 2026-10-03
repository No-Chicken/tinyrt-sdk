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
python esp32/tools/ota_push.py tmp/wave-host/eebadge.bin --address 7C:4F:AD:BC:25:7A --pair
```

固件来自当前工作区，包含运动事件和音量零静音，也包含工作区既有的其他改动。
失败后先检查设备身份与连接状态，不连续盲重试。

## 2. 轴向

重启后在应用大厅打开接收/安装窗口，在电脑运行：

```powershell
python esp32/tinyrt-sdk/tools/ble_install.py --address 7C:4F:AD:BC:25:7A --pair install esp32/tinyrt-sdk/examples/wave_sim/build/axis-app/build/demo.wave-axis.trpkg
```

从应用大厅打开 Wave Axis Check，分别记录屏幕朝上平放、向右倾、向自己倾的 AX/AY/AZ。
先据此修正轴映射，再确认普通 Wave 中水向屏幕右、下方流动。

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
