# Flappy Bird · 466×466 圆屏演示

采用经典 Flappy Bird 小鸟、管道、背景、像素数字、Game Over 计分板和橙色 OK 按钮。画面铺满圆屏，小鸟三帧扇翼、地面滚动，失败后下落并显示得分、最高分、NEW 纪录提示；10 分银牌、20 分金牌。封面也来自游戏内实际素材。

## 操作

- 点击游戏区域或短按 KEY1 起跳。按住、拖动和松手不连跳。
- 穿过管道得分；碰到管道、上边界或地面结束。
- 失败后 600 ms 显示计分板，800 ms 后可点击橙色 OK 或短按 KEY1 重开。
- 最高分写入应用独立 KV；正常退出保存。突然断电仍受 Host 延迟落盘规则影响。
- 宿主返回控件、KEY1 长按负责退出。输入队列取消会清除触摸和按键状态，避免吞掉下一次按下。

音效使用原始 wing、point、hit、die WAV 转换出的 16 kHz 单声道 PCM。碰撞先播放 hit，随后播放 die。宿主后台有界队列播放，不在游戏回调里等待扬声器；队列满时允许丢弃音效。桌面 runner 校验音频请求但静音，不代表真机扬声器已经验收。IMU 和 MIC 未使用。

## 构建与验证

需要 Python、Pillow、SDK requirements、Zig 0.13 和支持 audio_play 的匹配 runner。以下从 SDK 根目录运行：

```powershell
python examples/flappy/prepare_assets.py
python tests/flappy/run_native.py --cc path/to/zig.exe
python tools/tinyrt.py build examples/flappy --cc path/to/zig.exe
python tools/tinyrt.py run examples/flappy --runner path/to/tinyrt-run.exe --events examples/flappy/events.json --frames 32,990,3300,6600,9900,14000 --output examples/flappy/build/classic-preview
python tools/tinyrt.py pack examples/flappy --aot --development-key --wamrc path/to/wamrc.exe
python tools/tinyrt.py validate examples/flappy/build/demo.sky-hop.trpkg --development-key
python examples/flappy/export_web.py
```

保留 demo.sky-hop 应用 ID 以便更新旧预览，版本递增到 3。游戏中分数顶部下移到屏幕 y=88，避开宿主返回按钮。旧固件不支持 AUDIO 权限和导入，必须配套更新固件。

游戏使用 33 ms 固定整数物理与有界补帧；8 ms 时钟分四条带合成 233×233 RGB565 完整帧，再由宿主放大到 466×466。一帧只提交一张图，部分合成帧不显示；像素素材通过包内只读资源读取，避免超出解释器回调预算。32 ms 合成周期是软件目标，不是设备帧率实测结果。

测试覆盖持续按压、队列取消、重试、防误触、得分与最高分、时钟回绕、像素/音频资源边界。事件脚本飞行约 12 秒后停止操作，最后一帧展示失败计分板。截图来自真实 WAMR 解释器。没有烧录或真机性能、触摸、音量验收。

## 网页交付与素材来源

build/sky-hop-web-release.zip 包含应用包、210×210 封面、三张圆屏截图、介绍、目录元数据与素材说明。website.json 标为开发预览，导出目录中的 catalog.json 记录包大小与 SHA-256。公开开发钥不代表正式发行身份；AOT 需要固件允许对应开发授权。

原始素材和音效保存在 assets/，转换结果为 resources.bin 与 assets_generated.h。来源与版权说明见 [assets/NOTICE.md](assets/NOTICE.md)。原版素材权利并不因仓库代码开源而自动转授；网页资料明确标注素材授权尚未核实。

旧 counter、color、pomodoro、snake、nes、nes-maze、nes-player/SkyTrail 示例及其专用测试已按要求移除，仅保留本示例和通用 SDK/core 测试。删除源码不会卸载设备上已安装的旧应用。
