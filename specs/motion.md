# ABI 1 运动事件扩展

`TINYRT_EV_MOTION=7`，订阅位 `TINYRT_IN_MOTION=0x80`。
调用 `input_events(old_mask | TINYRT_IN_MOTION)`；合法掩码为
0、56、64、120、128、184、192、248，仅 init 且需要 INPUT 权限。

事件 `x=ax`、`y=ay`、`arg=az`，都是经 BSP 安装方向归一化后的有符号 mg，
每轴范围 -16000..16000。宿主以约 34 ms 周期读取运动快照，
只发送新时间戳、100 ms 内的有效有限采样。设备无传感器或数据陈旧时不投递。
BSP 将不同板子的安装方向转换到旧板坐标；应用统一使用屏幕 X=ax、Y=-ay，
无需识别芯片或重复旋转。S31 V2 的原始 X/Y 加速度和角速度反号，Z 不变。
桌面注入直接使用归一化坐标，不能代替物理方向验收。

原有应用不订阅则不接收。旧运行时在 init 拒绝新掩码；运动应用需要更新宿主。
不新增 import、不改变包格式和 BLE 布局。新 SDK 契约固定到支持扩展的核心提交。

桌面 runner 接受 `motion -700 0 1000`。SDK 事件脚本例子：

```json
[{"ms": 100, "kind": "motion", "x": -700, "y": 0, "arg": 1000}]
```

Wave 的 `tools/live_preview.py --runner <tinyrt-run.exe>` 用方向键生成运动事件，
Space 模拟 KEY1，鼠标模拟触摸。画面来自真实 WAMR 解释器，不代表真机性能。
