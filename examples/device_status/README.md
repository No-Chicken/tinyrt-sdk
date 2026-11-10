# Device status

Shows local time and the latest battery percentage. Dashes indicate that the
clock or battery sample is unavailable. Tap to switch between 70% brightness
with keep-on requested and 35% brightness with normal idle behavior. The host
restores its configured brightness on exit. Low battery protection takes priority.

Build and preview with `python tools/tinyrt.py build examples/device_status` and
`python tools/tinyrt.py run examples/device_status`. Requires ABI 1, API Level 3.
