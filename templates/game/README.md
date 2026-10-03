# TinyRT game application

Version: 0.0.1.

Hold the screen to move the pixel character; release to stop. Use host back to exit.

## Build a website ZIP

From the SDK directory (replace `<app>` with this application directory):

```text
python tools/tinyrt.py release <app> --cc path/to/zig.exe --runner path/to/tinyrt-run.exe --development-key
```

Edit listing.json, this README, CHANGELOG.md, LICENSES.md and cover.png for your application.
The template cover is a geometric placeholder. The default package is Wasm.

## Known limitations

Desktop preview is not device verification. Code and media rights have not been confirmed.
A matching TinyRT development host is required.
