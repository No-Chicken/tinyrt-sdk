# 应用版本与兼容性

应用发布版本唯一来源是 `app.json.version`：1..4294967295 的整数。Bird 当前 4，Wave 当前 7。签名 `.trpkg` 包头、`release.json.version`、ZIP 名称使用相同值；不维护 `display_version`，应用大厅不显示版本。

标准名称：`<app_id>-v<version>-<variant>.zip`。当前为 `demo.sky-hop-v4-wasm-aot.zip`、`demo.wave-sim-v7-wasm-aot.zip`。ZIP 是发布资料容器，设备安装的是其中 `package.trpkg`；改 ZIP 名称不会改变已签名应用版本。`wasm-aot` 表示包内含 Wasm 与 AOT，不代表设备实际选择了哪一个。

## 版本统计

| 层级 | 当前源码值 | 意义 |
| --- | --- | --- |
| 固件 VERSION | 0.0.4 | 宿主固件发布版本 |
| core VERSION | 0.0.3 | 运行时组件版本 |
| SDK VERSION | 0.0.4 | 工具/文档版本，本次统一整数 ZIP 命名 |
| Bird app.json.version | 4 | 应用发布/升级编号 |
| Wave app.json.version | 7 | 应用发布/升级编号 |
| Guest abi_version | 1 | Host import、回调、数据布局契约 |
| trpkg format | 1 | 应用包结构 |
| release.json schema_version | 2 | ZIP 发布清单结构 |
| 管理协议 | 1 | 安装与查询通信协议 |
| AOT format | 5 | 机器码格式；还需目标和 compat_id 匹配 |

这些层级独立演进，不要求数值相等。设备查询返回的是刷入固件构建时的模块版本，更新 SDK 源码不会改变已刷设备；本次没有重刷固件，设备的 SDK 字段仍为 0.0.3。

## 发布与兼容注意事项

- **身份一致**：ZIP 文件名、release.json 与签名包的 app_id/version 必须一致。安装以签名包为准，不能靠修改 JSON 或文件名更改版本。发布自检核对清单中的包大小、SHA-256 和签名。
- **升级递增**：同 app_id 的正常更新必须使用更大的整数。反复本地编译不自动加版本；一旦分发，不要以相同版本替换不同应用内容。当前归一化只复用已安装包字节，更新 ZIP 名称和文档，不重新编译或签署 Guest。
- **ABI 不匹配**：当前宿主只接受 abi_version=1。不匹配应在验证/安装阶段拒绝，不应尝试运行。ABI 相同也不保证支持所有新增可选 import/图形能力；应用要查询能力、提供兼容路径或声明真实要求，不能只看 SDK 版本。
- **包格式不匹配**：宿主不能解析未知 trpkg 格式，需匹配固件或重新打包。ZIP 清单 schema 不同属于发布工具/网站兼容问题，与 Guest ABI 不同。
- **AOT 不匹配**：架构、CPU、format、compat_id、受控工具链和签名授权都需匹配；S3 AOT 不能直接当作 S31 AOT。仅带 Wasm 且满足规则时可回退；同目标 AOT 校验拒绝不能静默回退。设备交付要求 APP_INFO selected_backend=2、fallback_reason=0。
- **权限与资源**：permissions、memory_pages、budget 和导入函数必须符合宿主限制。ABI 正确仍可能因内存不足、执行超限或资源/输入契约错误而运行失败。
- **签名授权**：公开开发钥只用于允许开发授权的匹配固件；包内元数据不能自行授予 AOT 运行权限。不要把开发 ZIP 当作受信量产发布。
- **追溯信息**：release.json 已记录包摘要、SDK Git 提交、工具链及构建报告；保留它们。SDK 提交只代表工具来源；本仓库应用源码也在 SDK 仓库中，发布要核对构建时工作区是否干净，避免将有未提交源码的构建声称为该提交的精确重现。旧包的构建信息不得改写成新版工具来源。
- **安装与运行分开验证**：打包/签名自检通过不等于真机运行、帧率、轴向和音效验收通过；保留原有 device verification 状态，不能因重命名 ZIP 就改成已验收。

本次正式发布目录中的整数命名 ZIP 对应设备现有包；此前语义版本命名的文件只作为历史交付，不再作为新版本命名规范。
