# TinyRT SDK：应用开发速查

SDK 对外版本唯一来源是根目录 VERSION，`python tools/tinyrt.py --version` 查询。
应用发布版本见各自 `app.json.version`；反复编译不自动升版本。

## 本项目设备交付约定

`pack <app>` 默认用仓库内 `tools/toolchains/esp32s31/windows-x64/wamrc.exe` 编译 S31 AOT，不需要额外参数。不得引用个人机器的 `tmp/` 编译器路径；SDK 按 `tools/toolchains/esp32s31.json` 强制核验摘要。来源、许可证与网站登记说明见 [S31 编译器](tools/toolchains/esp32s31/windows-x64/README.md)。网站 ZIP 用 `release <app>`，默认同一个编译器。

默认交付 AOT 包；网站发布使用
`release <app>`（默认 wasm-aot + esp32s31）。桌面 WAMR 的 Wasm 仅用于预览和测试。
安装前确认宿主 `aot_enabled=true`；安装后查询
APP_INFO，必须 `selected_backend=2`、`fallback_reason=0`，不得以 Wasm 回退作为设备验收。

## 本地交付网站 ZIP

从 SDK 根目录执行：

```powershell
python tools/tinyrt.py release examples/flappy --cc path/to/zig.exe --runner path/to/tinyrt-run.exe
```

设置 TINYRT_CC / TINYRT_RUNNER 后可用 `make release APP=examples/flappy`。
命令离线完成测试、编译、WAMR 截图、打包、清单、ZIP 与自检。默认输出应用 release/ 下
的 ZIP 和同名展开目录；本项目只交付 AOT，默认 wasm-aot。
Wasm 用于编译、预览和包内回退，独立 Wasm ZIP 仅用于解释器诊断。

应用提供 app.json、源码、listing.json、cover.png 和 README.md / CHANGELOG.md / LICENSES.md。
三个 Markdown 用 UTF-8 无 BOM、LF、各≤64 KiB；封面为 210×210 单帧 RGB/RGBA PNG。
listing 包含 summary/category/tags/controls/publisher_display/release_notes/full_bleed/rights/device_verified/screenshots，
不允许 display_version；完整例子在 README“一键生成网站交付 ZIP”。
拒绝未知字段和重复键。截图 at_ms 与 file 二选一、1–6 项；真机图必须原本为 466×466。
截图/events 时间受 --preview-ms 限制，默认最多 60000 ms。没有真机验证不填 device_verified=true，
授权未确认保留 rights.status=unverified。两个 new 模板均带完整资料与占位封面。

应用发布版本是从 1 开始的整数，上架更新要递增；设备本身允许同版本覆盖和降级，
release.json 必须与应用包相同。不要往 app.json 或网站 v2 清单添加显示版本字段。
构建时间取 SOURCE_DATE_EPOCH / --built-at / SDK 提交时间，本地构建不自动升版本。
规则见 specs/release-bundle-v2.md。

这是开发预览版。Guest ABI、包格式、存储布局与 BLE schema 均为 1；应用 `version` 是从 1 开始的独立整数。先读本页，再修改应用目录中的 C 文件和 `app.json`。生成的 `include/tinyrt.h` 由 core 契约同步，不手工修改。详细说明见 [README](README.md) 和 [包规格](https://github.com/No-Chicken/tinyrt/blob/main/specs/package.md)。

## 从游戏模板到可分享的包

需要 Python 3.10+、Zig 0.13.0 或支持 wasm32 的 Clang。桌面预览还需要真实 WAMR `tinyrt-run.exe`；S31 AOT 编译器随仓库提供。用 `--runner` 指向匹配的本地工具；使用下载工具时须核对发布附件和固定摘要。命令从 SDK 根目录运行，路径按机器替换。

```powershell
python -m pip install -r requirements.txt
python tools/tinyrt.py new my-game --title "My game" --template game
python tools/tinyrt.py build my-game --cc path/to/zig.exe
python tools/tinyrt.py run my-game --runner path/to/tinyrt-run.exe --events my-game/events.json --frames 0,330
python tools/tinyrt.py pack my-game
python tools/tinyrt.py validate my-game/build/my-game.trpkg
```

创建 `my-game/events.json`，按住触摸让模板中的角色移动；时间不得超过最后一个截图时间：

```json
[
  {"ms": 33, "kind": "press", "x": 300, "y": 300},
  {"ms": 165, "kind": "move", "x": 330, "y": 220},
  {"ms": 297, "kind": "release", "x": 330, "y": 220}
]
```

输出仅允许更新本工具标记的PNG与report.json；拒绝覆盖输入、链接目标或未知已有文件。

模板是完整可运行的小游戏：四个回调、33 ms 时钟、触摸生命周期、32×32 RGB565 图像与 Host 缩放。源码在 [templates/game/main.c](templates/game/main.c)，`new` 会复制清单与源码供你继续开发。截图默认写入 `my-game/build/preview/frame-000000.png` 和 `frame-000330.png`，计时写入 `report.json`。预览跑真实 WAMR 解释器，桌面时间不是设备 FPS；桌面字体与设备字体也不完全一致。JSON脚本支持触摸press/move/release/cancel，以及已订阅KEY1的应用所用的key_press（x=1,y=1）/key_release（x=1,y=0）。默认按Guest的clock_interval推进虚拟时间，处理完到期CLOCK再投递该时间点的输入并截图；截图不额外运行render。桌面回调执行的墙钟耗时不会推进虚拟时钟。显式--step-ms 33会每33ms强制注入CLOCK，覆盖Guest周期，只用于诊断。

`.trpkg` 不需要任何密钥：设备不校验签名，`app_id` 只要合法就能用，存档随 app_id 走。编译器摘要、目标、bounds/stack/loop-poll 选项均固定。

在应用大厅开启安装窗口，再安装、核查和启动：

```powershell
python -m pip install bleak winrt-Windows.Devices.Bluetooth winrt-Windows.Devices.Enumeration
python tools/ble_install.py --scan
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install my-game/build/my-game.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id my-game
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF launch my-game/build/my-game.trpkg
```

按设备屏幕 PIN 配对。查看 `selected_backend=2` 才能确认设备选择 AOT；`1` 是 Wasm，结合 `fallback_reason` 判断性能降低原因。安装成功与运行正确需要分别验证。

## 回调与输入

| 回调 | 工作 |
|---|---|
| `int32_t tinyrt_init(int32_t width, int32_t height)` | 初始化状态、输入订阅、时钟；返回 0 表示成功 |
| `int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg)` | 推进游戏、处理输入、读取资源；成功返回 0 |
| `int32_t tinyrt_render(void)` | 仅提交绘制；每个普通帧从 `draw_clear` 开始，或只调用一次 `draw_skip` |
| `int32_t tinyrt_stop(void)`（可选） | 正常离场时有界保存；不得绘制；故障和断电不保证调用 |

466×466 圆屏的四角不可见，约 330×330 的中央正方形是安全区（约 x/y=68..397）。EEBadge 宿主使用边缘返回手势，无固定顶部返回键；未订阅 KEY1 时短按由宿主返回应用大厅。CLOCK=2；触摸 RELEASE=1、PRESS=3、MOVE=4、CANCEL=5。在 init 调用 `input_events(56)` 订阅完整触摸；mask=0 保留 release 模式。KEY1为事件6（`TINYRT_USER_KEY_EVENT`），x=1、y=1按下/0释放、arg=0；init订阅mask=64，触摸与KEY1合用mask=120。CANCEL清除触摸与KEY1按住状态；队列溢出可能丢失KEY1 release，因此取消必须解除所有按住动作。应用认领KEY1后短按/双击用于Guest，长按仍保留Host返回。按住动作在release/cancel时清零，release不自动代表click。使用 `now_ms()` 的无符号差推进时间，禁止 Guest 自己忙等。

## ABI 1 全部 Host 函数

`gfx_*` / `fb_*` import、能力位和二进制记录见生成的
`include/tinyrt_gfx.h` 与 [图形契约](specs/graphics-v1.md)。其余函数约束如下。

所有函数在模块 `tinyrt` 中；整数为 i32/u32，像素和文本传 Guest 线性内存地址。`now_ms`返回u32，其余返回i32；`kv_get`返回存储值，`asset_read`返回长度；绘制和设置通常以0表示成功，失败会令当前回调失败。

| 函数 | 权限与约束 |
|---|---|
| `draw_clear(rgb)` | DRAW=1；render 首条，颜色 `0xRRGGBB` |
| `draw_rect(x,y,w,h,rgb)` | DRAW；正尺寸，完整矩形在画布内 |
| `draw_round_rect(x,y,w,h,radius,rgb)` | DRAW；radius=0..min(w,h)/2 |
| `draw_arc(cx,cy,radius,thickness,start_deg,end_deg,rgb)` | DRAW；0≤start≤end≤360，0 度向右、顺时针；0..360 全圆 |
| `draw_text(x,y,text,len,rgb)` | DRAW；严格 UTF-8，1..63 字节 |
| `draw_text_box(x,y,w,h,text,len,rgb,font_px,align)` | DRAW；单行裁剪、垂直居中；字体18/24/36/48；align=0左/1中/2右 |
| `draw_rgb565(x,y,w,h,pixels,len)` | DRAW；源宽≤256、高≤240；len=w×h×2，小端 RGB565 |
| `draw_rgb565_scaled(x,y,dst_w,dst_h,src_w,src_h,pixels,len)` | DRAW；源宽≤256、高≤240，len=src_w×src_h×2；目标完整落在画布；Host 最近邻缩放 |
| `draw_skip()` | DRAW；render 的唯一操作；保留上一帧 |
| `input_events(mask)` | INPUT=2；仅 init；允许0/56/64/120，可附加运动位0x80与手柄位0x100；见 specs/motion.md、specs/gamepad.md |
| `clock_interval(ms)` | CLOCK=8；init/event；1..1000 ms，默认100；Host 无补偿突发回调 |
| `now_ms()` | CLOCK；Host 的 u32 单调毫秒时钟 |
| `kv_get(key,fallback)` / `kv_set(key,value)` | STORAGE=4；key=0..15，值为 i32，每应用隔离；应用在 init/event/stop 中保存状态 |
| `asset_read(offset,destination,len)` | 无额外权限；仅 init/event，只读当前包 resources；每次≤4096字节，返回读取长度或-1 |
| `haptic_pulse(strength,duration_ms)` / `haptic_stop()` | HAPTIC=32；仅 init/event；强度 1..100%，时长 1..2000 ms；新脉冲替换旧脉冲，非阻塞；0 成功、1 不支持，非法参数或调用阶段使回调失败；应用结束由 Host 停止；桌面无马达 |
| `audio_play(resource_offset,byte_length,sample_rate)` | AUDIO=16；仅init/event；当前包资源内PCM16 LE单声道，固定16000Hz，偶数字节2..32000（最长1秒）；每回调最多4次；0已排队，1队列满、次数超限或不可用；Host复制，退出清队列并取消在播音效；桌面预览校验资源但静音 |
| `runtime_backend()` | 无额外权限；0=classic解释器，1=AOT；用于选择有界工作批量，不保证设备速度 |

## 性能、边界和常见错误

- 清单 `permissions` 是上述位之和，最大63；`memory_pages` 为1..16，每页64 KiB；`budget` 为每回调1..100000条受计量指令。AOT 保留边界、原生栈与循环取消检查，不把指令计量转换成固定设备耗时承诺。
- 整包（含头、section 表、Wasm、AOT、资源和对齐）不超过设备 HELLO 报告的包库数据区即可，无单独单包上限；更新需另一段同样大小的连续空闲区。runtime 配额2 MiB还包含运行时开销和 AOT 映射预留；16页线性内存不意味着这些额外成本免费。
- 一帧最多128条命令，两个 RGB565 函数合计最多提交一张图。把像素工作合到一张图后提交；更大内容用 Host 缩放。长计算分片放在 event，单次回调及时返回。
- render 中调用资源或时钟设置会失败；KV 状态逻辑放在 init/event/stop；普通帧忘记 clear、混用 skip 与绘制、越界文本/像素、无权限 import 都会失败。不得依赖 WASI、libc、构造器或未声明的 import。
- `kv_set` 成功先更新 RAM。Host 普通保存按全局5秒合并，正常 stop 强制刷新；断电可能丢失尚未保存的状态。仅保存实际变化。
- `validate` 仅检查信封，不运行 Wasm/AOT，也不证明玩法正确。同 ID 的不同包会直接替换旧包并保留存档，同版本重编译和降级都可以。旧内部包和目录与当前格式不兼容，需重新构建包并按设备部署流程处理旧媒体。

应用版本、ZIP 命名和兼容性注意事项见 [版本管理](specs/version-management.md)。
