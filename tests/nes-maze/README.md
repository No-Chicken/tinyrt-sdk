# NES Maze 验证

验证通过真实 CPU/PPU 和真实 TinyRT WAMR 进行。没有硬件访问；桌面单回调耗时、内存跟踪值与板上帧率分别记录。

## 复现

从 SDK 根目录执行：

```powershell
python examples/nes-maze/build.py --cc <zig路径> --native
examples/nes-maze/build/nes-maze-native.exe examples/nes-maze/build
python examples/nes-maze/build.py --cc <zig路径> --development-key --key-id 1
```

在 MSVC x64 开发环境中编译真实 WAMR runner，传入 core 要求的固定 WAMR checkout：

```powershell
cmake -S tests/nes-maze -B tests/nes-maze/build -G Ninja -DTINYRT_ROOT=<core绝对路径> -DWAMR_ROOT_DIR=<wamr绝对路径> -DNES_MAZE_WASM=<nes-maze.wasm绝对路径> -DCMAKE_BUILD_TYPE=Release
cmake --build tests/nes-maze/build
ctest --test-dir tests/nes-maze/build --output-on-failure
tests/nes-maze/build/test_nes_maze_wamr.exe examples/nes-maze/build/nes-maze.wasm 100000 260 examples/nes-maze/build
python tests/nes-maze/test_oracle.py --captures examples/nes-maze/build --png -v
python tests/nes-maze/budget_scan.py --runner tests/nes-maze/build/test_nes_maze_wamr.exe --wasm examples/nes-maze/build/nes-maze.wasm --output tests/nes-maze/budget-scan.json
```

`--png` 需要 Pillow，仅将实际RGB565像素转换为PNG。runner参数依次是Wasm、预算、每个实例额外空闲帧数（0..260）、可选采样目录。

## 证据

- `native.c`：69项断言，直接检查ROM CPU RAM的初始状态、Start、碰墙、同方向重复移动、保持键不连跳、20步通关、胜利后不再移动、重开、NMI计数回绕和实例重置。
- `test_wamr.c`：通过实际ABI事件点击屏幕按钮，检查画面中的玩家位置、单项输入队列、队列满时不覆盖、无效区域触摸、完整路线、退出及两次实例创建/销毁。每次事件/渲染均受原100000预算控制；停止调用可选 `tinyrt_stop`；销毁后受跟踪内存回到基线，关闭runtime后为0。
- `test_oracle.py`：独立构造预期nametable、解码ROM CHR图块，比较native与WAMR标题、行走、胜利、重开八张完整画面的全部61440像素；核验两次vblank暖机、控制器读取、向量与原创地图。另对WAMR完整路线每步采样作逐像素核验，导出 `board-oracle.json`。
- [artifact.json](artifact.json)：ROM/Wasm/签名包大小、哈希和实际模块导入/导出。`preview.png` 来自native真实帧。
- [budget-scan.json](budget-scan.json)：完整有限路线的最低passing budget=63592，63591失败。正式manifest保持100000。扫描结果仅覆盖此ROM、此有限输入轨迹；不构成任意ROM、sprite或全部6502 opcode的最坏上界。
- `red-native.txt`、`red-wamr.txt` 记录先失败的行为；`green-native.txt`、`green-wamr.txt` 记录最终通过结果。

最终长轨迹包含两个完整通关实例，各再空闲260帧。真实WAMR峰值跟踪内存488648字节（包括模块、线性内存和宿主frame，不是进程RSS或板上总RAM）。桌面耗时包含加载、建实例、测试检查及采样文件开销，不能换算成ESP32-S3帧率。

## 板测坐标与判定

等待标题CRC后触摸。每次点击后等待至少三张新的完整像素帧，再发下一步；序号推进与CRC变化分开看，墙壁/空闲时CRC可以不变。

| 操作 | 触摸中心 | 预期 |
|---|---|---|
| START / RESET | (326,365) | 开始或回到(1,1) |
| UP | (233,365) | 向上 |
| LEFT | (183,405) | 向左 |
| DOWN | (233,405) | 向下 |
| RIGHT | (283,405) | 向右 |
| 宿主返回 | (233,50) | 由产品UI停止并退出应用 |

CRC为标准CRC32，覆盖 `frame.pixels` 的全部122880字节RGB565LE数据，不包含宿主控件。标题=`a742dc95`；开始/重开/初始撞上墙=`7a1a234c`；右一次=`059ce89c`；右两次=`92549c97`；胜利=`7348876c`。

重开后完整路线为 `RRRDDRRUURRRRDDDDDDD`（20步）。每步坐标、玩家位置和经过真实WAMR采样并独立比较的CRC见 [board-oracle.json](board-oracle.json)。真实runner确认首次像素在第524次CLOCK事件；之后每88次事件完成一帧。实际秒数取决于设备调度与执行成本，须上板测量。
