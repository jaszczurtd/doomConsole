# C8 Bluetooth Gamepad Hardware Gate

Run this gate on `rp2350-arm:pico2w` with an 8BitDo Zero 2 in Android D-input
mode. The host tests and firmware build do not replace this physical gate.

## Preparation

1. Build and flash the default project manifest and the WHX payload.
2. Connect all nine active-low GPIO buttons described in `hardware_layout.md`.
3. Start the persistent serial monitor and save the complete log.
4. With `BT_AUTOMATIC_PAIRING=1`, confirm the 120-second pairing message after
   startup, then start the Zero 2 with `B+Start` and press `Select` to pair.
   Repeat with the flag set to `0` and physical `Menu+Back` held for three
   seconds before accepting the final hardware gate.
5. Before every reconnect, hold one pad control until the connection becomes
   active. Confirm that Doom remains idle until that control is released and a
   neutral report has been received.

## Required checks

- Navigate the menu and start a game with the pad.
- Verify forward/backward movement, left/right turning, fire, use, menu/pause,
  accept, back, both `Y` + D-pad strafe directions, and direct `L`/`R` strafe
  with the mapping in `hardware_layout.md`.
- Repeat the same flow using only GPIO, both with the pad connected and after
  it has disconnected.
- Play continuously for 30 minutes.
- Perform 20 controller power-off/power-on reconnects in the same firmware
  runtime.
- Disconnect once while holding each mapped pad control, including both strafe
  chords and both shoulder buttons. Each case must produce a release with no
  stuck movement, turn, strafe, fire, use, or menu key.
- Measure report-to-event latency over at least 200 transitions. Required:
  p95 at most 29 ms and maximum at most 58 ms.
- Compare the `Y` + D-pad and direct `L`/`R` strafe mappings and record the
  final ergonomic choice.

Do not mark C8 complete without attaching the serial log, latency data, exact
firmware commit, board and controller identity, elapsed play time, reconnect
count, per-control disconnect results, and final mapping.
