# Wave 真机验证流程

当前 S3 已安装正式 `demo.wave-sim` revision 7、20 网格/130 粒子包。匹配固件启用开发 AOT，查询结果必须为 `selected_backend=2`、`fallback_reason=0`。安装和运行记录应保存固件身份、包摘要、配置及完整日志；使用当前扫描地址，不复用旧设备地址或旧构建路径。

1. 扫描设备并开启安装窗口，安装普通 AOT 包。查询 APP_INFO 核对身份、revision、大小和实际后端，再启动。操作取消后先查询状态，避免直接重复安装。
2. 验证四方向、触摸长按 500 ms、取消、短按掀浪和长按退出，以及五主题循环。软件注入传感器和主题自动化已通过；本轮未人工倾斜，实物手感应单独记录。
3. 普通包用宿主日志统计真实面板完成窗口，同时记录 Guest 提交、获取、绘制和传输时间。临时 P/R 测量用 `tools/build_debug.py` 生成的同 ID、同 revision metrics 包；开发控制台移除该 ID 后临时安装，测量后恢复普通包。禁止增加诊断 app ID。
4. metrics 文本有额外成本，其提交 FPS 不代表普通包面板速度。P/R 为 1 ms 分辨率的 Guest 时间，不含宿主绘制与传输。

内部 SRAM 条带的最新普通包持续倾斜为 21.36/24.26 FPS，平稳及主题窗口约 26.88–27.07 FPS；不能宣称全部场景达到 25 FPS。此次 32 行内部缓冲全部命中，8 行回退未触发。日志为根仓库 `tmp/gfx-wave20-sram8-active-bench.log`；旧普通包及 metrics 证据见 [VALIDATION](VALIDATION.md)。最终发布资料待交付记录确认后更新。

局部刷新包后续 60 秒实测为左右持续倾斜 21.71 FPS、上下 27.96 FPS、平稳及主题 30.17–30.27 FPS。8 行内部 SRAM 缓冲全命中，原生绘制平稳约 7.9 ms、倾斜约 21.1 ms。证据为根仓库 `tmp/gfx-wave20-damage-active-bench.log`；左右倾斜仍未达到 25 FPS。横向裁剪优化待独立测量。

横向 region 的 build12 后续阶段持续倾斜为 29.47–29.56 FPS，平稳为 30.27–30.37 FPS，16 行内部 SRAM 全命中，后段原生约 4.999 ms。首窗混入 Bird 绘制应剔除；证据为根仓库 `tmp/gfx-wave20-region-active-bench.log`。最终固件 build13 连续运动约 29.77 FPS，平稳及主题约 30.07–30.34 FPS，原生稳定 4.58–4.72 ms；四方向及五主题软件注入通过，证据为 `tmp/gfx-wave20-final-build13-bench.log`。
