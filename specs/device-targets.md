# S31 AOT target

Magie runs RISC-V AOT code. The SDK uses the pinned compiler in `tools/toolchains/esp32s31.json`; use the bundled Windows x64 compiler at
  `tools/toolchains/esp32s31/windows-x64/wamrc.exe` by default.
  See the [compiler bundle](../tools/toolchains/esp32s31/windows-x64/README.md)
  for provenance, licenses, checksum verification and website integration.

```powershell
python tools/tinyrt.py pack examples/flappy --wasm examples/flappy/build/demo.sky-hop.wasm --output examples/flappy/build/demo.sky-hop-s31.trpkg 
```

The S31 target uses RV32 with M/A/C/F features and **ilp32f single-precision hard-float calling
convention**, matching its ESP-IDF toolchain. Bounds, native stack and loop-poll
checks remain mandatory. `--size-level=3` selects LLVM's supported small code
model; it does not remove those checks. Target/options/compiler digests must
match the firmware compatibility profile. The package still contains source
Wasm for rebuild checks, but Magie requires a compatible AOT section at installation.

`release --target esp32s31` builds the website v2 ZIP with
the same S31 profile; without `--wamrc` it uses the bundled compiler above.
