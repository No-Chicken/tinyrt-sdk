# NES N1：真实 CPU/PPU 分片实验

N1 在 Wasm guest 内运行固定的 6502 CPU/PPU 核心和原创 NROM 测试程序。界面只显示帧号、帧 CRC、NMI 计数、A 状态和算术签名；这是计算正确性与运行预算实验，尚未实现可玩的 NES 应用。

- 固定核心：[PeakRacing/nes](https://github.com/PeakRacing/nes/tree/638096ae00d258700779be2af06478d1be5bf8a1)，Apache-2.0；[来源和适配说明](../../third_party/nes/README.md)。
- `make_rom.py` 生成原创 24,592 字节 NROM-128 ROM，不使用商业 ROM。生成器和生成的 ROM 采用 [MIT 许可](ROM-LICENSE)。
- ABI 1；permissions=11（绘制、触摸、时钟）；memory_pages=16；每次 init/event/render 的预算为 100000。Wasm 线性内存初始 256 KiB、最大 1 MiB。
- 静音、无 WASI、无文件访问；ROM 编译进 Wasm。仅接受这个固定测试 ROM，不提供通用 ROM 导入或 mapper 支持。

## 构建与打包

从 SDK 根目录执行。`<zig>` 为现有 Zig 0.13.0 可执行文件路径；构建过程无下载或全局安装。

```powershell
python examples/nes/build.py --cc <zig>
python examples/nes/build.py --cc <zig> --native
examples/nes/build/nes-native.exe
python tests/nes/test_oracle.py
python tools/tinyrt.py pack examples/nes --wasm examples/nes/build/nes.wasm --output examples/nes/build/nes-n1.trpkg --key-id 1 --development-key
python tools/tinyrt.py validate examples/nes/build/nes-n1.trpkg --key-id 1 --development-key
```

NES 使用私有 `build.py` 完成来源校验、ROM 生成、CPU 分派生成和第三方编译；当前公共 `tinyrt.py build` 不支持这些步骤。`app.json` 供统一打包命令读取。公开开发键仅用于明确启用该信任配置的测试设备。

构建先校验 16 个固定上游文件的 SHA256，再生成 ROM 和适配后的 CPU 源码。输出均位于忽略的 `build/`，不会写回上游源码。详见[实际 WAMR 测试](../../tests/nes/README.md)。

## ROM 和已知预期

程序在 reset 后计算 `$10 + $23` 并把 `$33` 写入 CPU RAM `$02`，通过 `$2006/$2007` 设置图案和调色板。NMI 增加 `$00`，通过 `$4016` 读取 A 到 `$01`，更新下一帧方块颜色。CHR tile 1 的低位平面为全 1、高位平面全 0。画面是 256×240 黑底，在 `(64,64)` 至 `(127,127)` 有 64×64 实心方块。

CRC 为整帧按行排列的 **little-endian RGB565** 字节的标准 CRC32。预期从独立矩形模型计算，不从模拟器输出反推：

| 稳态画面 | RGB565 | CRC32 |
|---|---:|---:|
| 蓝色，完成的偶数帧 | `20D1` | `7FDD3027` |
| 浅蓝，完成的奇数帧 | `3DFF` | `EE67C189` |
| A 被锁存为按下，经过 NMI 生效后 | `FFFF` | `7AC3EB7F` |

验收在第 8 帧开始，避开程序写入 VRAM 的启动帧。第 8 帧应为 `NMI 8 A 0 SIG 51`、`CRC 7FDD3027`。主机测试核验所有 61,440 个像素、帧/CRC 同时发布、A 改色/释放、实例重建以及第 260 帧的 NMI 8 位回绕。

## 分片和交互

`n1_start()` 只重置续执行状态；随后每次时钟事件清零最多 512 字节，完成后仅 reset CPU 一次。每次主动 `n1_step()` 执行一条扫描线并返回，续存 CPU、PPU、扫描线和分数周期；不调用阻塞的上游 `nes_run()`，也不依赖预算异常恢复。

每帧为 262 次 step。CRC 每次处理一行，在整帧完成时与帧号一起发布。触摸 release 切换测试用 A 锁存状态，ROM 在下一个 NMI 中读取它；界面显示的是 ROM 实际读回状态，因此可能延迟一帧以上。这不代表已实现按下/松开连续手柄输入。

在当前设备 100 ms 时钟事件周期下，初始化约 25.4 秒，每个模拟帧约 26.2 秒，第 8 帧约 235 秒。这是调度频率对应的时间估算，不是板上性能测量。桌面 WAMR 耗时也不能换算成 ESP32-S3 帧率。

下一阶段需要板上逐片耗时与内存测量，再决定调度、受控像素传输和完整输入。音频、真实游戏兼容性、mapper、通用 ROM 资源读取和实时画面均未验收。
