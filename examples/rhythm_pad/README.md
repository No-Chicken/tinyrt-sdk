# Rhythm Pad

需要 API Level 4 和 DRAW、INPUT、AUDIO、HAPTIC 权限。轻点左半屏，应用写入 40 ms 的 500 Hz PCM16 方波。轻点右半屏，设备合成 120 ms 的 880 Hz 提示音。每次点击都触发两次短振动，中间停顿 65 ms。

从 SDK 根目录运行 `python tools/tinyrt.py dev examples/rhythm_pad`。在音频尚未播完或振动间隔中关闭应用，可以检查退出时的停止行为。
