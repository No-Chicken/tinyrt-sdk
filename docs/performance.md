# 应用性能测量

桌面 runner 用解释器验证回调、像素和输入。Magie 用 S31 AOT 运行同一应用。真机计时前可用 `python tools/ble_install.py --address <地址> info --app-id <应用ID>` 查询 APP_INFO，核对 selected_backend=2 和 fallback_reason=0。

## 帧耗时

联调固件时，在专用开发构建中启用现有的 `CONFIG_TINYRT_PERFORMANCE`。从固件 `esp32/` 目录执行 `python tools/cli/eedb.py --port COM11 logcat --seconds 30`，或用串口终端读取统计：

| 标签 | 用途 |
|---|---|
| `APP_GUEST` | Guest 事件与 render 回调耗时、调用次数 |
| `APP_PERF` | 提交、获取与覆盖帧数，以及宿主绘制耗时分布 |
| `PANEL_PERF` | 面板完成刷新次数、传输字节与错误 |

按各自窗口时长计算频率，分别比较逻辑、绘制和面板完成帧率；Guest 提交频率不能代替显示频率。记录固件构建、应用包摘要及输入序列。测量完成后恢复默认关闭统计的构建配置。

应用内部的局部计时可使用 `now_ms` 和 `debug_log`，通过 `python tools/tinyrt.py logs --port COM11` 读取；该命令只输出应用日志，不包含上述宿主统计。`debug_log` 要求 API Level 2。性能信息使用开发串口，不加入应用画面。

## 绘制与数值

- 首帧完整绘制。以后用 SPRITE_BATCH 提交共享纹理的精灵，用 gfx_damage 标记旧、新覆盖范围的并集。静止帧可用 draw_skip。
- 不保留宿主帧指针，不在 Guest 内操作 DMA。单帧命令、提交字节数与资源限额见 ABI 合同。
- 运动输入只更新控制状态，按动画时钟提交新画面；无新画面时使用 `draw_skip`。大圆弧和透明遮罩先做真机测量，热点可改为预计算边界和小图元，避免每帧大范围逐像素角度计算。
- S31 目标启用单精度浮点扩展，double 使用软件路径。float 和 double 在 S31 AOT 上结果正确，但 double 较慢。具体游戏应测量自己的工作负载。可用 tinyrt_libc.h 的 Q16.16 和查表正弦、余弦替代热点计算。
