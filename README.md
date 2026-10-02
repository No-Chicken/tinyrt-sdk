# TinyRT SDK 0.1.0

独立开发、编译、预览和签名 TinyRT ABI 1 的 C/Wasm 应用。当前是开发预览：包格式、store 与 BLE schema 都为 1，公开开发钥仅用于开发固件。普通 C/Wasm 构建不依赖 ESP-IDF；桌面预览使用真实 WAMR runner，开发 AOT 使用固定目标与来源的编译器。权威契约与运行验证属于 TinyRT core。

## 开发者：创建、预览、打包

Windows x64 的已打包 `wamrc.exe` 还需要 Microsoft Visual C++ v14 x64 运行库（14.51或以上）；可安装[微软最新运行库](https://learn.microsoft.com/zh-CN/cpp/windows/latest-supported-vc-redist)。已打包桌面 runner 使用静态CRT。

需要 Python 3.10+，以及可生成 wasm32 的 Clang 或 Zig 0.13.0。安装 `requirements.txt`（签名用 cryptography，PNG 用 Pillow）；用 `--cc` 或 `TINYRT_CC` 指定编译器。AI 与人都可以从 [AGENTS.md](AGENTS.md) 开始，它包含全部 ABI 函数、回调边界和完整小游戏模板。

```powershell
python -m pip install -r requirements.txt
python tools/tinyrt.py new my-game --app-id demo.my-game --title "My game" --template game
python tools/tinyrt.py build my-game --cc path/to/zig.exe
python tools/tinyrt.py run my-game --runner path/to/tinyrt-run.exe --frames 0,330
python tools/tinyrt.py pack my-game --aot --development-key --wamrc path/to/wamrc.exe
python tools/tinyrt.py validate my-game/build/demo.my-game.trpkg --development-key
```

`new` 创建尚不存在的目录；默认模板为 `minimal`，`game` 提供触摸、时钟、帧缓冲与缩放。`build` 编译清单中的 C 文件，产物为 `build/<app_id>.wasm`；`pack` 默认读取此文件并写入 `build/<app_id>.trpkg`。`--output` 自定义输出，`pack --wasm` 选择既有 Wasm。输出后缀、魔数和链接目标受检查，不能覆盖源文件、清单、资源或签名输入；损坏产物需选择新路径。

`run --events events.json` 按时间驱动真实 WAMR 解释器，输入格式见 [AGENTS.md](AGENTS.md)。`--frames` 是0..60000 ms的截图时间列表，最多120张；默认尊重Guest的`clock_interval`，按虚拟deadline执行CLOCK；截图与输入时点不会额外触发CLOCK或render。回调墙钟耗时不推进虚拟时钟。`--step-ms`仅在显式传入时强制按固定间隔注入CLOCK，覆盖Guest请求的周期，用于诊断。默认输出 `build/preview/` 下的466×466圆形PNG和`report.json`。报告含回调耗时与heap_bytes；桌面耗时不是设备FPS，字体与设备也不完全一致。runner依次取`--runner`、`TINYRT_RUNNER`、SDK `bin/tinyrt-run.exe`或Windows x64固定下载来源；发布二进制按`desktop-windows-x64.json`校验SHA-256和大小。该v0.1.0下载位置尚待发布，当前使用显式本地runner。输出只更新当前工具标记的PNG和report，未知已有文件、输入及链接目标会被拒绝；旧版未标记预览请用新的`--output`目录。RGB565缩放使用与Host一致的floor最近邻采样。源码用户可以按 [core构建说明](https://github.com/No-Chicken/tinyrt) 构建runner后显式传入。

`pack --aot --development-key` 限定非空`demo.`子命名空间与key_id=1。它用SDK pin校验编译器SHA-256，固定ESP32-S3目标与bounds/stack/loop-poll检查，自行生成AOT，打入格式1包并保留Wasm回退。通用入口不接受上传者提供的AOT或自报安全参数。`--wamrc`也必须匹配固定摘要。当前`tools/toolchains/esp32s3.json`内的v0.1.0附件URL是待发布位置，尚不能声称可下载；现阶段需使用匹配的本地编译器。自动下载代码只支持Windows x64，其他系统须显式提供匹配工具。

不加`--aot`时生成Wasm包，可用`--key application-signing.pem --key-id 100`签名。公开开发钥是公开P-256标量1；任何人都能签名，不能证明作者身份，不适合量产。设备还必须显式配置信任。底层`tools/package.py`提供单独打包与`development-public-key`，不读取或生成生产/固件密钥。

**`validate`仅认证包信封。** 它验证规范字段、section布局、策略、长度、摘要、low-S P-256签名与key ID；输出含`validation_level: envelope`、`wasm_validation: not_performed`、`aot_validation: not_performed`。设备必须再运行core结构/ABI/目标/兼容性与执行保护校验。签名元数据是声明，不能单独证明机器码安全。

## 用户：安装别人分享的包

安装匹配的开发固件（启用`TINYRT_ALLOW_DEVELOPMENT_KEY=ON`并给予开发钥DEVELOPMENT授权），打开设备应用大厅的限时BLE安装入口。固件准备方式由产品提供；本工具只安装应用包。电脑准备依赖后扫描、配对、安装：

```powershell
python -m pip install bleak
# Windows显式配对还需：
python -m pip install winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Enumeration
python tools/ble_install.py --scan
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install my-game/build/demo.my-game.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id demo.my-game
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF launch my-game/build/demo.my-game.trpkg
```

按设备显示PIN配对；自动化终端用`--pin`提供六位PIN。完整包身份的最终QUERY确认安装结果。`info`中的`selected_backend=2`表示AOT，`1`表示Wasm；带AOT包选择Wasm时结合`fallback_reason`显示“兼容模式，性能降低”。手机安装入口尚未完成时使用电脑流程。开发包分享应同时说明固件目标和公开开发钥要求。

## 受信 AOT 发布

发布操作方使用独立受控入口：

```powershell
python tools/release_compile.py --manifest path/to/app/app.json --wasm path/to/app/build/app.wasm --source-lock path/to/toolchain/source-lock.json --provenance path/to/compiler/provenance.json --release-profile path/to/release-profile.json --runtime-patch path/to/runtime/0001.patch --runtime-patch path/to/runtime/0002.patch --key path/to/release-signing.pem --output path/to/app/build/app.trpkg
```

source-lock、provenance、release profile、按序runtime补丁和签名钥由发布方控制。工具固定编译器及选项，重新核查输入、补丁和编译器摘要后签名；默认包含原始Wasm，`--without-wasm`可省略回退。来源绑定、兼容ID算法、AOT授权与发布钥轮换见 [包规格](specs/package.md)。发布密钥和签名服务由部署方管理。正式固件只信任其受控发布钥的AOT。

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
| `version` | 1..4294967295 的整数；同 ID 更新必须递增 |
| `abi_version` | 当前只能为 1 |
| `permissions` | 0..15；draw=1、input=2、storage=4、clock=8 |
| `memory_pages` | 1..16，页面为 64 KiB；这是 Wasm 线性内存上限 |
| `budget` | 每回调 1..100000 条受计量指令 |
| `sources` | 1..64 个不重复的本地 `.c` 相对路径 |
| `assets` | 可选：应用目录内现有文件的相对路径，或 null |
| `defines` | 可选：最多 32 个 C 标识符到整数的映射，值为 -2147483648..4294967295 |

当前只支持section包格式1，无`--format`参数；旧内部布局需要重建，应用`version`整数与平台版本独立。

拒绝未知字段、重复 JSON 键、用 bool/小数代替整数、越界值和逃出应用目录的路径。编译器按代码和 16 KiB 栈选择初始内存，最大内存受清单限制。完整签名包最多 2 MiB（2097152 字节），资源计入总长；安装时还会检查目标宿主 HELLO 公布的实际包上限，旧宿主仍可拒绝超出其容量的包。Flash 容量增加不改变 ABI 1 的 16 页线性内存与每回调 100000 指令限额；较大的 Wasm 仍需通过宿主运行时的独立内存检查。

`assets` 指定包内经过签名的原始字节。`asset_read(offset,ptr,len)`在init/event中只读当前包资源，每次最多4096字节；尚无通用私有文件系统API。KV 是每应用 16 个 i32，不能把它描述为文件系统。guest 不能依赖 WASI/libc、构造器或耗尽预算来让出执行；长任务需主动分片并成功返回。

当前 core 将普通 KV 保存尝试按 manager 全局五秒合并，正常停止强制刷新；`kv_set` 成功只表示 RAM 更新，突然断电可能丢失未持久化变化。故障回调不会获得 stop 补救机会。正常停止及重启是节流例外，应用仍应只保存有意义的变化。

当前固定的 core 支持 `draw_round_rect`、`draw_arc` 和单行 `draw_text_box`；文本框可选择 18/24/36/48 号字体与左/中/右对齐。旧 core 会拒绝尚不支持的导入，包信封通过不能证明图形兼容。普通 clear/rect/text 应用仍可使用原接口。

可选 `int32_t tinyrt_stop(void)` 用于正常离场前的有界保存；省略该定义的旧应用仍可构建。支持它的宿主在初始化成功后最多调用一次，失败或断电不保证调用；返回成功才提交变化的 KV，随后销毁实例。它不是 guest 主动退出接口。Zig 0.13 通过头文件的 Wasm `export_name` 属性导出已定义回调，Clang 同时使用 `--export-if-defined`。

## 示例和安装

- [counter](examples/counter/README.md)：触摸加一，保存计数。
- [color](examples/color/README.md)：触摸切换颜色。
- [pomodoro](examples/pomodoro/README.md)：25m/5m 前台番茄钟与明确标记的 25s/5s 验证包。
- [snake](examples/snake/README.md)：圆屏可玩贪吃蛇，触控方向键、暂停/继续、最高分存档和满棋盘胜利。
- [nes-maze](examples/nes-maze/README.md)：原创迷宫ROM与6502/PPU，已验证的设备性能见产品记录。
- [nes-player / SkyTrail](examples/nes-player/README.md)：从签名resources加载mapper0 ROM，默认原创MIT卷轴游戏含移动、跳跃、敌人、金币和重置；完整按键、KEY1=A与两档圆屏显示。指南包含专用构建、预览、开发AOT安装及原生检查；设备性能实测待完成。
- `examples/nes`：N1 CPU/PPU 诊断样例，固定自制 ROM 与输出 oracle 用于验证执行边界；不属于可玩 NES 模拟器。状态和限制以该目录说明为准。

BLE 安装使用单独的 `tools/ble_install.py`，先查看 `--help`。它需要 Bleak；Windows 配对还需要对应 WinRT Python 包。设备配对、信任集和具体传输 profile 由产品配置提供，应用构建不依赖这些包。

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install examples/pomodoro/build/demo.pomodoro.trpkg
```

支持隔离诊断的宿主在 HELLO capability bit 3 标记能力：`quarantine` 列出损坏包，`uninstall --app-id demo.snake` 解析当前目录完整身份后卸载，无需原包。`list` 只列健康项。重新 install 相同原始签名包可修复隔离项；最终 QUERY 仍必须确认完整身份，不能把 VERIFY_FAILED 当成成功。旧宿主不提供隔离查询时可继续正常安装及按 ID 解析健康项。

支持分页的宿主在 HELLO capability bit 4 标记能力，最多安装 16 个应用。客户端使用 `LIST_PAGE=0x18`，每页最多三条 info72；目录在翻页期间变化时，从第一页重新读取，完整读取最多尝试三次。generation 是不透明的 64 位标记，客户端校验每页标记、总数、游标、规范身份和严格递增的 app ID；畸形回复立即失败。管理消息仍不超过 256 字节。未声明分页能力的旧宿主继续使用最多两条记录的 LIST/LIST_QUARANTINED。

空间查询需要HELLO capability bit5（32）；详情查询需要bit6（64）的section元数据能力：

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF storage
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id demo.snake
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info examples/snake/build/demo.snake.trpkg
```

`storage` 输出实际包字节、4 KiB 对齐占用、总空闲、最大连续空闲、健康和隔离总数及容量限制；总空闲不保证能暂存同样大小的连续更新。传输中返回 BUSY。generation 在 JSON 中是精确的 16 位十六进制字符串，表示 wire 小端 u64 的数值，来源与分页 token 相同。BSP RAM 有效位未设置时，六项 internal/external RAM 指标输出 null；runtime heap 的 limit/used/peak 独立有效，始终校验 used≤peak≤limit。

`info` 仅查询已验证健康应用。包路径使用该文件的完整身份和大小；`--app-id` 先解析健康目录的当前完整身份，再发 APP_INFO。返回 title、key_id、ABI、permissions、memory_pages、instruction_budget、wasm_size、assets_size 和完整身份，可供网站/手机对照应用目录。标题和 ID 本身不证明“官方”；应结合完整 SHA-256 与受信发布者 key_id。客户端严格检查 STORAGE 92 字节与 APP_INFO 248 字节布局、schema/保留位、容量关系、身份匹配与 UTF-8，未知能力或畸形回复直接失败。Python 对应 API 为 `storage_info(link)` 和 `app_info(link, identity68_or_info72)`；产品 TypeScript 协议层提供 `storageInfo` / `appInfo`，本轮未增加查询 UI。

信任策略由宿主配置：签名钥可限定 app ID 或末尾点命名空间。开发者应使用所属范围内的 ID；签名合法但越权的包仍被拒绝。公开开发钥只用于明确开启该信任的试验固件。

HELLO bit6（64）声明PACKAGE_SECTIONS能力。当前格式1安装要求此位；APP_INFO只使用70字节schema1请求、248字节schema1回复，旧68/164字节布局已移除。输出`package_format=1`、`selected_backend`、`fallback_reason`及各section大小；总长包含16字节表项、最小4字节对齐和256字节AOT元数据。

`python tools/ble_install.py --address AA:BB:CC:DD:EE:FF runtime`或`await runtime_info(link)`查询实际运行时profile：格式位图（格式1=1）、后端、开发AOT开关、目标、compat_id、选项摘要与WAMR提交。未启用AOT时全部AOT/profile字段为零；未知schema/位、尺寸矛盾与不完整profile会失败。字段见 [BLE合同](specs/package.md#ble-协商合同)。

Windows 仅在系统发出 PIN 请求后读取控制台，支持退格、Enter、Ctrl-C（取消）和 Ctrl-Z（EOF）。读取使用可取消的异步轮询；系统拒绝配对时会结束读取，不留下阻塞 `input()` 的后台线程。非交互终端必须提供 `--pin`。Python 自动验收可调用 `await windows_pair(address, pin_reader=reader)`，其中 `reader` 是无参数的异步函数，在收到请求后返回六位数字字符串；仍可用原有 `pin` 参数直接提供值。

安装成功、信封校验成功与实际应用行为通过是不同层次的证据。桌面预览只提供有界真实WAMR运行和画面审阅，实际设备行为另行验证。

## 验证

```powershell
# TINYRT_CC 让独立复制构建测试使用明确的工具链；未提供时该编译测试可跳过。
$env:TINYRT_CC = "path/to/zig.exe"
python -m unittest discover -s tests/sdk -v
python -m unittest discover -s tests/transport -v
python -m unittest discover -s tests/package -v
python tests/pomodoro/run_native.py --cc path/to/zig.exe
python tests/pomodoro/run_native.py --cc path/to/zig.exe --fast
python tests/snake/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/counter --cc path/to/zig.exe
python tools/tinyrt.py build examples/color --cc path/to/zig.exe
python tools/tinyrt.py build examples/pomodoro --cc path/to/zig.exe
python tools/tinyrt.py build examples/pomodoro/app-fast.json --cc path/to/zig.exe
python tools/tinyrt.py build examples/snake --cc path/to/zig.exe
```

SDK 测试把工具、头和模板复制到临时独立目录，然后实际 new/build/pack/validate；覆盖清单范围、签名损坏、单包上限和输入覆盖。番茄钟和贪吃蛇原生测试通过 ABI 回调驱动生产 guest。完整的 WAMR 运行使用同一组场景，见[番茄钟测试说明](tests/pomodoro/README.md)和[贪吃蛇测试说明](tests/snake/README.md)。只有可选运行测试需要明确的 TinyRT core 与其固定 WAMR checkout。

源码仓库不包含构建缓存、签名包或本机工具链路径。产物保留在被忽略的 `build/` 中，发布时另作附件。

## 契约同步与来源固定

日常 new/build/pack/validate 使用随 SDK 发布的 `include/tinyrt.h` 和 `contracts/` 快照，无需核心仓库。维护者更新 ABI/包/协议契约时，显式指定核心根目录与完整 commit：

```powershell
python scripts/sync_contracts.py --core path/to/tinyrt --revision <40-character-core-commit>
python scripts/sync_contracts.py --core path/to/tinyrt --check
```

同步器验证该路径本身是 core Git 根目录、HEAD 与请求 revision 一致，三件权威契约在该提交中均为普通文件。生成内容直接读取固定 commit 的 Git blob，同时逐字节比对工作区源文件；即使设置 assume-unchanged 或 skip-worktree 也不能隐藏未提交修改。它记录 `contracts/source.json` 中的来源 revision，以及 `guest-v1.h`、`abi-v1.json`、`wire-v1.json` 的 SHA-256；生成的 `include/tinyrt.h` 必须与权威 guest 头逐字节相同。`--check` 校验 revision、源摘要、快照及生成头，不写文件。SDK 为这些文件固定 LF 换行，不受本机 autocrlf 设置影响。输出写前检查路径，并通过同目录临时文件与原子替换断开已有硬链接，避免改写 SDK 外共用同一文件内容的路径。

尚未创建 core 首次提交时，可以显式使用 `--revision pending` 准备开发快照；此状态不会通过 `--check`，发布前必须换成实际 commit。除显式更新 revision 外，普通同步也要求锁定的来源 hash 一致。同步方向始终为 core → SDK，核心构建不读取 SDK。
