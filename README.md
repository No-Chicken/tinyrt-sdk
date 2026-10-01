# TinyRT SDK

独立开发、编译和签名 TinyRT ABI 1 的 C/Wasm 应用。普通应用构建不依赖 ESP-IDF、EEBadge、WAMR 或 TinyRT 源码；`include/tinyrt.h` 随 SDK 提供。权威 ABI、包格式与运行验证属于 TinyRT core。

## 快速开始

需要 Python 3.10+，以及可生成 wasm32 的 Clang 或 Zig 0.13.0。签名及信封检查需要 `cryptography>=43`。在自己的虚拟环境安装 `requirements.txt`；编译器用 `--cc` 显式指定，或设置 `TINYRT_CC`，无需全局安装。

```powershell
python -m pip install -r requirements.txt
python tools/tinyrt.py new path/to/my-app --app-id demo.my-app --title "My app"
python tools/tinyrt.py build path/to/my-app --cc path/to/zig.exe
python tools/tinyrt.py pack path/to/my-app --key path/to/application-signing.pem --key-id 100
python tools/tinyrt.py validate path/to/my-app/build/demo.my-app.trpkg --public-key path/to/application-public.sec1 --key-id 100
```

`new` 只创建尚不存在的目录。`build` 编译清单内的所有 C 源文件，导出三个 ABI 回调；产物默认位于应用的 `build/<app_id>.wasm`。`pack` 读取同一清单，默认使用该 Wasm 并写入 `build/<app_id>.trpkg`。`--output` 可指定产物路径；`pack --wasm` 可选择已有 Wasm。输出不会覆盖源文件、清单、声明资源或签名输入。`build --output` 必须使用 `.wasm`，`pack --output` 必须使用 `.trpkg`；如果目标已存在，还需匹配对应格式魔数，才能重复生成。后缀同时检查请求路径及解析后的链接目标，因此不能把普通头文件、文本或空文件当作输出；已损坏的产物需选择新路径。已有合法产物可以原路径重建。

**`validate` 只验证包信封。** 它检查规范头字段、策略、长度、载荷摘要、P-256 low-S 签名和显式信任 key ID；JSON 输出始终包含 `validation_level: envelope`、`wasm_validation: not_performed`。它不检查 Wasm 导入/导出、执行预算或应用行为。安装端仍必须调用 TinyRT core 的完整 Wasm 校验，运行测试另行执行。

公开测试密钥必须显式启用，不适合生产。测试设备也必须显式信任其公钥和 ID：

```powershell
python tools/tinyrt.py pack examples/pomodoro --development-key --key-id 1
python tools/tinyrt.py validate examples/pomodoro/build/demo.pomodoro.trpkg --development-key --key-id 1
```

底层 `tools/package.py` 保留原来的单独打包和 `development-public-key` 命令。签名工具不读取固件密钥，也不生成或替换生产密钥。

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

拒绝未知字段、重复 JSON 键、用 bool/小数代替整数、越界值和逃出应用目录的路径。编译器按代码和 16 KiB 栈选择初始内存，最大内存受清单限制。完整签名包最多 296 KiB（303104 字节），资源计入总长。

`assets` 只是包内经过签名的原始字节；ABI 1 尚无资源读取或通用文件系统 API。KV 是每应用 16 个 i32，不能把它描述为文件系统。guest 不能依赖 WASI/libc、构造器或耗尽预算来让出执行；长任务需主动分片并成功返回。

## 示例和安装

- [counter](examples/counter/README.md)：触摸加一，保存计数。
- [color](examples/color/README.md)：触摸切换颜色。
- [pomodoro](examples/pomodoro/README.md)：25m/5m 前台番茄钟与明确标记的 25s/5s 验证包。
- `examples/nes`：独立 N1 CPU/PPU 可行性试验；状态和边界以该目录说明为准。

BLE 安装使用单独的 `tools/ble_install.py`，先查看 `--help`。它需要 Bleak；Windows 配对还需要对应 WinRT Python 包。设备配对、信任集和具体传输 profile 由产品配置提供，应用构建不依赖这些包。

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install examples/pomodoro/build/demo.pomodoro.trpkg
```

Windows 仅在系统发出 PIN 请求后读取控制台，支持退格、Enter、Ctrl-C（取消）和 Ctrl-Z（EOF）。读取使用可取消的异步轮询；系统拒绝配对时会结束读取，不留下阻塞 `input()` 的后台线程。非交互终端必须提供 `--pin`。Python 自动验收可调用 `await windows_pair(address, pin_reader=reader)`，其中 `reader` 是无参数的异步函数，在收到请求后返回六位数字字符串；仍可用原有 `pin` 参数直接提供值。

安装成功、信封校验成功与实际应用行为通过是不同层次的证据。此工具不提供通用 SDK 模拟器。

## 验证

```powershell
# TINYRT_CC 让独立复制构建测试使用明确的工具链；未提供时该编译测试可跳过。
$env:TINYRT_CC = "path/to/zig.exe"
python -m unittest discover -s tests/sdk -v
python -m unittest discover -s tests/transport -v
python -m unittest discover -s tests/package -v
python tests/pomodoro/run_native.py --cc path/to/zig.exe
python tests/pomodoro/run_native.py --cc path/to/zig.exe --fast
python tools/tinyrt.py build examples/counter --cc path/to/zig.exe
python tools/tinyrt.py build examples/color --cc path/to/zig.exe
python tools/tinyrt.py build examples/pomodoro --cc path/to/zig.exe
python tools/tinyrt.py build examples/pomodoro/app-fast.json --cc path/to/zig.exe
```

SDK 测试把工具、头和模板复制到临时独立目录，然后实际 new/build/pack/validate；覆盖清单范围、签名损坏、单包上限和输入覆盖。番茄钟原生测试通过 ABI 回调驱动生产 guest。完整的 WAMR 运行使用同一组场景，见[番茄钟测试说明](tests/pomodoro/README.md)。只有可选运行测试需要明确的 TinyRT core 与其固定 WAMR checkout。

源码仓库不包含构建缓存、签名包或本机工具链路径。产物保留在被忽略的 `build/` 中，发布时另作附件。

## 契约同步与来源固定

日常 new/build/pack/validate 使用随 SDK 发布的 `include/tinyrt.h` 和 `contracts/` 快照，无需核心仓库。维护者更新 ABI/包/协议契约时，显式指定核心根目录与完整 commit：

```powershell
python scripts/sync_contracts.py --core path/to/tinyrt --revision <40-character-core-commit>
python scripts/sync_contracts.py --core path/to/tinyrt --check
```

同步器验证该路径本身是 core Git 根目录、HEAD 与请求 revision 一致，三件权威契约在该提交中均为普通文件。生成内容直接读取固定 commit 的 Git blob，同时逐字节比对工作区源文件；即使设置 assume-unchanged 或 skip-worktree 也不能隐藏未提交修改。它记录 `contracts/source.json` 中的来源 revision，以及 `guest-v1.h`、`abi-v1.json`、`wire-v1.json` 的 SHA-256；生成的 `include/tinyrt.h` 必须与权威 guest 头逐字节相同。`--check` 校验 revision、源摘要、快照及生成头，不写文件。SDK 为这些文件固定 LF 换行，不受本机 autocrlf 设置影响。输出写前检查路径，并通过同目录临时文件与原子替换断开已有硬链接，避免改写 SDK 外共用同一文件内容的路径。

尚未创建 core 首次提交时，可以显式使用 `--revision pending` 准备开发快照；此状态不会通过 `--check`，发布前必须换成实际 commit。除显式更新 revision 外，普通同步也要求锁定的来源 hash 一致。同步方向始终为 core → SDK，核心构建不读取 SDK。
