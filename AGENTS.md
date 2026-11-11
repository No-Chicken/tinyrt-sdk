# TinyRT SDK 开发约定

在设备应用大厅打开“添加”页后，运行 `python tools/tinyrt.py install <pkg>`。默认优先 USB，多台 USB 设备用 `--port COM11` 指定，也可用 `--transport ble --address <地址>` 选择蓝牙。

应用工程使用 `app.json` 声明版本、权限和资源。构建及发布命令见 [README](README.md)。设备以 Magie S31 为目标，交付包含 Wasm 与匹配 AOT 的包。

- 公开接口和签名以 `contracts/abi-v1.json` 与 `include/tinyrt.h` 为准，逐个接口的说明见 [API 参考](docs/api.md)。
- 应用版本由作者递增。最低固件由应用使用的 API Level 决定，见[兼容矩阵](specs/compat-matrix.md)。
- 打包和预览后在设备验证输入、退出和存档。板上运行核对 APP_INFO 的 selected_backend=2、fallback_reason=0。
- 发布 ZIP 的文件、授权说明和校验要求见[发布规范](specs/developer-publishing-v1.md)。
