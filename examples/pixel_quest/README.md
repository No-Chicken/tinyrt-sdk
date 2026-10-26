# Pixel Quest · 466×466 圆屏横版跳跃

应用发布版本唯一来源为 app.json.version，构建不自动递增。应用 ID 为 `demo.pixel-quest`。

原创横版平台跳跃：小探险机器人「豆豆」穿过草地、石阶、木桥与尖刺，收集宝石，踩扁甲虫，抵达终点传送门。一整关约 176 格宽，中段有检查点。

## 操作

- 倾斜设备左右移动（死区约 130 mg，约 400 mg 满速）；也可按住屏幕左侧或右侧移动。宿主对边缘首触有约 120 ms 返回判定，静止按住后才交给游戏；从边缘向内横滑仍是返回。
- 点按屏幕中间起跳；按住期间重力减半，跳得更高，轻点是小跳。
- 从上方落到甲虫身上将其踩扁并反弹；从下方顶宝石箱得宝石，顶箱子还会掀翻站在上面的甲虫。
- 碰到甲虫、尖刺或掉坑失去一条命，从起点或检查点复活，有 1.5 秒无敌闪烁。3 条命用完结束。
- 计分：宝石 100、踩甲虫 200、通关 1000 加剩余每条命 500。最高分写入应用 KV，正常退出保存。
- 短按 KEY1 返回应用大厅（游戏不订阅 KEY1）。
- 接上 pogo 外设手柄后：十字键或左摇杆移动（摇杆死区 24），A 起跳、按住跳得更高，START 在标题和结算界面开始；此时不再读倾斜，手柄放在桌上也不会误动。外设的 HOME 键由宿主返回大厅。
- 游戏中拔掉手柄自动暂停，点屏幕或按 A 继续，之后恢复倾斜和触屏操作。

## 实现

- 逻辑画布 233×233，`gfx_begin` 整数放大 2 倍铺满圆屏；33 ms CLOCK 推进 1/16 像素整数物理并合成完整帧。
- 128×128 INDEX8 图集与 30 色调色板放在包资源里，`init` 时一次上传。每帧提交天空 CLEAR、视差云和远山 SPRITE、16×15 的 TILEMAP 可见窗口、角色精灵与 SPRITE_BATCH 计分字形。
- 标题、结算文字用宿主 `draw_text_box`，只用 ASCII，避免设备字体子集缺字。
- 音效为 16 kHz PCM16：起跳、宝石、踩敌、撞块、受伤、检查点、通关。haptic 用于踩敌、受伤与通关；无马达时宿主返回不支持，不影响游戏。
- 手柄使用 `TINYRT_IN_GAMEPAD`（kind 8，每个事件都是完整状态）。不支持手柄事件的旧固件会拒绝这个订阅并终止应用，所以本版本需要新固件。
- IMU 使用 `TINYRT_IN_MOTION`，屏幕 X 轴取 `ax` 并做一阶平滑；桌面预览用事件脚本注入，物理方向需在真机确认。

## 构建与验证

以下从 SDK 根目录运行：

```powershell
python examples/pixel_quest/prepare_assets.py
python tests/pixel_quest/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/pixel_quest --cc path/to/zig.exe
python tools/tinyrt.py run examples/pixel_quest --runner path/to/tinyrt-run.exe --events examples/pixel_quest/events.json --frames 500,6600,9000 --output examples/pixel_quest/build/preview
python tools/tinyrt.py pack examples/pixel_quest --aot --development-key --target esp32s31 --wamrc tools/toolchains/esp32s31/windows-x64/wamrc.exe
python tools/tinyrt.py validate examples/pixel_quest/build/demo.pixel-quest.trpkg --development-key
python tools/tinyrt.py release examples/pixel_quest --target esp32s31 --cc path/to/zig.exe --runner path/to/tinyrt-run.exe --wamrc tools/toolchains/esp32s31/windows-x64/wamrc.exe --development-key
```

原生测试校验每帧记录格式、图集与资源边界、ASCII 文字，并用确定性自动驾驶不掉命走完整关，再覆盖左右按住移动、取消输入、长短跳、手柄移动与长短跳、拔手柄暂停、踩甲虫、掉坑复活、游戏结束与最高分保存。桌面截图来自真实 WAMR 解释器，不代表设备帧率；真机安装后需确认 `selected_backend=2`。

素材来源见 [LICENSES.md](LICENSES.md)。
