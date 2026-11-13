# TinyRT SDK

在设备应用大厅打开“添加”页后，运行 `python tools/tinyrt.py install <pkg>`。默认优先 USB，多台 USB 设备用 `--port COM11` 指定，也可用 `--transport ble --address <地址>` 选择蓝牙。

开发固件支持 `python tools/tinyrt.py dev examples/pixel_quest --watch`。命令构建并打包应用，自动寻找 USB 开发板，安装后等待设备显示应用页面，再持续输出 `app:` 日志。修改源码、清单或素材会触发重新构建和安装，按 Ctrl+C 退出。使用 BLE 时，先在设备上打开“添加”页，再指定 `--transport ble --address <地址>`，以便在同一连接内安装和启动。

客户版固件在大厅打开“添加”页，运行 `python tools/tinyrt.py install <pkg> --transport ble`，在设备上确认后从大厅启动。

远程安装由设备确认；应用权限只用于协议与运行时校验。

在“添加”页运行 `python tools/tinyrt.py info` 可读取本次开机最近一次应用崩溃的包身份、回调阶段和原因。连接参数与 `install` 相同。设备保留一条记录，后续崩溃会覆盖它。运行时能定位出错指令时显示函数索引和 PC，缺失的字段显示“未知”。

Magie（S31）设备只运行 AOT 应用。`pack` 默认编译并附带 S31 AOT。桌面预览使用解释器，帧耗时不能用来估计设备性能。

独立开发、编译、预览和打包 TinyRT ABI 1 的 C/Wasm 应用。当前是开发预览：包格式、store 与 BLE schema 都为 1。普通 C/Wasm 构建不依赖 ESP-IDF；桌面预览使用真实 WAMR runner，AOT 使用固定目标与来源的编译器。权威契约与运行验证属于 TinyRT core。

游戏模板默认使用 `clock_interval(33)`，约 30 Hz。每次 `TINYRT_CLOCK_EVENT` 更新游戏逻辑，随后在 `tinyrt_render()` 提交一张完整帧；静止画面可以 `draw_skip()`。不要靠累计多次 CLOCK 才提交来控制帧率。物理固定步长应保留实际经过时间的余数补偿；需要更高帧率的应用可显式请求其他周期，实际显示频率受宿主和送屏能力限制。

SDK 对外版本以根目录 VERSION 为准，用 `python tools/tinyrt.py --version` 查询。
应用发布版本见各自 `app.json.version`；反复编译不自动升版本。

Wave 为 40×40 网格、400 粒子的二维水面效果，见 [Wave 示例](examples/wave_sim/README.md)。

图形批次、GRID、驻留精灵与兼容要求见 [graphics-v1.md](specs/graphics-v1.md)。

麦克风需要 MIC 权限（64）和 API Level 3。设备特征采集、玩家标定和桌面 WAV 回放命令见[麦克风标定与声音检测](specs/audio-detectors.md)。

## 一键生成网站交付 ZIP

安装 requirements.txt 并准备本地工具后，从 SDK 根目录执行：

```powershell
python tools/tinyrt.py release examples/flappy --cc path/to/zig.exe
python tools/tinyrt.py release examples/puff --cc path/to/zig.exe
```

设置 `TINYRT_CC` 后也可执行 `make release APP=examples/flappy`。
Windows 没有 make 时直接使用 Python 命令。

命令离线完成资料检查、原生测试（如有）、编译、真实 WAMR 截图、打包、网站 v2 清单和 ZIP 自检。
本项目只交付 AOT：Flappy 输出 `examples/flappy/release/<app_id>-v<version>-wasm-aot.zip` 及同名展开目录。
ZIP 包含应用包、封面、截图、README、CHANGELOG、LICENSES、清单和构建报告。
release 默认为 wasm-aot，`--target` 选择 AOT 设备目标（默认 `esp32s31`，使用仓库内 `tools/toolchains/esp32s31/windows-x64/wamrc.exe`）。
`--output` 指定输出目录。`.wasm` 用于编译和桌面预览，AOT 包内保留源 Wasm 供复编译比对。S31 安装要求兼容的 AOT section。
失败保留此前完整交付件；诊断报告写入应用 `build/release-report.json`（安全可写时）。
三个 `new` 模板均带齐发布资料和占位封面，创建后即可运行 release。新建时排除 build、release、zig-cache 和 Python 缓存。

`pack` 输出供设备安装的 `.trpkg`；上传网站使用 `release` 输出的 ZIP，例如 `examples/puff/release/puff-v<version>-wasm-aot.zip`。ZIP 由工具统一生成清单、文件摘要和校验结果。

开发者维护 listing.json、三个 Markdown 和封面。完整 listing 示例：

```json
{
  "summary": "圆屏像素游戏",
  "category": "game",
  "tags": ["像素"],
  "controls": ["点击起跳", "KEY1 长按退出"],
  "publisher_display": "开发者",
  "release_notes": "v2：新增通用图形绘制。",
  "full_bleed": true,
  "rights": {"status": "unverified", "note": "来源与许可见 LICENSES.md"},
  "device_verified": false,
  "screenshots": [{"at_ms": 32, "caption": "准备"}, {"at_ms": 3300, "caption": "运行"}]
}
```

截图也可用 `{"file":"screenshots/device.png","caption":"真机"}`，必须原本为 466×466 单帧 PNG。
at_ms 与 file 二选一，1–6 张。封面必须为 210×210 RGB/RGBA PNG、≤64 KiB。
三个 Markdown 必须 UTF-8 无 BOM、LF、各≤64 KiB。未知字段、重复键、超时和错误尺寸都报错。
应用版本唯一来源是 app.json.version；ZIP 名称、应用包和 release.json 使用同一个整数。listing 不允许 display_version。
上架更新要递增版本，设备本身不强制，反复本地构建不自动涨版本。

构建时间固定取 SDK 提交时间，可用 SOURCE_DATE_EPOCH 或 --built-at 覆盖；同样输入和工具环境产出相同字节。
无 Git 的源码分发须传 --sdk-revision 和固定时间。
规范见 [v2 发布规则](specs/release-bundle-v2.md) 和 [listing Schema](specs/listing.schema.json)。
本地自检不包含网站账号与发布审核，在线验收仍需上传联调。

## 开发者：创建、预览、打包

Windows x64 的已打包 `wamrc.exe` 还需要 Microsoft Visual C++ v14 x64 运行库（14.51或以上）；可安装[微软最新运行库](https://learn.microsoft.com/zh-CN/cpp/windows/latest-supported-vc-redist)。已打包桌面 runner 使用静态CRT。

需要 Python 3.10+，以及可生成 wasm32 的 Clang 或 Zig 0.13.0。安装 `requirements.txt`（打包用 cryptography，PNG 用 Pillow）；用 `--cc` 或 `TINYRT_CC` 指定编译器。各导入函数的签名、权限、调用阶段、限额和错误处理见 [API 参考](docs/api.md)。

```powershell
python -m pip install -r requirements.txt
python tools/tinyrt.py new my-game --title "My game" --template game
python tools/tinyrt.py build my-game --cc path/to/zig.exe
python tools/tinyrt.py run my-game --frames 0,330
python tools/tinyrt.py pack my-game
python tools/tinyrt.py validate my-game/build/my-game.trpkg
```

`new` 创建尚不存在的目录；默认模板为 `minimal`，`game` 提供触摸、时钟、帧缓冲与缩放，`watchface` 演示本地时间和分钟事件（需要 ABI 1.3、固件 0.4.0 及以上）。`build` 编译清单中的 C 文件，产物为 `build/<app_id>.wasm`；`pack` 默认读取此文件并写入 `build/<app_id>.trpkg`。`--output` 自定义输出，`pack --wasm` 选择既有 Wasm。输出后缀、魔数和链接目标受检查，不能覆盖源文件、清单或资源；损坏产物需选择新路径。

`app.json` 可选 `"cover": "cover.png"`，输入为单帧 210×210 PNG，最多 64 KiB。`pack` 将其转换为 cover section，内含 210/150 两档 RGB565；整包增加 133232 字节，计入整包长度。透明区使用大厅底色 `#101418`。封面独立于 Guest resources，完整字节格式见 [包规格](https://github.com/No-Chicken/tinyrt/blob/main/specs/package.md)。 手机封面分块读取、设备确认 token 与空间预检字段见 [手机应用管理](https://github.com/No-Chicken/tinyrt/blob/main/specs/mobile-management.md)。

`run --events events.json` 按时间驱动真实 WAMR 解释器，输入示例见 [Wave 事件脚本](examples/wave_sim/events.json)。`--frames` 是0..60000 ms的截图时间列表，最多120张；默认尊重Guest的`clock_interval`，按虚拟deadline执行CLOCK；截图与输入时点不会额外触发CLOCK或render。回调墙钟耗时不推进虚拟时钟。`--step-ms`仅在显式传入时强制按固定间隔注入CLOCK，覆盖Guest请求的周期，用于诊断。默认输出 `build/preview/` 下的466×466圆形PNG和`report.json`。报告含回调耗时与heap_bytes；桌面耗时不是设备FPS，字体与设备也不完全一致。runner 优先使用 `--runner` 或 `TINYRT_RUNNER` 指定的路径。默认使用 SDK 内置的本机 runner，按对应平台的 pin 校验 SHA-256 和大小。缺少本机 pin 或二进制时，按提示安装对应的 SDK 或指定自建 runner。输出只更新当前工具标记的PNG和report，未知已有文件、输入及链接目标会被拒绝；旧版未标记预览请用新的`--output`目录。RGB565缩放使用与Host一致的floor最近邻采样。源码用户可以按 [core构建说明](https://github.com/No-Chicken/tinyrt) 构建runner后显式传入。

`pack` 默认为 ESP32-S31 编译 AOT 并保留源 Wasm，使用仓库内编译器，不需要任何密钥或额外参数；`app_id` 只要合法就行，不要求命名空间。`--wasm-only` 跳过 AOT（S31 固件不运行这种包）。SDK 用 pin 校验编译器 SHA-256，目标 ABI 与 bounds/stack/loop-poll 检查均固定，不接受上传者提供的 AOT 或自报安全参数；`--wamrc` 也必须匹配固定摘要。文件说明见[编译器目录](tools/toolchains/esp32s31/windows-x64/README.md)，目标细节见[设备目标](specs/device-targets.md)。

设备不校验签名，只校验包结构、完整性、ABI、权限和 AOT 兼容性。SDK 仍在包头写入公开的兼容签名，开发者无感，只为让包能装到旧固件；不能证明作者身份。

**`validate`仅检查包信封。** 它验证规范字段、section布局、策略、长度和摘要；输出含`validation_level: envelope`、`wasm_validation: not_performed`、`aot_validation: not_performed`。设备必须再运行core结构/ABI/目标/兼容性与执行保护校验。AOT元数据是声明，不能单独证明机器码安全；`validate --rebuild-aot` 重新编译包内 Wasm 并要求 AOT 逐字节一致，发布平台必须用它。

## 用户：安装别人分享的包

打开设备应用大厅的限时BLE安装入口。固件准备方式由产品提供；本工具只安装应用包。电脑准备依赖后扫描、配对、安装：

```powershell
python -m pip install bleak
# Windows显式配对还需：
python -m pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Enumeration
python tools/ble_install.py --scan
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install my-game/build/my-game.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id my-game
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF launch my-game/build/my-game.trpkg
```

按设备显示PIN配对；自动化终端用`--pin`提供六位PIN。完整包身份的最终QUERY确认安装结果。`ble_install.py info` 的 `selected_backend=2`、`fallback_reason=0` 表示应用使用已验证的 S31 AOT。手机安装入口尚未完成时使用电脑流程。分享包时说明固件目标。

## 马达反馈

应用清单在原权限基础上加 `32`（HAPTIC），例如 DRAW + INPUT + CLOCK + HAPTIC 为 `43`。
在 `tinyrt_init` 或 `tinyrt_event` 中调用 `haptic_pulse(50, 100)`，以 50% 强度震动 100 ms；
`haptic_stop()` 提前停止。调用非阻塞，新脉冲替换旧脉冲；强度范围 1..100%，时长 1..2000 ms。
返回 0 表示成功，1 表示设备无马达或不可用；非法参数、缺权限或在 render/stop 调用会失败。
宿主在应用退出、异常或切换时停止马达。S31 支持，桌面预览返回不支持。
需要支持这些 import 的宿主与桌面 runner；旧宿主拒绝新 import。

## 外设手柄

吧唧经 pogo 外设口接手柄时，输入以 `TINYRT_EV_GAMEPAD`（kind 8）送给前台应用，需要 INPUT 权限。
在 `tinyrt_init` 里把 `TINYRT_IN_GAMEPAD`（0x100）或进订阅掩码，可与触摸、KEY1、MOTION 同时订阅：

```c
input_events(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_IN_MOTION|TINYRT_IN_GAMEPAD);
```

每个事件都是完整状态：`x` 为按键位图（`TINYRT_PAD_A`…`TINYRT_PAD_RIGHT`、`TINYRT_PAD_FN(1..8)`，bit16..23 恒为 0），
`y` 为四个 int8 摇杆（`TINYRT_PAD_LX/LY/RX/RY(y)`，+X 右、+Y 下），`arg` 为扳机与标志
（`TINYRT_PAD_LT_VALUE/RT_VALUE(arg)`、`TINYRT_PAD_CONNECTED(arg)`）。按键变化保序送达，一次轻点的按下和松开
即使落在同一帧也都会收到；摇杆变化会合并。外设的 HOME 键由宿主保留，用于返回大厅；切换应用时仍按住的键要松开后才会出现。
订阅时外设已连接，宿主会立即补发一次当前状态；拔掉外设时收到 `CONNECTED=0` 且全部松开，应用自行决定是否暂停。

**兼容性**：不支持手柄事件的旧固件会拒绝 0x100 并终止应用，重新订阅也无法降级。订阅手柄的应用要求新固件，
请在 CHANGELOG 与 listing 中注明。桌面预览的事件脚本格式见 [specs/gamepad.md](specs/gamepad.md)。

## app.json

```json
{
  "app_id": "demo.example",
  "title": "Example",
  "version": 1,
  "abi_version": 1,
  "permissions": 15,
  "memory_pages": 2,
  "budget": 100000,
  "sources": ["main.c"]
}
```

| 字段 | 约束 |
|---|---|
| `app_id` | 1..31 字节 ASCII `[a-z0-9._-]` |
| `title` | 1..63 字节严格 UTF-8，不含 NUL |
| `version` | 1..4294967295 的整数；同 ID 的不同包会直接替换旧包并保留存档，设备不要求递增，上架要递增 |
| `abi_version` | 当前只能为 1 |
| `permissions` | 位掩码。DRAW=1、INPUT=2、STORAGE=4、CLOCK=8、AUDIO=16、HAPTIC=32、MIC=64、SENSOR=128、SYSTEM=256、ACCESSORY=512。 |
| `min_api_level` | 可选。SDK 取此值和导入、权限要求的最高 Level，基线为 1。 |
| `graphics` | 可选。immediate、raster 或 framebuffer，默认 immediate。 |
| `memory_pages` | Level 1–3 为 1..16，Level 4 为 1..20。每页为 64 KiB。 |
| `budget` | 解释器每回调的指令预算。Level 1–3 为 1..100000，Level 4 为 1..125000。S31 AOT 使用时间守卫。 |
| `sources` | 1..64 个不重复的本地 `.c` 相对路径 |
| `assets` | 可选：资源文件的相对路径、按顺序转换的 PNG/WAV 列表，或 null |
| `defines` | 可选：最多 32 个 C 标识符到整数的映射，值为 -2147483648..4294967295 |

当前只支持section包格式1；应用`version`整数与平台版本独立。

拒绝未知字段、重复 JSON 键、用 bool/小数代替整数、越界值和逃出应用目录的路径。编译器按代码和 16 KiB 栈选择初始内存，最大内存受清单限制。完整包没有单独的单包上限，资源计入总长；`pack` 只做 16 MiB 格式检查，安装时以目标宿主 HELLO 公布的包库数据区为准（S31 V2 为 3960 KiB）。更新同一应用需要再有一段同样大小的连续空闲区，超过数据区一半的包需先卸载再安装。使用扩展配额时须在清单填写 `"min_api_level": 4`。宿主另行检查运行时总内存。

字符串形式的 `assets` 原样打包文件。列表形式按顺序拼接图像和声音，例如：

```json
{
  "assets": [
    "background.png",
    {"path": "art/hero.png", "format": "index8", "name": "player"},
    "audio/click.wav"
  ]
}
```

PNG 默认生成小端 RGB565（忽略透明度）。`index8` 输出逐像素索引，后接小端 RGB565 调色板。索引 0 表示透明，最多使用 255 个不透明颜色。以 0 为透明索引的调色板 PNG 保留原有索引顺序。其他 PNG 按像素首次出现的颜色分配索引，alpha 小于 128 的像素透明。PCM WAV 支持 8、16、24、32 位采样，混合声道并线性重采样到 16 kHz 单声道 PCM16。WAV 条目可设置 `"clip": "effect"` 去除首尾静音、截取至多一秒，并将末尾 10 ms 淡出。

`build` 会生成 `build/assets/<摘要>/resources.bin` 和 `assets_generated.h`。在 C 源码中使用 `#include <assets_generated.h>`，即可引用如 `ASSET_PLAYER_OFFSET` 和 `ASSET_PLAYER_LENGTH` 的字节偏移及长度。图像还提供 `WIDTH`、`HEIGHT`，索引图另提供 `PALETTE_OFFSET`、`PALETTE_COUNT`。修改素材或顺序后重新执行 `build`，再预览或打包。

`asset_read(offset,ptr,len)`在init/event中只读当前包资源，每次最多4096字节；尚无通用私有文件系统API。每个应用可保存 16 个 i32 KV 槽，Level 4 可保存 24 个槽，并新增最多 2048 字节的私有 blob。guest 不能依赖 WASI/libc、构造器或耗尽预算来让出执行；长任务需主动分片并成功返回。

当前 core 将普通 KV 保存尝试按 manager 全局五秒合并，正常停止强制刷新；`kv_set` 成功只表示 RAM 更新，突然断电可能丢失未持久化变化。故障回调不会获得 stop 补救机会。正常停止及重启是节流例外，应用仍应只保存有意义的变化。

当前固定的 core 支持 `draw_round_rect`、`draw_arc` 和单行 `draw_text_box`；文本框可选择 18/24/36/48 号字体与左/中/右对齐。旧 core 会拒绝尚不支持的导入，包信封通过不能证明图形兼容。普通 clear/rect/text 应用仍可使用原接口。

可选 `int32_t tinyrt_stop(void)` 用于正常离场前的有界保存；省略该定义的旧应用仍可构建。支持它的宿主在初始化成功后最多调用一次，失败或断电不保证调用；返回成功才提交变化的 KV，随后销毁实例。它不是 guest 主动退出接口。Zig 0.13 通过头文件的 Wasm `export_name` 属性导出已定义回调，Clang 同时使用 `--export-if-defined`。

## 示例和安装

- [Flappy Bird / 圆屏 Flappy Bird](examples/flappy/README.md)：466×466 满屏像素场景，触摸起跳、碰撞重试、最高分保存，短按 KEY1 返回大厅。使用经典精灵与音效；素材授权状态见示例 NOTICE。
- [Wave](examples/wave_sim/README.md)：圆屏二维粒子水面，支持 IMU 重力、点击掀浪、长按换色，短按 KEY1 返回大厅。
- [Pixel Quest](examples/pixel_quest/README.md)：原创横版平台跳跃，倾斜或按住左右侧移动、点按起跳，TILEMAP 关卡窗口加精灵，美术与音效由脚本原创生成。
- [Puff](examples/puff/README.md)：Level 3 吹气气球冒险，倾斜移动，含四个世界 16 关、首领战和无尽模式。

BLE 安装使用单独的 `tools/ble_install.py`，先查看 `--help`。它需要 Bleak；Windows 配对还需要对应 WinRT Python 包。设备配对和具体传输 profile 由产品配置提供，应用构建不依赖这些包。

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install examples/flappy/build/demo.sky-hop.trpkg
```

支持隔离诊断的宿主在 HELLO capability bit 3 标记能力：`quarantine` 列出损坏包，`uninstall --app-id demo.sky-hop` 解析当前目录完整身份后卸载，无需原包。`list` 只列健康项。重新 install 相同的原包可修复隔离项；最终 QUERY 仍必须确认完整身份，不能把 VERIFY_FAILED 当成成功。

支持分页的宿主在 HELLO capability bit 4 标记能力，最多安装 16 个应用。客户端使用 `LIST_PAGE=0x18`，每页最多三条 info72；目录在翻页期间变化时，从第一页重新读取，完整读取最多尝试三次。generation 是不透明的 64 位标记，客户端校验每页标记、总数、游标、规范身份和严格递增的 app ID；畸形回复立即失败。管理消息不超过 256 字节。

空间查询需要HELLO capability bit5（32）；详情查询需要bit6（64）的section元数据能力：

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF storage
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id demo.sky-hop
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info examples/flappy/build/demo.sky-hop.trpkg
```

`storage` 输出实际包字节、4 KiB 对齐占用、总空闲、最大连续空闲、健康和隔离总数及容量限制；总空闲不保证能暂存同样大小的连续更新。传输中返回 BUSY。generation 在 JSON 中是精确的 16 位十六进制字符串，表示 wire 小端 u64 的数值，来源与分页 token 相同。BSP RAM 有效位未设置时，六项 internal/external RAM 指标输出 null；runtime heap 的 limit/used/peak 独立有效，始终校验 used≤peak≤limit。

`info` 仅查询已验证健康应用。包路径使用该文件的完整身份和大小；`--app-id` 先解析健康目录的当前完整身份，再发 APP_INFO。返回 title、key_id、ABI、permissions、memory_pages、instruction_budget、wasm_size、assets_size 和完整身份，可供网站/手机对照应用目录。标题和 ID 本身不证明“官方”；来源由发布平台的 app_id 归属保证，设备不校验。客户端严格检查 STORAGE 92 字节与 APP_INFO 248 字节布局、schema/保留位、容量关系、身份匹配与 UTF-8，未知能力或畸形回复直接失败。Python 对应 API 为 `storage_info(link)` 和 `app_info(link, identity68_or_info72)`。

设备靠 BLE 配对和屏幕确认决定谁能安装。同一 app_id 的新包会替换旧包并沿用存档，所以社区发布时 app_id 归属由网站保证。

HELLO bit6（64）声明PACKAGE_SECTIONS能力。当前格式1安装要求此位；APP_INFO使用70字节schema1请求、248字节schema1回复。输出`package_format=1`、`selected_backend`、`fallback_reason`及各section大小；总长包含16字节表项、最小4字节对齐和256字节AOT元数据。

`python tools/ble_install.py --address AA:BB:CC:DD:EE:FF runtime`或`await runtime_info(link)`查询实际运行时profile：格式位图（格式1=1）、后端、目标、compat_id、选项摘要与WAMR提交。未启用AOT时全部AOT/profile字段为零；未知schema/位、尺寸矛盾与不完整profile会失败。字段见 [BLE合同](https://github.com/No-Chicken/tinyrt/blob/main/specs/package.md#ble-协商合同)。

Windows 仅在系统发出 PIN 请求后读取控制台，支持退格、Enter、Ctrl-C（取消）和 Ctrl-Z（EOF）。读取使用可取消的异步轮询；系统拒绝配对时会结束读取，不留下阻塞 `input()` 的后台线程。非交互终端必须提供 `--pin`。Python 自动验收可调用 `await windows_pair(address, pin_reader=reader)`，其中 `reader` 是无参数的异步函数，在收到请求后返回六位数字字符串；仍可用原有 `pin` 参数直接提供值。

安装成功、信封校验成功与实际应用行为通过是不同层次的证据。桌面预览只提供有界真实WAMR运行和画面审阅，实际设备行为另行验证。

## 验证

```powershell
# TINYRT_CC 让独立复制构建测试使用明确的工具链；未提供时该编译测试可跳过。
$env:TINYRT_CC = "path/to/zig.exe"
python -m unittest discover -s tests/sdk -v
python -m unittest discover -s tests/transport -v
python -m unittest discover -s tests/package -v
python tests/flappy/run_native.py --cc path/to/zig.exe
python tests/pixel_quest/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/flappy --cc path/to/zig.exe
```


源码仓库不包含构建缓存、应用包或本机工具链路径。产物保留在被忽略的 `build/` 中，发布时另作附件。

## 契约同步与来源固定

日常 new/build/pack/validate 使用随 SDK 发布的 `include/tinyrt.h` 和 `contracts/` 快照，无需核心仓库。维护者更新 ABI/包/协议契约时，显式指定核心根目录与完整 commit：

```powershell
python scripts/sync_contracts.py --core path/to/tinyrt --revision <40-character-core-commit>
python scripts/sync_contracts.py --core path/to/tinyrt --check
```

同步器验证该路径本身是 core Git 根目录、HEAD 与请求 revision 一致，四件权威契约在该提交中均为普通文件。生成内容直接读取固定 commit 的 Git blob，同时逐字节比对工作区源文件；即使设置 assume-unchanged 或 skip-worktree 也不能隐藏未提交修改。它记录 `contracts/source.json` 中的来源 revision，以及 `guest-v1.h`、`guest-gfx-v1.h`、`abi-v1.json`、`wire-v1.json` 的 SHA-256；生成的 `include/tinyrt.h` 和 `include/tinyrt_gfx.h` 必须与对应权威 guest 头逐字节相同。`--check` 校验 revision、源摘要、快照及生成头，不写文件。SDK 为这些文件固定 LF 换行，不受本机 autocrlf 设置影响。输出写前检查路径，并通过同目录临时文件与原子替换断开已有硬链接，避免改写 SDK 外共用同一文件内容的路径。

除显式更新 revision 外，普通同步也要求锁定的来源 hash 一致。同步方向始终为 core → SDK，核心构建不读取 SDK。

应用版本、ZIP 命名和兼容性注意事项见 [版本管理](specs/version-management.md)。

## Linux 和 macOS 桌面工具链

在与 SDK 配套的 core 仓库中，检出 SDK `contracts/source.json` 指定的 core revision，初始化 `third_party/wamr` 子模块，用 CMake、Ninja 和本机 C/C++ 编译器生成桌面 runner。macOS 构建需要 Xcode Command Line Tools 的系统头文件。

```sh
git submodule update --init third_party/wamr
python3 tools/toolchain/build.py --runner-only --work build-tools --jobs 4
export TINYRT_RUNNER="$PWD/build-tools/runner-build/tinyrt-run"
```

回到 SDK 根目录。Wasm 构建需要支持 wasm32 的 Zig 或 Clang。打包 S31 AOT 时，SDK 校验随包的 Windows x64 wamrc 的 SHA-256，在 POSIX 上经 PATH 中的 `wine` 或 `wine64` 执行。Wine 环境需支持 x86-64 PE 和 Microsoft Visual C++ v14 x64 运行库。S31 的目标选项、编译器哈希和 `compat_id` 与 Windows 发布包相同。当前 SDK 提供的发行二进制校验值覆盖 Windows x64。

```sh
python3 tools/tinyrt.py new demo --template game
python3 tools/tinyrt.py build demo --cc "$(command -v zig)"
python3 tools/tinyrt.py run demo
python3 tools/tinyrt.py pack demo
python3 tools/tinyrt.py validate demo/build/demo.trpkg --rebuild-aot
python3 tools/tinyrt.py release demo --cc "$(command -v zig)"
```

`--wasm-only` 可在没有 Wine 时生成解释器诊断包。S31 设备发布需要包含 AOT 的包。
