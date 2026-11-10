# Saved palette

Tap to rotate the three RGB channels. After a normal exit and restart the
application restores the last color. TinyRT API Level 4 supplies one blob of
up to 2048 bytes per application. This example uses three bytes in RGB order.

On S31, KV and the blob share one application record in the existing 64 KiB
`app_data` NVS partition. Sixteen applications with full blobs use 32 KiB of
payload plus 1664 bytes of KV records before NVS overhead. The remaining space
allows NVS to copy records during updates. Updating the application preserves
its state. Uninstalling removes the entire record.
