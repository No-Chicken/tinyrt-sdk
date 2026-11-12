# Puff

Magie S31 上的吹气气球冒险。对着吧唧吹气让豆豆和气球上升，倾斜设备左右移动，避开障碍、收集金币和星币，打败每个世界的首领。

## 玩法

| 操作 | 方式 |
|---|---|
| 上升 | 吹气；默认「力度」模式下吹得越用力上升越快，停止吹气立即悬停 |
| 左右 | 倾斜设备，设置里可调灵敏度 |
| 使用羽毛 | 拿到羽毛后点击屏幕，向上冲刺并短暂无敌 |
| 返回大厅 | 短按设备按键 |

气球就是生命：开局 3 个，最多 5 个，被碰到会失去一个。气球越少越重，上升越慢。

## 内容

- **4 个世界、16 关**：花园、云海、夜空、风暴，每个世界 3 关加 1 个首领关。
- **关卡类型**：飞到终点旗、限时到达、收齐金币才开终点、逃离从下方追来的风暴、首领战。
- **障碍**：蜜蜂、刺藤、侧风、弹力云、雷云、乌鸦、刺球、夜空视野（萤火虫补光）、暴雨、旋风、电网。
- **道具**：气球、护盾、磁铁、羽毛、沙漏、别针。
- **首领**：蜂后、雷鸣云、夜枭、风暴之王。从下方撞向首领粉色的弱点造成伤害；雷鸣云只在劈雷后下沉时、夜枭只在俯冲后停下喘气时能被打到。
- **三星评价**：通关、收齐本关星币、不丢气球。
- **无尽模式**：通过第 1 世界首领后解锁，记录最高高度和分数。

## 吹气标定

首次启动使用内置的出厂标定值；在「设置 → 吹气标定」可按提示先保持安静、再持续吹气，完成个人标定。吹气感应暂不可用时，可在设置里打开「按屏代吹」，按住屏幕代替吹气。游戏只读取宿主每 10 ms 提供的声音特征，不录音、不保存音频。

## 构建与安装

在 SDK 根目录执行，`--cc` 指向本机 Zig 0.13：

```powershell
python tools/tinyrt.py build examples/puff --cc path/to/zig.exe
python tools/tinyrt.py pack examples/puff
python tools/tinyrt.py install examples/puff/build/puff.trpkg
```

需要 ABI 1.3、固件 0.4.0 及以上；设备只运行 AOT，`pack` 默认生成 S31 AOT 包。

## 上传网站

在 SDK 根目录执行：

```powershell
python tools/tinyrt.py release examples/puff --cc path/to/zig.exe
```

上传生成的 `release/puff-v<version>-wasm-aot.zip`。该目录位于 `examples/puff/` 下；ZIP 包含应用包、方形封面、真实 WAMR 截图、使用说明、更新记录、内容来源、网站清单和构建报告。
通用发布命令与前置条件见 [SDK 打包说明](../../README.md#一键生成网站交付-zip)。

## 文件

| 文件 | 内容 |
|---|---|
| `main.c` | 界面流程、渲染、输入、存档 |
| `puff_game.h` | 关卡数据和游戏逻辑（整数、确定性） |
| `puff_input.h` | 吹气检测：在 SDK 检测器上加冲击声筛选 |
| `prepare_assets.py` | 用代码生成全部像素图、文字字形和音效，输出 `resources.bin`、`assets_generated.h` |

修改 `main.c` 或 `puff_game.h` 中的中文后，重新运行 `python examples/puff/prepare_assets.py` 以更新字形表（需要 Pillow 和 Microsoft YaHei 字体）。
