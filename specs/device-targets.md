# Device AOT targets

Guest source and ABI remain shared. Native AOT code must be compiled for the
device architecture; an ESP32-S3 Xtensa AOT section cannot execute on ESP32-S31.

Development `pack --aot --development-key` supports:

- `--target esp32s3` (default), the existing pinned Xtensa compiler/profile.
- `--target esp32s31`, the pinned RISC-V compiler/profile in
  `tools/toolchains/esp32s31.json`; use the bundled Windows x64 compiler at
  `tools/toolchains/esp32s31/windows-x64/wamrc.exe` with explicit `--wamrc`.
  See the [compiler bundle](../tools/toolchains/esp32s31/windows-x64/README.md)
  for provenance, licenses, checksum verification and website integration.

```powershell
python tools/tinyrt.py pack examples/flappy --wasm examples/flappy/build/demo.sky-hop.wasm --output examples/flappy/build/demo.sky-hop-s31.trpkg --aot --development-key --target esp32s31 --wamrc tools/toolchains/esp32s31/windows-x64/wamrc.exe
```

The S31 target uses RV32 with M/A/C/F features and **ilp32f single-precision hard-float calling
convention**, matching its ESP-IDF toolchain. Bounds, native stack and loop-poll
checks remain mandatory. `--size-level=3` selects LLVM's supported small code
model; it does not remove those checks. Target/options/compiler digests must
match the firmware compatibility profile. The package still contains source
Wasm for authentication, but S31 bring-up firmware requires AOT to start games.

Current website release ZIP schemas/pipeline remain ESP32-S3-specific. This
change enables S31 development package delivery and bench verification only;
production S31 signing and website variants require separate release work.
