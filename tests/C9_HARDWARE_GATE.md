# C9 Persistent Gamepad Bond Hardware Gate

Run this gate on `rp2350-arm:pico2w` with an 8BitDo Zero 2 in Android D-input
mode. The current console has no physical buttons, so use the default
buttonless build (`BT_AUTOMATIC_PAIRING=1`) and keep a complete serial log. Do
not publish a full Bluetooth address or link key in the result.

## Preparation

1. Build and flash the default firmware, then upload the WHX payload. Confirm
   that both the build and uploader reserve the final 8192 flash bytes for KV.
   When another RP board is connected in BOOTSEL, select the Pico 2 W with the
   uploader's `--serial` option.
2. Start the first run with no stored bond and the Zero 2 powered off. For a
   repeated run, complete the controller factory-reset step below first.
3. Reboot and confirm exactly one bounded pairing window is opened because the
   bond is empty. Do not put any controller into pairing mode yet.
4. Save the firmware revision, board identity, controller mode and timestamps.
   Identify the controller only by a short operator-assigned label.

## Required checks

1. In the already-open 120-second window, pair the Zero 2 started with
   `B+Start` and `Select`.
2. Exercise menu navigation and one in-game action after the neutral-input
   gate. Confirm that the accepted peer is persisted only after the usable HID
   connection is established.
3. Power the controller off and on. Confirm automatic reconnect without a new
   pairing window and without a stuck input.
4. Choose **Quit Game** in Doom. This reaches `I_Quit()` and performs a
   watchdog reboot. Confirm automatic reconnect after boot without pairing.
5. Remove power from the Pico 2 W, wait at least five seconds, restore power,
   and confirm the same reconnect behavior after the cold boot.
6. While pairing is closed, expose XY-BT and the Zero 2 in another persona.
   Confirm that neither changes persistent identity or creates a repeated
   connect/disconnect loop. Restore Android D-input mode and confirm the saved
   persona can still reconnect.
7. Hold the connected pad's `L+R` combination, add `Select`, and keep all three
   pressed for five seconds. (The reset chord deliberately excludes `Start`,
   whose long-press powers the Zero 2 off.)
   Confirm a generic `persisted bond erased` message, disconnection, no
   reconnect, and no new pairing window in that runtime. Power the controller
   off, reboot the console and confirm one new empty-bond pairing window. Start
   the old controller normally, without `Select`, and confirm that it cannot
   reconnect using the erased bond.
8. Put the Android D-input persona into pairing mode with `Select`. Confirm that
   normal input and a later reconnect both work.

## Evidence and release audit

- Keep the complete serial log, but redact it before sharing if any external
  component prints a full device address.
- Reject the run if the log contains a six-octet Bluetooth address, link-key
  bytes, bond blob bytes or secret material.
- Record each reconnect result, watchdog boot, cold boot, rejected persona and
  factory-reset result. Also record that the WHX payload remained readable
  after all KV writes.
- Run the shared RP KV power-loss fixture and the OTA+KV+LittleFS hardware
  fixture separately; both are destructive to their own application storage
  reservations and are not run over the retained Doom bond.

Do not close C9 from host tests or builds alone. The gate passes only when all
steps above succeed on physical hardware and the sanitized evidence contains
no Bluetooth identity or key material.
