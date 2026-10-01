# Color

每次触摸松开切换三种背景颜色，不保存状态。ABI 1 权限为 draw/input（3）。

从 SDK 根目录执行：

```powershell
python tools/tinyrt.py build examples/color --cc path/to/zig.exe
python tools/tinyrt.py pack examples/color --key path/to/application-signing.pem --key-id 100
```

验收：初始为绿色，连续点击依次红色、蓝色、绿色；重新打开回绿色。设备屏幕和触摸结果在产品层另行验证。
