# 麦克风标定与声音检测

应用需要 MIC（64）、ABI 1、API Level 3。用 `mic_open(16000, TINYRT_MIC_FEATURES)` 启动，若返回 −2，提示麦克风不可用。每次 event 通过 `mic_features` 读完积压的 32 字节帧，按顺序交给 [tinyrt_audio.h](../include/tinyrt_audio.h) 的检测器。一帧对应 160 个采样（10 ms）。重新打开时重新初始化检测器。SETTLING、OVERRUN 或序号跳变会清除检测状态并令该帧无效。

## 真机录音

在 S31 V2 成品壳上使用开发版固件。记录 MIC 型号、板号、固件版本、增益、角度、距离、环境、播放音量及参与者的同意记录。以下从固件仓库根目录执行，把 `COM11` 和输出路径换成实际值。输出目录应已存在。原始音频、标注及个人资料保存在参与者同意的本地位置。

```powershell
python esp32/tools/cli/eedb.py --port COM11 mic record --seconds 3 --gain 24 -o D:/mic/quiet.wav --features D:/mic/quiet.csv
python esp32/tools/cli/eedb.py --port COM11 mic record --seconds 3 --gain 24 -o D:/mic/light.wav --features D:/mic/light.csv
```

第一段保持安静。第二段在日常使用距离轻吹一次，保持至少 0.3 s。确认录音指示在采集期间可见。削波时记录距离、增益并重新采集，改变增益后重做两段标定。让 WAV、设备 CSV 和描述录音条件的 JSON 使用同一文件名主干。用设备 CSV 可保留 PLAYBACK、OVERRUN、SETTLING 标志与真实帧序号。EEDB 在 WAV 的缺帧位置补零；配套 JSON 报告缺帧时，离线工具要求改用设备 CSV，再采无丢帧的录音做验收。

## 电脑标定与回放

从 SDK 根目录运行。设置 `TINYRT_CC` 指向 Zig、Clang 或 GCC 的本机编译器。输入 WAV 时用 `--extractor` 指向与固件同一版 core 的 `tinyrt-mic-features`。它接受 16 kHz、单声道、PCM16 WAV，命令为 `tinyrt-mic-features input.wav output.csv`。可先用电脑录制这种格式的 WAV 检查流程，玩家使用的标定值应来自实际设备。

```powershell
$env:TINYRT_CC = 'C:/tools/zig/zig.exe'
python tools/audio/eval_detector.py calibrate D:/mic/quiet.csv D:/mic/light.csv -o D:/mic/player01.json
python tools/audio/eval_detector.py trace D:/mic/session.csv --detector blow --calibration D:/mic/player01.json -o D:/mic/blow-trace.csv
python tools/audio/eval_detector.py trace D:/mic/session.wav --detector pitch --extractor C:/tools/tinyrt-mic-features.exe
python tools/tinyrt.py run my-app --mic D:/mic/session.wav --frames 0,330,1000
```

标定成功会输出 `floor_db_q8`、`ref_db_q8` 和 `ok=1`。安静段需要连续 100 帧，参考段需要连续 30 帧比静音均值高至少 6 dB。两段均跳过带 PLAYBACK、CLIPPED、OVERRUN、SETTLING 或序号跳变的窗口。失败时检查录音、距离及削波后重录。输出文件名需尚未存在。

长录音中先对照动作时间和响度曲线，选择完整轻吹中部的连续 300 ms。给 `calibrate` 加 `--reference-start-ms` 可锁定窗口，防止准备时的声音被当作轻吹。用 `--quiet-start-ms` 同样可锁定 1000 ms 静音窗口，两者均按 10 ms 对齐。重复三次轻吹时，各取一个稳定窗口，使用三个均值中居中的一次。

在设备应用中，先清零 `ta_calib_t` 和 `ta_calib_window_t`。逐帧运行 `ta_calib_push(&window, &frame, 0, &cal)`，返回 1 表示静音段完成。提示玩家轻吹并重新清零 window，再用 `reference=1` 喂帧，成功后将两个有符号 Q8 值存入应用 KV。用 `ta_blow_init(&state, NULL, &cal)` 加载结果。两值之差描述玩家轻吹相对环境的响度，检测器用它确定力度的零点。

桌面 runner 的 `--mic` 按虚拟时钟回放 WAV。事件脚本也可指定 `{"ms":100,"kind":"mic_level","peak":16000,"rms":4000,"flags":0}`，向已订阅的应用注入一次电平事件。

## 原生检测器

| 检测器 | 判定条件及输出 |
|---|---|
| `ta_blow_push` | 四项分段线性得分按 46%、20%、18%、16% 求和。斜坡分别为响度余量 5–13 dB、无音高置信度 0.5–0.8、低中频能量比 0.4–0.75、谱平坦度 0.2–0.5。得分连续 100 ms 达 0.75 进入，连续 80 ms 低于 0.60 退出。进入时 PLAYBACK 帧多要求 6 dB。候选期峰值达 24000 后 60 ms 内幅度衰减至十分之一，清除候选计时。 |
| 力度 | 轻吹参考至参考 +15 dB 映射到 Q15，激活期间每帧向目标靠近剩余差值的十分之一。无玩家标定时按环境以上 9–24 dB 映射。 |
| `ta_clap_push` | 峰值 ≥24000、谱平坦度 ≥0.35，随后 60 ms 内峰值衰减至十分之一，输出一帧脉冲，200 ms 不应期。 |
| `ta_scream_push` | 响度比环境高 ≥25 dB、音高置信度 ≥0.5、2–4 kHz 能量比 ≥0.25。进入 150 ms，退出 120 ms。 |
| `ta_pitch_push` | 对置信度 ≥0.6 的非零音高取最近 5 个有效帧的中值。返回 Hz ×16、MIDI 音分（A4 = 6900）和置信度。连续 3 帧无音高后失效。 |

## 按玩家评测

先按人划分 train、validation 和 test，例如 8 人分 5/1/2。一个人的所有录音均属于同一组。用训练组定参数，在验证组检查误触和最差片段，参数锁定后再打开预留测试组。第一轮用 5–8 人，每人录轻吹与普通吹各 10 次（含 0.3 s 短吹、1.5 s 长吹，距离 3、8、15 cm）、连续两口短吹 5 组、拍手 20 次（30 cm 与 1 m 各半）、敲壳和桌面各 10 次、朗读 2 分钟、喊声 10 次、哼唱 5 段、边放音边吹 10 次。另录至少 10 分钟风扇、走动、遮挡等背景。

在 `sessions.csv` 用 `player,split,recording,calibration` 四列登记匿名玩家、划分、WAV 或特征 CSV、可选的标定 JSON。路径相对这张表。其余会话条件可增加列。每段录音配同名 `.labels.csv`，包含 `start_sample,end_sample,label,uncertain`。以录音起点为零，按递增、不重叠的半开样本区间标记。类别取 blow、clap、knock、speech、scream、hum、other。uncertain 为 0 或 1。未标注区间计入背景负样本。比较设备 CSV 和同一 WAV 的离线 CSV 前先对齐第一帧及 L1 初始状态，分别检查序列、特征和设备独有标志。

```powershell
python tools/audio/eval_detector.py evaluate D:/mic/sessions.csv --split train --detector blow -o D:/mic/train.md
python tools/audio/eval_detector.py evaluate D:/mic/sessions.csv --split validation --detector blow --params D:/mic/candidate.json -o D:/mic/validation.md
```

`--params` 接受 `ta_blow_cfg_t` 同名字段组成的扁平 JSON，例如 `{"gate.enter_ms":90,"gate.enter_q15":25000}`。四个斜坡分别用 `level`、`unvoiced`、`low_band`、`flatness` 的 `.lo`、`.hi`、`.weight`；响度单位 Q8 dB，其余斜坡及门限用 Q15，四个整数权重之和须为 100。在训练组测各项分布后，以普通吹气召回 ≥95% 为约束比较参数，优先减少说话误触。报告按人列出事件命中分子与分母、各干扰类误触次数及每小时误触、长吹额外启动、起止延迟 P50/P95 和待复听片段。起止时刻按帧结束采样计，提前释放产生负延迟，录音末尾仍激活的事件无释放延迟。模糊标注单列。

## 验收状态

当前默认参数来自计划初值。合成特征回归覆盖校准、状态机、校准力度、短峰、放音、失帧和音高。尚无获同意的分人标注测试集，无法报告普通吹气召回、真实拍手与说话误触率、不同距离表现或玩家试玩结果。采集后需分别核对普通吹气 ≥95%、100 次拍手误触 ≤1 次、10 分钟朗读误触 ≤1 次，以及 ≥95% 的 1.5 s 长吹只启动一次。现场起停延迟的目标分别为 P95 ≤200 ms 和 ≤180 ms，需要屏幕时序测量。
