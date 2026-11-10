# 应用性能测量

桌面 runner 用解释器验证回调、像素和输入。Magie 用 S31 AOT 运行同一应用。真机计时前可用 `python tools/ble_install.py --address <地址> info --app-id <应用ID>` 查询 APP_INFO，核对 selected_backend=2 和 fallback_reason=0。

## 帧耗时

在前后两次事件取 now_ms 的差值，按固定窗口汇总后调用 debug_log，每秒输出一次即可。在开发固件上用 `python tools/tinyrt.py logs --port COM11` 查看 app 标签。分别比较更新逻辑、绘制和实际显示帧率。记录固件构建、应用包摘要及输入序列。

## 绘制与数值

- 首帧完整绘制。以后用 SPRITE_BATCH 提交共享纹理的精灵，用 gfx_damage 标记旧、新覆盖范围的并集。静止帧可用 draw_skip。
- 不保留宿主帧指针，不在 Guest 内操作 DMA。单帧命令、提交字节数与资源限额见 ABI 合同。
- S31 目标启用单精度浮点扩展，double 使用软件路径。SDK 的 platform_probe 在 S31 AOT 上检查了 float 和 double 各 512 次累加的正确性，组合循环短于一次毫秒计时分辨率。具体游戏应测量自己的工作负载。可用 tinyrt_libc.h 的 Q16.16 和查表正弦、余弦替代热点计算。
