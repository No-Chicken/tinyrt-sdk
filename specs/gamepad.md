# ABI 1 外设手柄事件扩展

`TINYRT_EV_GAMEPAD=8`，订阅位 `TINYRT_IN_GAMEPAD=0x100`。
调用 `input_events(old_mask | TINYRT_IN_GAMEPAD)`，可与运动位 0x80 同时附加；合法掩码为
0、56、64、120 各自或上 0、128、256、384，仅 init 且需要 INPUT 权限。

事件参数，每个事件都是完整状态：

| 参数 | 内容 |
|---|---|
| `x` | 按键位图：bit0..15 依次为 A B X Y LB RB LT RT SELECT START L3 R3 UP DOWN LEFT RIGHT，bit24..31 为 FN1..FN8；bit16..23 恒为 0 |
| `y` | `LX | LY<<8 | RX<<16 | RY<<24`，各为 int8（-127..127），+X 向右、+Y 向下 |
| `arg` | `LT | RT<<8 | flags<<16`，扳机 0..255；flags bit0 为 CONNECTED |

按键按位置定义（W3C Standard Gamepad）：A 下、B 右、X 左、Y 上。宏见 `tinyrt.h` 的 `TINYRT_PAD_*`。

宿主行为：

- 按键变化保序送达，摇杆变化合并；队列满时只会丢中间的短按，最后一个状态总是真实的。
- 外设 HOME 键由宿主保留，用于返回大厅，永远不会出现在 `x` 里。
- 订阅时外设已连接，立即补发一次当前状态；拔掉外设时投递一次全部松开且 CONNECTED=0。
- 切换应用时仍按住的键在松开前不会出现，避免把用来启动应用的那一下当成游戏输入。

**兼容性**：旧运行时拒绝 0x100 并终止实例，Guest 无法通过重新订阅降级。订阅手柄的应用需要更新宿主。
不新增 import、不改变包格式和 BLE 布局。外设与吧唧之间的线协议见固件仓 `components/acc_proto/PROTOCOL.md`。

桌面 runner 接受 `gamepad <buttons> <lx> <ly> <rx> <ry> <lt> <rt> <connected>`，例如
`gamepad 1 0 0 0 0 0 0 1` 表示按住 A。SDK 事件脚本例子：

```json
[{"ms": 100, "kind": "gamepad", "buttons": 1, "lx": 0, "ly": 0, "rx": 0, "ry": 0, "lt": 0, "rt": 0, "connected": 1}]
```
