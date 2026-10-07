# TinyRT game application

The default clock is 33 ms (~30 Hz). Each CLOCK updates the game and the next render submits one complete frame. Skip unchanged frames; request a different interval explicitly when a game needs a higher rate. Device display throughput still limits the visible frame rate.

Version: 0.0.1.

Hold the screen to move the pixel character; release to stop. Use host back to exit.

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
