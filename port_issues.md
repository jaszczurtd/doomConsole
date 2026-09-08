# DoomConsole Performance Context

This file is the current working context for further Doom performance work on
JaszczurHAL.

## Current State

- The active port builds through JaszczurHAL's native RP dispatcher and the
  official Pico SDK.
- The active legacy `src/pico` directory has been removed; files still needed
  by the port now live under `src/jaszczurhal`.
- The manifest and CMake default to `DOOM_DUAL_CORE_COLUMNS=0`.
- The safe dual-core column variant is currently the post-BSP batch path: core0
  draws the left side, and core1 receives the right side after the BSP pass
  completes.
- Producer/consumer column streaming during BSP produced a visible FPS gain,
  but also caused black artifacts and a fatal assert in `R_GetColumn`, so it is
  not safe as the current path.
- Async TFT flush runs on core1. RP2350 ILI9341 builds use two framebuffers;
  single-buffer builds wait before reusing the framebuffer.

## Historical Measurements And Conclusions

These observations predate the current cache index and are not a measurement
of the current firmware. Compare builds with the same board, resolution,
clocks, map position, and input/audio configuration.

- After unlocking SPI and raising clocks, TFT transfer stopped being the main
  bottleneck. Gameplay usually sits around 10-13 FPS depending on the scene.
- The main cost remains wall/BSP rendering and column handling:
  `tus=bsp` can dominate the frame.
- Simple plane rendering offload to core1 did not produce a meaningful gain,
  because core0 still has to wait before `R_DrawMasked()`.
- Post-BSP dual-core column batching does wake core1, but the gain is small
  because core0 often waits for the job to finish and some work still falls
  back to inline rendering.
- Right-side column streaming during BSP confirmed the potential:
  in light measurements, `tus=bsp` dropped from around 43k us to around 30k us,
  and FPS rose to about 13.
- The same streaming path also showed instability: black glitches and the
  assert `R_GetColumn: fd->real_id >= 0`. The most likely cause is concurrent
  access from core0/core1 to global renderer state, WAD/WHD/zone cache,
  framedrawables, or texture structures.
- The per-core patch decoder and per-core column cache are necessary, but are
  not sufficient on their own to make streaming safe.

## Open Performance Issues

1. Column rendering is still the main bottleneck.
   Further work needs to target `tus=bsp`, `cols`, `pcache`, and wall texture
   decode cost.

2. Safe dual-core column rendering requires data isolation.
   Core1 must not touch global structures mutated by core0 during BSP unless
   their stability and independent cache ownership are guaranteed.

3. Column cache effectiveness may drop after splitting it per core.
   Watch `pcache=hits/misses/tall_uncached/tall_hits` and any hit-rate drop
   after enabling dual-core rendering.

4. Tall-cache still needs validation.
   If `tall_uncached` keeps growing in heavy scenes, decide whether to enlarge
   or reorganize tall-cache, or accept the cost as less important than core1
   synchronization.

5. Async TFT flush can hide or reveal artifacts.
   When black glitches appear, separate renderer problems from transfer
   problems. Useful diagnostics are sync-flush tests and the `black=`,
   `flush=`, and `casync=` counters.

6. Rendering artifacts remain a separate risk.
   With `texfail=0` and `pdrop=0`, likely candidates are visplane ordering or
   organization, clipping, core0/core1 synchronization, or reads from renderer
   data that has not been stabilized.

## Counters To Watch

- `fps`: the main user-visible result.
- `tus=bsp/planes/masked`: render-time breakdown.
- `cols`: number of wall columns in the frame.
- `planes`, `pdrop`: plane cost and plane queue overflows.
- `masked`: sprite/masked draw cost.
- `pcache=hits/misses/tall_uncached/tall_hits`: column cache effectiveness.
- `tall`, `tallh`: tall-cache details, when enabled in the log.
- `black=total/top/mid/bot`: undrawn pixels / black glitches.
- `flush=done/waits/wait_ms`: TFT flush cost and wait time.
- `casync=queued/done/wait_ms`: dual-core column work.
- `ccol=queued/right/inline/core1`: column work distribution between cores.
- `free_heap`: quick RAM budget check.

## Column Cache

The compact decoded-column cache uses hash chains for exact lookup and a linked
LRU list for replacement. Hash collisions retain every cached key. Hits update
the LRU order, and insertion happens only after successful decoding. Each core
owns its index and pixel slots; this also applies to the queued column mode.
The index keeps the configured capacity and avoids full-cache lookup and
replacement scans. Tall columns retain their separate transient cache.

Host regression tests compare hits, misses, and selected pixel slots against
the previous LRU implementation, including collisions and independent owners:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
```

To compare cache lookup and replacement costs on the host:

```bash
cc -std=c17 -O2 -Isrc tests/doom_column_cache_test.c -o .build/column-cache-test
.build/column-cache-test --benchmark
```

This synthetic benchmark excludes texture decoding, drawing, and TFT transfer.
Measure gameplay FPS and `tus=bsp` on the device to assess the frame-rate gain.

## TFT Clock

`JH_ILI9341_SPI_DEFAULT_HZ` in the manifest selects the requested clock for both
supported TFT families. The recipe passes it to the HAL library as
`JH_ILI9341_SPI_DEFAULT_HZ` and `JH_ST77XX_SPI_DEFAULT_HZ`; the application
inherits the same definitions. Defining these only on the firmware target
leaves the separately compiled HAL drivers at their default clocks and makes
the boot-time estimate misleading. The actual SPI rate also depends on the
peripheral clock and available divisors.

## Performance Continuation Plan

### 1. Establish A Stable Baseline

- Start with the manifest's default single-core columns. Compare queued
  dual-core columns separately, without streaming during BSP.
- Collect logs from the same map locations for several heavy and light frames.
- Compare mainly `fps`, `tus=bsp`, `pcache`, `ccol`, `casync`, and `black`.
- Keep resolution, clocks, and renderer flags identical between cache builds.

### 2. Diagnose What Exactly Breaks Streaming

- Do not restore full streaming as the default path.
- Add a diagnostic streaming mode that limits core1 to predecoded data or to a
  minimal texture set, to check whether the crash comes from
  `R_GetColumn`/framedrawable/WAD cache access.
- For each column handed to core1, record a stable texture identifier, real_id
  or lump, height, offset, and x. On assert, log the last core1 column.
- Check whether core1 enters code that may allocate in zone, invalidate cache,
  or lazily build a composite texture while streaming.

### 3. Prepare A Safe Data Model For Core1

- Preferred direction: core0 prepares a column description during BSP that is
  independent from global lookups, while core1 only performs pure draw/decode
  work from ready pointers or predecoded data.
- If full isolation is too expensive in RAM, use a middle step:
  core0 prefetches or forces materialization of texture columns before handing
  work to core1, and core1 does not call lazy-load paths.
- Keep cache and decode scratch data per-core. No cache structure used by core1
  should be mutated by core0 without a barrier.

### 4. Reintroduce Streaming In A Small Scope

- Start by streaming only the right side of the screen, `x >= 160`.
- Keep core0 inline rendering for the left side.
- When the FIFO is full, do not render the same right-side column inline until
  there is a clear ordering model; a diagnostic drop or wait is preferable to
  double-rendering.
- Test with extra asserts and counters first, not immediately as the
  performance path.

### 5. Optimize Column Cache After Stabilization

- Only after dual-core rendering is stable, check whether per-core cache
  splitting eats the gain.
- If `pcache misses` remain high, consider:
  - a larger compact-column cache,
  - a better hash or hint,
  - a separate policy for repeated wall textures,
  - tuning tall-cache to the real heights seen in logs.
- Measure every change in the same map location, because scene differences can
  easily obscure the result.

### 6. Touch Plane/Sprite Work Later

- Plane async has already been tested and did not produce a large gain, so it
  is not the current priority.
- Optimize sprite/masked rendering only once `tus=masked` starts dominating.
- If `planes` grows extremely high, inspect visplane organization and flush
  count, but do not mix that with column work.

## Success Criteria For The Next Major Change

- No fatal asserts during a longer runtime test.
- `black=` does not show new large spikes compared with baseline.
- `texfail=0`, `flatfail=0`, and `pdrop=0` remain true.
- `tus=bsp` drops in comparable scenes.
- FPS rises by at least a few stable frames, not only in one favorable view.
- Build firmware and refresh the compile database through
  `../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project .` and
  `../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project .`.

## Nearest Concrete Step

Profile texture decoding in heavy scenes with the configured TFT clock applied
to the HAL drivers. Keep matching `fps`, `tus=bsp`, cache hits/misses, and
failure-counter logs for future comparisons. Column streaming still requires
ready data that avoids `R_GetColumn`, lazy lookups, and global WAD/WHD/zone cache
access on core1 during BSP.
