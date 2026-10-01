# Counter

触摸松开时加一，用 KV key 0 保存；负数存档恢复为零，达到 i32 正数上限后保持该值。ABI 1 权限为 draw/input/storage（7）。

从 SDK 根目录执行：

```powershell
python tools/tinyrt.py build examples/counter --cc path/to/zig.exe
python tools/tinyrt.py pack examples/counter --key path/to/application-signing.pem --key-id 100
```

验收：默认显示 0；一次点击加一；关闭/重新打开保留计数。原有应用行为由 TinyRT 核心的独立计数器测试夹具回归；本 SDK 独立构建须另外通过，设备验收属于产品层。
