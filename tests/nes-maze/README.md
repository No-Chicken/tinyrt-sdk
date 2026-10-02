# NES Maze 验证

当前 [app.json](../../examples/nes-maze/app.json) 为 version4。实际 Wasm 构建、真实 CPU/PPU 的 WAMR 通关路线和 host 输入生命周期测试已通过；这些是桌面验证，不是当前固件的整机 FPS 测量。

## v4 输入能力与验证边界

应用在初始化时订阅 `input_events(0x38)`，接收 PRESS、MOVE、CANCEL，并继续处理 RELEASE。方向键支持按住重复移动、拖动换向；RELEASE/CANCEL 和停止会清除保持状态，START/RESET 不连发。首次按住等待300 ms，随后每150 ms提出一次移动请求，实际移动仍受模拟器控制器读取和调度影响。旧脚本只发送 RELEASE 的点击方式继续可用；旧 guest 未订阅时不会收到新增事件。v4 需要提供 `input_events` 导入的宿主。

[test_input.c](test_input.c) 编译实际 `main.c`，用明确的引擎和 ABI 替身检查快速点击不丢失且只触发一次、按住重复、松开停止、拖动换向、取消、START 不重复、旧点击与退出清键。当前结果为 `NES INPUT PASS frames=204 presses=11`；这里的 frame 是测试替身计数，不是显示帧率。[test_wamr.c](test_wamr.c) 则运行实际 Wasm 与真实 WAMR、CPU/PPU，检查完整路线、实例重建及资源释放。

## 复现

从 SDK 根目录执行，`zig` 未加入 PATH 时将 `$zig` 改为本机可执行文件的绝对路径：

```powershell
$sdk = (Resolve-Path .).Path
$zig = (Get-Command zig).Source
python examples/nes-maze/build.py --cc "$zig" --native
if ($LASTEXITCODE) { throw 'Native build failed' }
& "$sdk/examples/nes-maze/build/nes-maze-native.exe" "$sdk/examples/nes-maze/build"
if ($LASTEXITCODE) { throw 'Native test failed' }
python examples/nes-maze/build.py --cc "$zig" --development-key --key-id 1
if ($LASTEXITCODE) { throw 'Wasm/package build failed' }
```

该命令生成 Wasm 与 PUBLIC 演示钥签名包，仅用于开发测试，不能证明生产发布者身份。AOT 的受控编译、签名和 v2 格式见 [包规格](../../specs/package-v2.md)；普通打包入口只接收 Wasm。ESP32-S3 与 ESP32-S31 需要独立的 AOT 产物与目标验收。

在 MSVC x64 开发环境中编译真实 WAMR runner。下面使用 SDK 同级的 core 和其 WAMR checkout；独立布局需替换 `$core`、`$wamr`。WAMR 必须是 core 要求的固定 commit、干净源码；构建系统准备并核对 runtime 补丁，不要传入已有修改的编译器工作目录。

```powershell
$core = (Resolve-Path ../tinyrt).Path
$wamr = (Resolve-Path "$core/third_party/wamr").Path
$wasm = (Resolve-Path examples/nes-maze/build/nes-maze.wasm).Path
cmake -S tests/nes-maze -B tests/nes-maze/build -G Ninja `
    -D "TINYRT_ROOT=$core" -D "WAMR_ROOT_DIR=$wamr" `
    -D "NES_MAZE_WASM=$wasm" -D CMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Test configure failed' }
cmake --build tests/nes-maze/build
if ($LASTEXITCODE) { throw 'Test build failed' }
ctest --test-dir tests/nes-maze/build --output-on-failure
if ($LASTEXITCODE) { throw 'NES tests failed' }
```

CTest 同时运行 `nes_maze_pointer_lifecycle` 和 `nes_maze`。后者通过 Wasm 实际 ABI 发送点击、验证通关与两次实例创建/销毁，每次回调仍受100000预算控制。需要重采像素和预算时继续执行：

```powershell
& "$sdk/tests/nes-maze/build/test_nes_maze_wamr.exe" "$wasm" 100000 260 "$sdk/examples/nes-maze/build"
if ($LASTEXITCODE) { throw 'WAMR capture failed' }
python tests/nes-maze/test_oracle.py --captures examples/nes-maze/build --png -v
if ($LASTEXITCODE) { throw 'Pixel oracle failed' }
python tests/nes-maze/budget_scan.py --runner tests/nes-maze/build/test_nes_maze_wamr.exe `
    --wasm "$wasm" --output tests/nes-maze/build/budget-scan-v4.json
if ($LASTEXITCODE) { throw 'Budget scan failed' }
python tests/nes-maze/run_perf.py --cc "$zig"
if ($LASTEXITCODE) { throw 'Native performance regression failed' }
```

`--png` 需要 Pillow，仅将实际RGB565像素转换为PNG。runner参数依次是Wasm、预算、每个实例额外空闲帧数（0..260）、可选采样目录。

## 已保存的 v3 历史证据

本目录已有 `artifact.json`、`budget-scan.json`、`board-oracle.json`、preview 和 red/green 文本来自 version3 的采样。下面的具体数值属于该次版本与构建，不能用于声称当前 v4、AOT 或整机性能；重新采样应保留新产物的版本和构建身份，避免覆盖这些历史基线。

- `native.c`：69项断言，直接检查ROM CPU RAM的初始状态、Start、碰墙、同方向重复移动、ROM原始保持键不连跳、20步通关、胜利后不再移动、重开、NMI计数回绕和实例重置。v4 的宿主输入适配通过重复脉冲实现按住移动，不改变此 ROM 行为。
- `test_wamr.c`：通过实际ABI事件点击屏幕按钮，检查画面中的玩家位置、单项输入队列、队列满时不覆盖、无效区域触摸、完整路线、退出及两次实例创建/销毁。每次事件/渲染均受原100000预算控制；停止调用可选 `tinyrt_stop`；销毁后受跟踪内存回到基线，关闭runtime后为0。
- `test_oracle.py`：独立构造预期nametable、解码ROM CHR图块，比较native与WAMR标题、行走、胜利、重开八张完整画面的全部61440像素；核验两次vblank暖机、控制器读取、向量与原创地图。另对WAMR完整路线每步采样作逐像素核验，导出 `board-oracle.json`。
- [artifact.json](artifact.json)：ROM/Wasm/签名包大小、哈希和实际模块导入/导出。`preview.png` 来自native真实帧。
- [budget-scan.json](budget-scan.json)：版本3完整有限路线的最低passing budget=82582，82581失败。正式manifest保持100000。扫描结果仅覆盖此ROM、此有限输入轨迹；不构成任意ROM、sprite或全部6502 opcode的最坏上界。
- `run_perf.py`：60个稳定帧的真实6502 dispatch从595193条降到6399条，背景扫描线绘制从14400条降到0；NMI仍执行60次。`test_idle.c`以未修改的CPU解释器对照17245项断言，覆盖周期余量、NMI、延迟NMI、IRQ、mapper时钟和PRG读取回调。`test_cache_trace.c`比较开关缓存两版54个完整帧的CPU、VRAM和像素摘要，覆盖真实PPU的tile、attribute、palette、mask、scroll、sprite状态变化与重启。
- `red-native.txt`、`red-wamr.txt` 记录先失败的行为；`green-native.txt`、`green-wamr.txt` 记录最终通过结果。

版本3长轨迹包含两个完整通关实例，各再空闲260帧；需要4870次CLOCK事件，原版本需要64056次。每次测试点击后完成三个新画面最多64次CLOCK（原版本264次，会触发新增回归失败）。真实WAMR峰值跟踪内存490355字节（包括模块、线性内存和宿主frame，不是进程RSS或板上总RAM）。桌面耗时包含加载、建实例、测试检查及采样文件开销，不能换算成ESP32-S3帧率。

`red-perf.txt`、`green-perf.txt`分别保留优化前的性能失败与差分验证结果。固定NROM的背景缓存要求可见期间CPU没有可能修改PPU的工作；条件不满足时退回原渲染。该结果不代表任意NES ROM可以在WAMR字节码解释器上达到实时帧率。

## 板测坐标与历史像素基线

等待标题CRC后触摸。每次点击后等待至少三张新的完整像素帧，再发下一步；序号推进与CRC变化分开看，墙壁/空闲时CRC可以不变。

| 操作 | 触摸中心 | 预期 |
|---|---|---|
| START / RESET | (326,365) | 开始或回到(1,1) |
| UP | (233,365) | 向上 |
| LEFT | (183,405) | 向左 |
| DOWN | (233,405) | 向下 |
| RIGHT | (283,405) | 向右 |
| 宿主返回 | (233,50) | 由产品UI停止并退出应用 |

以下是 v3 的像素基线，v4 实机应重新采样确认。CRC为标准CRC32，覆盖 `frame.pixels` 的全部122880字节RGB565LE数据，不包含宿主控件。标题=`a742dc95`；开始/重开/初始撞上墙=`7a1a234c`；右一次=`059ce89c`；右两次=`92549c97`；胜利=`7348876c`。

重开后完整路线为 `RRRDDRRUURRRRDDDDDDD`（20步）。每步坐标、玩家位置和经过真实WAMR采样并独立比较的CRC见 [board-oracle.json](board-oracle.json)。真实runner确认版本3首次像素在第429次CLOCK事件；稳定无输入帧每5次事件完成一帧。实际秒数取决于设备调度与执行成本，须上板测量。

产品的 `CONFIG_TINYRT_PERFORMANCE` 和 `CONFIG_TINYRT_TEST_CONSOLE` 默认关闭；专项板测须显式开启，发布时关闭。回调耗时、计数器和上述桌面结果不能直接换算为整机 FPS。
