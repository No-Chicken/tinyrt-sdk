# Pixel Quest · 466×466 圆屏横版跳跃

应用发布版本唯一来源为 app.json.version，构建不自动递增。应用 ID 为 `demo.pixel-quest`。

原创横版平台跳跃：小探险机器人「豆豆」穿过草原、洞穴、云海和熔炉四个主题共八关，收集宝石，踩扁甲虫，最后打败熔核魔像。关卡布局全部原创，由 `prepare_levels.py` 生成；单关约 30 到 40 秒，每关一个检查点。

| 关卡 | 名称 | 看点 |
|---|---|---|
| 1-1 | GREEN START | 教学：宝石箱、小坑、石阶 |
| 1-2 | WINDY STEPS | 弹簧、黄蜂、跳虫 |
| 2-1 | ECHO CAVE | 低矮洞顶、刺背虫、第一个移动平台 |
| 2-2 | CRYSTAL SHAFT | 垂直平台与弹簧连续上升 |
| 3-1 | CLOUD HOP | 没有地面，只有高低错落的云岛 |
| 3-2 | STORM BRIDGE | 长木桥、台阶式木台、连续平台 |
| 4-1 | FORGE GATE | 岩浆池与平台接力 |
| 4-2 | CORE GOLEM | 熔核魔像，踩三次打开传送门 |

## 操作

- 倾斜设备左右移动（死区约 130 mg，约 400 mg 满速）；也可按住屏幕左侧或右侧移动。宿主对边缘首触有约 120 ms 返回判定，静止按住后才交给游戏；从边缘向内横滑仍是返回。
- 点按屏幕中间起跳；按住期间重力减半，跳得更高，轻点是小跳。
- 从上方落到甲虫、黄蜂、跳虫身上将其踩扁并反弹；刺背虫不能踩，只能绕开。从下方顶宝石箱得宝石，部分箱子里是护盾能量核；顶箱子还会掀翻站在上面的敌人。
- 护盾挡一次碰撞或尖刺。掉坑和岩浆挡不住。失去一条命后从起点或检查点复活，有 1.5 秒无敌闪烁；命用完后重打当前关。每 50 颗宝石奖励一条命。
- 落到弹簧上自动高跳；站在移动平台上会被带着走。
- 计分：宝石 100、踩敌 200、踩魔像 1000、过关 1000 加剩余每条命 500。最高分和解锁进度写入应用 KV，过关和正常退出时保存。
- 标题页点屏幕左右侧（手柄十字键左右）选择已解锁的关卡，点中间开始。
- 短按 KEY1 返回应用大厅（游戏不订阅 KEY1）。
- 接上 pogo 外设手柄后：十字键或左摇杆移动（摇杆死区 24），A 起跳、按住跳得更高，START 在标题页开始；此时不再读倾斜，手柄放在桌上也不会误动。外设的 HOME 键由宿主返回大厅。
- 游戏中拔掉手柄自动暂停，点屏幕或按 A 继续，之后恢复倾斜和触屏操作。

## 实现

- 逻辑画布 233×233，`gfx_begin` 整数放大 2 倍铺满圆屏；33 ms CLOCK 推进 1/16 像素整数物理并合成完整帧。
- 关卡按同字符横向行程存放：解释器每回调只有 10 万条指令，逐格扫描加一帧合成会超限。
- 128×128 INDEX8 图集与四套 30 色调色板放在包资源里，`init` 时一次上传；主题切换只换调色板槽。每帧提交天空 CLEAR、视差云和远山 SPRITE、16×15 的 TILEMAP 可见窗口、角色精灵与 SPRITE_BATCH 计分字形。
- 标题、结算文字用宿主 `draw_text_box`，只用 ASCII，避免设备字体子集缺字。
- 音效为 16 kHz PCM16：起跳、宝石、踩敌、撞块、受伤、检查点、通关、护盾、弹簧、魔像。haptic 用于踩敌、受伤与通关；无马达时宿主返回不支持，不影响游戏。
- 手柄使用 `TINYRT_IN_GAMEPAD`（kind 8，每个事件都是完整状态）。不支持手柄事件的旧固件会拒绝这个订阅并终止应用，所以本版本需要新固件。
- IMU 使用 `TINYRT_IN_MOTION`，屏幕 X 轴取 `ax` 并做一阶平滑；桌面预览用事件脚本注入，物理方向需在真机确认。

## 构建与验证

以下从 SDK 根目录运行：

```powershell
python examples/pixel_quest/prepare_assets.py
python examples/pixel_quest/prepare_levels.py
python tests/pixel_quest/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/pixel_quest --cc path/to/zig.exe
python tools/tinyrt.py run examples/pixel_quest --runner path/to/tinyrt-run.exe --events examples/pixel_quest/events.json --frames 500,6600,9000 --output examples/pixel_quest/build/preview
python tools/tinyrt.py pack examples/pixel_quest
python tools/tinyrt.py validate examples/pixel_quest/build/demo.pixel-quest.trpkg --rebuild-aot
python tools/tinyrt.py release examples/pixel_quest --cc path/to/zig.exe --runner path/to/tinyrt-run.exe
```

原生测试校验每帧记录格式、图集与资源边界、ASCII 文字，并用真实物理上的搜索求解器证明八关都能不受伤通关（每 4 步换一次输入，只走手柄事件路径），再覆盖选关与进度存档、左右按住移动、取消输入、长短跳、手柄移动与长短跳、拔手柄暂停、踩甲虫、护盾、岩浆、弹簧、平台带人、掉坑复活、魔像、游戏结束与最高分保存。求解器能过只证明关卡走得通，手感和难度仍要真机试玩。桌面截图来自真实 WAMR 解释器，不代表设备帧率；真机安装后需确认 `selected_backend=2`。

素材来源见 [LICENSES.md](LICENSES.md)。
