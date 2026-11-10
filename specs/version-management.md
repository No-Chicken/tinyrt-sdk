# 应用版本与兼容性

应用发布版本唯一来源是 `app.json.version`：1..4294967295 的整数。`.trpkg` 包头、`release.json.version`、ZIP 名称使用相同值；不维护 `display_version`，应用大厅不显示版本。

标准名称：`<app_id>-v<version>-wasm-aot.zip`。ZIP 是发布资料容器，设备安装的是其中 `package.trpkg`；改 ZIP 名称不会改变应用版本。`wasm-aot` 表示包内含 Wasm 与 AOT，不代表设备实际选择了哪一个。

Guest ABI 和 API Level 遵循 core 的 [版本规范](https://github.com/No-Chicken/tinyrt/blob/main/specs/versioning.md)。各 Level 的首个支持固件见[兼容矩阵](compat-matrix.md)。

## 发布与兼容注意事项

- **身份一致**：ZIP 文件名、release.json 与应用包的 app_id/version 必须一致。安装以应用包为准，不能靠修改 JSON 或文件名更改版本。发布自检核对清单中的包大小、SHA-256 和签名。
- **覆盖安装**：设备不强制版本递增，同 app_id 的不同包（同版本重编译、升级或降级）都会替换旧包并保留存档；完全相同的包返回已安装。上架更新仍要递增，网站靠它做更新检测。
- **ABI 不匹配**：当前宿主只接受 abi_version=1。不匹配应在验证/安装阶段拒绝，不应尝试运行。ABI 相同也不保证支持所有新增可选 import/图形能力；应用要查询能力、提供兼容路径或声明真实要求，不能只看 SDK 版本。
- **包格式不匹配**：宿主不能解析未知 trpkg 格式，需匹配固件或重新打包。ZIP 清单 schema 不同属于发布工具/网站兼容问题，与 Guest ABI 不同。
- **AOT 兼容性**：Magie S31 执行与其架构、CPU、格式和 compat_id 匹配的 AOT。安装运行后核对 APP_INFO 中的 selected_backend=2 和 fallback_reason=0。
- **权限与资源**：permissions、memory_pages、budget 和导入函数必须符合宿主限制。ABI 正确仍可能因内存不足、执行超限或资源/输入契约错误而运行失败。
- **追溯信息**：release.json 已记录包摘要、SDK Git 提交、工具链及构建报告；保留它们。SDK 提交只代表工具来源；本仓库应用源码也在 SDK 仓库中，发布要核对构建时工作区是否干净，避免将有未提交源码的构建声称为该提交的精确重现。旧包的构建信息不得改写成新版工具来源。
- **安装与运行分开验证**：打包/签名自检通过不等于真机运行、帧率、轴向和音效验收通过；保留原有 device verification 状态，不能因重命名 ZIP 就改成已验收。
