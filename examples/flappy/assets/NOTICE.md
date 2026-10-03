# Classic Flappy Bird asset provenance

This development demo reproduces the classic artwork requested for evaluation.
It is an unofficial port, not affiliated with Dong Nguyen or .GEARS.

- Sprites and WAV sound effects: https://github.com/samuelcust/flappy-bird-assets
  (downloaded 2026-10-03, master archive). The repository supplies an MIT license,
  retained as `LICENSE-samuelcust.txt`. Its license does not independently prove
  ownership of the original game's artwork or sound recordings.
- Original sprite atlas (score panel, OK, NEW, medals, ready/game-over labels):
  https://gist.github.com/allenluce/2002be29f52c5848352a9cf8488472be
  PNG revision `e6d3bb73ab0da34cfdc92d37addaf27fa87864b1`.
  The gist provides no separate asset license. The atlas carries `.GEARS 2013`.
- Supporting provenance: https://github.com/nebez/floppybird#notice
  explicitly attributes the original visual assets to Dong Nguyen and .GEARS,
  and states that its author did not obtain explicit permission.

The repository code licenses must not be advertised as a commercial license for
the original game art. Website metadata therefore marks asset clearance as
unverified. Keep this notice with all evaluation packages and screenshots.

`prepare_assets.py` converts these files into RGB565 scanline spans and bounded
16 kHz mono PCM16 effects. The cover and screenshots use these same assets;
they are not separate concept art. No code was copied from the reference sites.
