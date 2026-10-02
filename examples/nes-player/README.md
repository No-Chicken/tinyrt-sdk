# NES SkyTrail：资源型 mapper 0 播放器

本例从当前签名包的 `resources` 读取 iNES ROM，执行原始 6502 程序与 PPU，输出 256×240 RGB565 画面。默认 ROM 是本目录生成的原创 MIT 游戏 SkyTrail（应用标题 `NES Sky Trail`）：横向卷轴、左右移动、跳跃、巡逻敌人、金币与 START 重置均由 ROM 内的 6502 程序实现。游戏程序和像素素材见 [make_rom.py](make_rom.py)，许可见 [ROM-LICENSE](ROM-LICENSE)。

这是范围受限的 mapper 0 示例；不同本地 ROM 的运行正确性须单独验证。CPU/PPU 固定来源、Apache-2.0 许可与适配说明见 [third_party/nes](../../third_party/nes/README.md)，原始上游许可保留在仓库中。

## 构建、预览与安装

从 SDK 根目录运行，需要 Python 3.10+、`requirements.txt` 和 Zig 0.13.0：

```powershell
python -m pip install -r requirements.txt
python examples/nes-player/build.py --cc path/to/zig.exe
python tools/tinyrt.py run examples/nes-player --runner path/to/tinyrt-run.exe --frames 1000,3000
```

专用 `build.py` 会生成 `build/game.nes`、核查固定 CPU/PPU 来源并构建 `build/demo.nes-scroll.wasm`。本例需要其生成的 CPU 适配文件与额外来源，使用这个脚本构建。

匹配的 SDK 二进制发行目录包含固定摘要的 `bin/wamrc.exe` 时，下面的命令自动选择它，生成同时带 Wasm 回退与 ESP32-S3 AOT 的现代格式 1 包：

```powershell
python examples/nes-player/build.py --cc path/to/zig.exe --aot --development-key
python tools/tinyrt.py validate examples/nes-player/build/demo.nes-scroll.trpkg --development-key
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install examples/nes-player/build/demo.nes-scroll.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF launch examples/nes-player/build/demo.nes-scroll.trpkg
```

源码用户可追加 `--wamrc path/to/wamrc.exe`，编译器仍须匹配 SDK pin；缺少 bundled/cache 编译器时会尝试固定 Windows x64 下载来源。v0.1.0 下载附件尚未发布，当前请使用已核验的本地工具。BLE 依赖、配对流程、公开开发钥范围与设备 DEVELOPMENT 授权见 [SDK 指南](../../README.md#用户安装别人分享的包)。公开开发钥不能证明作者身份；设备还须启用开发钥并具有匹配的 AOT profile。`validate` 只验证包信封，不代替设备运行检查。

预览默认尊重 Guest 请求的虚拟时钟，执行真实 WAMR 解释器；它不代表 AOT 设备性能。输出 PNG 与报告位于 `build/preview/`，具体时序和输出保护见 [AGENTS.md](../../AGENTS.md)。可以保存下面的 JSON 为 SDK 根目录 `skytrail-events.json`，模拟右移并用 KEY1 跳跃：

```json
[
  {"ms": 600, "kind": "press", "x": 195, "y": 416},
  {"ms": 1100, "kind": "key_press", "x": 1, "y": 1},
  {"ms": 1300, "kind": "key_release", "x": 1, "y": 0},
  {"ms": 2600, "kind": "release", "x": 195, "y": 416}
]
```

```powershell
python tools/tinyrt.py run examples/nes-player --runner path/to/tinyrt-run.exe --frames 1000,3000 --events skytrail-events.json
```

## 操作与显示

| 输入 | 播放器行为 | SkyTrail 默认游戏 |
|---|---|---|
| UP / DOWN / LEFT / RIGHT | 对应 NES 方向键，按住持续输入 | LEFT / RIGHT 移动与卷轴；UP / DOWN 无额外游戏动作 |
| A / B | 对应 NES A / B | A 跳跃；B 无额外游戏动作 |
| START / SELECT | 对应 NES START / SELECT | START 重置位置与金币；SELECT 无额外游戏动作 |
| 设备 KEY1 | 按住 A，可与触摸方向键同时使用 | 跳跃 |
| 设备 KEY1 长按 | Host 返回应用大厅 | 离开应用 |
| 顶部 `+` / `1X` | 切换显示大小 | 同一游戏画面 |

触摸按下、移动、释放驱动屏幕按钮；释放或触摸 CANCEL 取消当前触摸按键。单个触摸点一次选择一个按钮，KEY1 可同时提供 A。应用订阅 KEY1 后，短按/双击不作为 Host 返回，长按仍保留 Host 返回。

默认 338×317 居中显示完整源图，适合圆屏；410×384 模式放大画面，四角会被圆屏裁掉，控件叠在画面上。缩放由 Host 使用 floor 最近邻采样。启动时先显示 `Loading cartridge`，完成分块加载和初始帧后出现游戏。

## 本地 ROM 与执行边界

仅使用用户已合法持有的本地 ROM；本工具不提供下载或分发其他 ROM：

```powershell
python examples/nes-player/build.py --cc path/to/zig.exe --rom path/to/local-game.nes --aot --development-key
```

`--rom` 将本地文件复制为 `build/game.nes`，替代默认生成的 SkyTrail；ROM 随签名包进入 resources。播放器仅接受以下范围：

- iNES mapper 0，NROM-128 / NROM-256：16 KiB 或 32 KiB PRG-ROM，恰好 8 KiB CHR-ROM。
- 水平或垂直镜像；拒绝 trainer、NES 2.0、four-screen、CHR-RAM、其他 mapper 与非零扩展头字段。
- 无音频、持久存档或通用文件系统；不宣称全部 opcode、ROM 或硬件时序兼容。

资源只在 init/event 中通过 `asset_read` 分块读取，每次最多 4096 字节；render 只提交画面。每回调预算保持 100000，长任务主动分片：解释器每次 CLOCK 处理最多 3 条扫描线，AOT 最多 64 条；按请求时钟安排后续工作。AOT 和 Wasm 执行相同 ROM 与 CPU/PPU 路径。

默认仅批量处理无副作用的 NROM `JMP` 自循环，保留周期和中断边界；背景和精灵逐帧重画，没有背景缓存。`--no-idle-batch` 禁用此批量处理，可用于与完整 CPU 路径对照。

## 原生检查与性能状态

原生检查针对默认原创 ROM 验证卷轴、跑跳、金币、敌人巡逻、重置以及非法/截断 ROM；它不生成可安装包：

```powershell
python examples/nes-player/build.py --cc path/to/zig.exe --native
examples/nes-player/build/nes-player-native.exe examples/nes-player/build/game.nes
# 完整 CPU 路径对照；--trace 输出逐帧状态与画面摘要：
python examples/nes-player/build.py --cc path/to/zig.exe --native --no-idle-batch
examples/nes-player/build/nes-player-native.exe examples/nes-player/build/game.nes --trace
```

2026-10-02 在 ESP32-S3 诊断固件上，原创滚动 ROM 的两个连续10秒窗口为23.70/23.70 FPS产帧、23.60/23.80 FPS被UI获取；背景与精灵完整重绘。该数值仅代表当前原创ROM、尺寸与设备配置，不是光学面板测量，也不保证其他ROM速度。人工可玩性与主观认可仍待完成。桌面预览的回调耗时、虚拟时钟和原生检查均不能替代设备实测。
