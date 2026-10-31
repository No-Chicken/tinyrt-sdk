# ESP32-S31 AOT 编译器（Windows x64）

此目录提供 SDK 固定的 S31 `wamrc.exe`。开发者和 AI 编译 S31 应用时统一使用本目录文件，显式传入 `--wamrc`，不要引用个人机器的 `tmp/` 构建目录。工具在 Windows x64 上运行，输出 ESP32-S31 使用的 RISC-V AOT；不是 ESP-IDF 固件 C 编译器。

## 文件与核验

- `wamrc.exe`：实际编译器；身份、目标、选项及兼容配置以 [esp32s31.json](../../esp32s31.json) 为准。
- `provenance.json`：原始构建来源记录，包含源码提交、补丁、构建命令及许可证摘要。保留原始字节；其中绝对路径描述原构建环境，使用者无需具备这些路径。
- `licenses/`：随编译器分发的原始许可证与版权声明，路径和摘要对应 provenance 的 `licenses` 数组。

从 SDK 根目录核验：

```powershell
$pin = Get-Content tools/toolchains/esp32s31.json -Raw | ConvertFrom-Json
$compiler = 'tools/toolchains/esp32s31/windows-x64/wamrc.exe'
if ((Get-FileHash $compiler -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.compiler.sha256) { throw 'Compiler SHA-256 mismatch' }
if ((Get-Item $compiler).Length -ne $pin.compiler.size) { throw 'Compiler size mismatch' }
```

SDK 打包时也会强制核验编译器 SHA-256。版本号相同不代表文件相同；自行重编译的文件不能直接替换这份固定工具。此工具需要 Microsoft Visual C++ v14 x64 运行库，要求见 [SDK README](../../../../README.md)。

## 构建 S31 应用包

从 SDK 根目录执行（先准备可生成 wasm32 的 Zig/Clang）：

```powershell
python tools/tinyrt.py build examples/flappy --cc path/to/zig.exe
python tools/tinyrt.py pack examples/flappy
```

使用 EEBadge 固件的应用入口时，从 `esp32/` 目录执行：

```powershell
$env:TINYRT_WAMRC_S31 = (Resolve-Path tinyrt-sdk/tools/toolchains/esp32s31/windows-x64/wamrc.exe).Path
python tools/cli/app.py build tinyrt-sdk/examples/flappy --target s31
```

生成 S31 网站 ZIP 用 `python tools/tinyrt.py release <app> --target esp32s31`，未传 `--wamrc` 时默认使用本目录的编译器。S31 包与网站交付流程的范围见 [设备目标](../../../../specs/device-targets.md)。

## 网站对接

`package.trpkg` 的 AOT 元数据记录 `compiler.sha256`，该值是 EXE 的摘要，不是 ZIP 或应用包的摘要。网站应审查并登记 [esp32s31.json](../../esp32s31.json) 对应的编译器身份、目标、AOT 格式、兼容 ID 和参数摘要，并与固件支持范围核对。只校验上传包的网站无需运行此 Windows 编译器。公开开发钥签名不证明发布者身份，登记工具链也不替代包签名及其他发布校验。
