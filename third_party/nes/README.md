> 历史依赖记录：NES 示例及其适配源码已退役，下文路径只描述历史版本。保留上游来源与许可，不作为当前示例构建说明。

# 固定 NES 核心来源

上游：[PeakRacing/nes](https://github.com/PeakRacing/nes)。固定 revision：[`638096ae00d258700779be2af06478d1be5bf8a1`](https://github.com/PeakRacing/nes/tree/638096ae00d258700779be2af06478d1be5bf8a1)。许可：[Apache-2.0](https://github.com/PeakRacing/nes/blob/638096ae00d258700779be2af06478d1be5bf8a1/LICENSE)，本目录保留其完整 [LICENSE](upstream/LICENSE) 和源码版权声明。

`source.json` 记录官方 revision、官方归档 SHA256 和每个保留文件的 SHA256。`upstream/` 仅保留 CPU、PPU、渲染循环、公共头、LICENSE、README 共 16 个文件，字节与固定上游一致。未引入上游平台端口、测试可执行文件或 ROM。构建时 `verify_source()` 逐个验证这些文件。

## 适配范围

- [原始主循环](https://github.com/PeakRacing/nes/blob/638096ae00d258700779be2af06478d1be5bf8a1/src/nes.c)：`nes_run()` 为阻塞帧循环。TinyRT 的 `examples/nes/engine.c` 保留其扫描线 CPU/PPU 渲染和滚动逻辑，改为有状态、一次一条扫描线的主动 step；添加分片清零、固定 NROM 连接和逐行 CRC。该文件保留上游 Apache 版权及修改说明。
- [原始 CPU](https://github.com/PeakRacing/nes/blob/638096ae00d258700779be2af06478d1be5bf8a1/src/nes_cpu.c)：原 256 分支 switch 经 LLVM 编译后超过现有 8 KiB WAMR 解释器栈。`adapt_cpu.py` 生成 16 个禁止内联的分派组，每组 16 个原始 case；256 个 opcode case 正文、周期计算及其调用实现保持不变。生成文件保留原版权，增加修改说明。脚本验证 case 完整性并拒绝来源哈希变化。
- [原始 PPU](https://github.com/PeakRacing/nes/blob/638096ae00d258700779be2af06478d1be5bf8a1/src/nes_ppu.c) 直接编译。上游渲染器通过源码 include 使用，未调用其阻塞循环。未修改原文件或 TinyRT 的栈/预算上限。
- `examples/nes/nes_conf.h` 关闭声音、文件系统、存档和 mapper 扩展；`port.c` 提供无系统调用的内存操作；固定 ROM 和静态状态不需要动态分配。

这里只证明原创 NROM fixture 所覆盖的 CPU/PPU 行为。未运行完整 CPU 一致性套件、通用 ROM/mapper 测试或商业游戏。分派改写保持源码 case 正文，但没有据此宣称全部 256 个 opcode 都通过动态验证。
