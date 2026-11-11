# Puff

Level 3 麦克风游戏，面向 Magie S31 的 466×466 圆屏。吹气带着豆豆与红、黄两只气球上升，在 25 秒内到达云顶；停止吹气时悬停，等待黄蜂经过。气球和角色均参与碰撞，两只气球都失去后本局结束。

- 点击「开始飞行」，麦克风在倒计时期间稳定采样，倒计时后吹气起飞。
- 默认「力度」模式：根据麦克风电平改变上升速度；「匀速」模式下，识别到吹气便以固定速度上升。
- 结算页点击「再飞一次」重新开始；短按设备按键返回应用大厅。
- 大厅名称为 `Puff`，封面使用独立的 210×210 不透明方图。

## 构建与安装

在 SDK 根目录执行，`--cc` 指向本机 Zig：

```powershell
python tools/tinyrt.py build examples/puff --cc path/to/zig.exe
python tools/tinyrt.py pack examples/puff
python tools/tinyrt.py install examples/puff/build/puff.trpkg --port COM13
```

安装前在设备应用大厅打开「添加」页。需要固件 0.4.0 及以上、API Level 3；默认打包包含 S31 AOT。

素材已随示例提交。重新生成精灵与文字需要 Python、Pillow 和本机 Microsoft YaHei 字体：

```powershell
python examples/puff/prepare_assets.py
```

`prepare_assets.py` 只更新精灵图集和头文件；独立封面保存在 `cover.png`。

## 麦克风输入

游戏以 16 kHz、12 dB 增益读取宿主每 10 ms 提供的特征。`puff_input.h` 在 SDK 吹气检测器上增加低频占比、音高置信度、持续时间及冲击衰减筛选，减少拍手和敲击外壳造成的误触。

力度以固定参考电平上下各 6 dB 映射并平滑，不随环境底噪估计漂移；它表示声音电平，不是物理气压。`app.json` 中的 `PUFF_CAL_FLOOR_Q8`、`PUFF_CAL_REF_Q8` 是装配设备测量所得的初始参考，不能代表所有玩家、距离和外壳。更换硬件后应按 [声音校准流程](../../specs/audio-detectors.md) 重新测量。

已完成实机游玩、AOT 启动及按键退出检查。冲击筛选经过录音回放对比，仍需更多玩家和噪声场景验证，不能保证完全排除外壳碰撞误触。
