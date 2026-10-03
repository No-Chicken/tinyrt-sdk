# TinyRT minimal application

Version: 0.0.1.

A greeting rendered on the 466x466 round display. Use host back to exit.

## Build a website ZIP

From the SDK directory (replace `<app>` with this application directory):

```text
python tools/tinyrt.py release <app> --cc path/to/zig.exe --runner path/to/tinyrt-run.exe --wamrc path/to/wamrc.exe --development-key
```

Edit listing.json, this README, CHANGELOG.md, LICENSES.md and cover.png for your application.
The template cover is a geometric placeholder. The default delivery package is ESP32-S3 AOT, with Wasm retained for fallback.

## Known limitations

Desktop preview is not device verification. Code and media rights have not been confirmed.
A matching TinyRT development host is required.
