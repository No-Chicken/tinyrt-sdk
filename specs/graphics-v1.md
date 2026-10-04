# Graphics API · SDK 0.0.2

ABI 和签名包版本仍为 1。生成的 `include/tinyrt_gfx.h` 来自 core 的
`contracts/guest-gfx-v1.h`；通过 `scripts/sync_contracts.py` 同步，禁止手工修改。
`VERSION` 表示 SDK 显示版本；应用 `version` 是设备升级使用的独立整数，构建不自动递增。

## 能力与兼容

`gfx_caps()` 返回功能位；使用新 import 的包需要支持这些 import 的宿主。
旧固件在安装预检中拒绝未知 import，需要先升级固件。
能力查询不能让新包自动运行于缺少 `gfx_caps` 的旧固件；需要旧固件时，发布保留旧 import 的独立构建。
SDK 在构建、开发 AOT 签名与受控发布签名前拒绝不认识的 `gfx_*` / `fb_*` import。
`app.json` 可选 `graphics` 只能是 `immediate`、`raster`、`framebuffer`。
该字段用于构建检查，不添加签名包字段；实际要求来自签名保护的 Wasm import。

## 帧与资源

`gfx_begin(0)` 开始完整帧；`gfx_submit(records,length)` 提交一批记录；
`gfx_end()` 完成。默认清黑；`TINYRT_GFX_KEEP_PREVIOUS` 显式保留上一帧。
普通旧绘制帧仍须以 `draw_clear` 开始；无变化仅调用 `draw_skip`。
批次最大 16384 字节，帧最大 65536 字节、4096 条记录。
头部为小端 `u16 op,u16 size`，size 包含头部并对齐 4 字节。
坐标、尺寸与其他字段都是 32 位；内联数据尾部以零对齐。
宿主整体校验后才采用提交内容；未知 op 和非法资源都拒绝。

纹理/调色板上传只在 init/event 调用；render 仅绘制。
纹理 32 槽、调色板 16 槽，纹理总容量 256 KiB；INDEX8 的索引 0 可透明。
`gfx_tex_upload` 可用 `TINYRT_GFX_FROM_ASSET` 直接引用签名资源偏移，宿主复制并拥有资源。
队列帧保持自己的资源版本；之后更新资源不会改变已提交帧。

GRID 为 40 字节头部加 `cols*rows` 个索引：每格画
`(cell_w-gap)×(cell_h-gap)`，索引 0 跳过。
SPRITE 从驻留纹理裁剪源矩形，最近邻缩放，可水平/垂直翻转。
所有具体结构、能力位及 import 参数以生成头文件为准。

## 示例与验证

Wave 正式 20×20 配置提交 400 字节网格加 CLEAR 与方向环矩形，共 1484 字节；40×40 比较配置为 1600/2684 字节；
五主题、36 方向与旧 RGB565 完整画布逐像素比较。
Bird 用原有 RGB565 色值生成无损 INDEX8 驻留图集；
准备、飞行和结算共 108 个场景与旧画布比较。
实际 core 条带渲染器与旧 import 构建的 Bird 桌面脚本截图在四个采样时点逐字节相同。
两者有能力降级路径：缺少所需能力时，初始化才扩展 Guest 内存用于旧 108578 字节图像，
保留旧 `draw_clear` 语义。支持能力时不分配该图像。
桌面结果验证契约、输入和像素，不等于设备帧率；S31 硬件仍待单独验证。
