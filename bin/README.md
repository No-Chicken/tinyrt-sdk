# Windows x64 桌面预览

`tinyrt-run.exe` 在本机解释执行 Guest Wasm，静态链接 Microsoft C 运行库。SDK 按 `tools/toolchains/desktop-windows-x64.json` 核验大小与 SHA-256。固件执行 S31 AOT，性能请在真机测量。

构建入口为 core 的 `tools/toolchain/build.py --runner-only --work build-tools`。Linux 和 macOS 源码构建后将输出路径传给 SDK 的 `--runner` 或 `TINYRT_RUNNER`。构建步骤见 [SDK README](../README.md)。

WAMR 许可证见 [WAMR-LICENSE.txt](../tools/toolchains/esp32s31/windows-x64/licenses/WAMR-LICENSE.txt)，第三方声明见 [WAMR-ATTRIBUTIONS.md](WAMR-ATTRIBUTIONS.md)。
