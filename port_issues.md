# Performance and diagnostics

[Polski](port_issues.pl.md) · [Project overview](README.md)

Use this guide to compare renderer changes on the same hardware and scene.
The historical observations below are not measurements of the current build.

## Current rendering paths

The active port uses the official Pico SDK through JaszczurHAL. Platform code
is in `src/jaszczurhal/`; the old `src/pico` backend is outside the active tree.

`hal_project_config.h` selects `DOOM_DUAL_CORE_COLUMNS=0`. The optional
post-BSP batch path draws the left side on core0 and submits right-side work
to core1 after BSP completes. Streaming columns during BSP remains an
experiment requiring data isolation.

Core1 handles asynchronous TFT transfer. RP2350 ILI9341 uses two indexed
framebuffers; single-buffer configurations wait before reusing a buffer.
The RP2350 path also enables `DOOM_RENDER_ASYNC_HUD`, with a barrier before
the next game tick. `DOOM_RENDER_ASYNC_PLANES=0` keeps planes on the default path.
`DOOM_VIDEO_SYNC_FLUSH=1` helps separate transfer problems from rendering problems.

## Reading the counters

| Field | Meaning |
| --- | --- |
| `fps` | Frame-rate estimate |
| `tus=bsp/planes/masked/pspr/hud` | Frame work in microseconds |
| `cols`, `planes`, `masked` | Wall, plane and masked rendering workload |
| `pdrop`, `texfail`, `flatfail` | Queue drops and texture/flat failures |
| `pcache=hits/misses/tall_uncached/tall_hits` | Column cache effectiveness |
| `tall`, `tallh` | Tall-column details when present in the log |
| `black=total/top/mid/bot` | Pixels matching the undrawn sentinel |
| `flush=done/waits/wait_ms` | TFT jobs and waits |
| `casync=queued/done/wait_ms` | Column helper jobs and waits |
| `ccol=queued/right/inline/core1` | Column work distribution |
| `free_heap` | Remaining heap budget |

`DOOM_RENDER_SENTINEL_CLEAR` and `DOOM_RENDER_BLACK_DIAG` default to `0` in
high-resolution gameplay. A zero `black` count with scanning disabled does
not establish that a frame has no artifacts. Even when enabled, the counter
finds sentinel-colored pixels, not every possible rendering error.

## Column cache and memory

The compact decoded-column cache uses hash chains for exact lookup and a
linked LRU list for replacement. Collisions retain every cached key, hits
update LRU order, and insertion follows successful decoding. Each core owns
its index and pixel slots, including queued mode. Tall columns use a separate
transient cache. The index avoids full-cache scans without reducing capacity.

Host tests compare hits, misses and chosen slots with the previous LRU
implementation, including collisions and independent owners:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
```

For a synthetic lookup/replacement benchmark:

```bash
mkdir -p .build
cc -std=c17 -O2 -Isrc tests/doom_column_cache_test.c -o .build/column-cache-test
.build/column-cache-test --benchmark
```

This excludes decoding, drawing and TFT transfer. Measure device FPS and
`tus=bsp` to evaluate gameplay. Cache growth competes with the Doom zone and
HAL buffers for SRAM. Keep `free_heap` and startup allocation failures in view;
a larger cache alone is not evidence of a faster or usable build.

## Historical observations

These observations predate the current cache index. Keep board, resolution,
clocks, map position, input and audio configuration identical when comparing.

- After SPI and system-clock changes, recorded gameplay was around 10–13 FPS;
  wall/BSP work dominated and TFT transfer was no longer the main bottleneck.
- Simple plane offload brought little gain because core0 still waited before
  `R_DrawMasked()`. Post-BSP column batching also brought a small gain because
  of waits and work falling back to inline rendering.
- Right-side streaming during BSP reduced observed `tus=bsp` from about
  43,000 to 30,000 µs and reached about 13 FPS in light scenes, but caused
  black artifacts and `R_GetColumn: fd->real_id >= 0` assertions.
- Concurrent access to renderer state, framedrawables, WAD/WHD/zone caches or
  textures is a suspected cause, not an established diagnosis. Per-core
  decoders and caches alone did not make streaming safe.

## Next experiments

1. Establish a default single-core baseline with matching light and heavy
   map views. Compare post-BSP batching separately and keep clocks, resolution
   and renderer flags fixed. The requested TFT clock comes from `frequencyHz`
   of `spi` in `device_tree.json`, via `JH_ILI9341_SPI_DEFAULT_HZ` and
   `JH_ST77XX_SPI_DEFAULT_HZ`; actual frequency depends on peripheral divisors.
2. Profile texture decoding before enlarging caches. Track `tus=bsp`, `cols`,
   `pcache` and `free_heap`. Check whether splitting cache ownership reduces
   hit rate and whether `tall_uncached` justifies a different tall-cache size.
3. Isolate core1 data before restoring streaming. Try a small predecoded
   texture set; log texture ID, `real_id` or lump, height, offset and x for
   submitted columns. Check for lazy loading, zone allocation and composite
   texture creation. Prefer ready pointers or predecoded data prepared by
   core0, with separate cache/scratch storage and explicit barriers.
4. Limit a streaming experiment to `x >= DOOM_COL_SPLIT_X` (half of
   `SCREENWIDTH`, not a fixed panel coordinate). Define FIFO-full behavior
   before allowing inline fallback; preserve ordering and prevent drawing the
   same work twice. Keep diagnostic drops or waits visible.
5. Once streaming is stable, compare cache size, hashing, repeated-texture
   policy and tall-cache limits. Revisit plane and sprite work when their
   measured time warrants it. With `texfail=0` and `pdrop=0`, investigate plane
   ordering, clipping and synchronization rather than assuming transfer failure.

Accept a performance change only after longer device runs without fatal
assertions or new visible artifacts, with `texfail=0`, `flatfail=0` and
`pdrop=0`, lower comparable `tus=bsp`, and a sustained FPS gain across scenes.
Rebuild and refresh IntelliSense as described in [the build guide](docs/BUILDING.md).
Keep logs and configuration with each comparison; host benchmarks do not
replace device measurements.
