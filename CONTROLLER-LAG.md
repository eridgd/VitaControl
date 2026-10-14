# Controller input lags while AirPods are connected

Firmware 3.65, SceBt seg0 at 0x02180000. Measured 2026-10-14 with the flag-gated stats build
(`ux0:data/vitacontrol_stats_enable.txt` -> `ux0:data/vitacontrol_stats.txt`), 8BitDo in
Switch mode (report 0x3F).

## Measurement

| Phase                 | Reports/s | Avg request->reply | Worst gap    |
| --------------------- | --------- | ------------------ | ------------ |
| Controller only       | ~100      | 10 ms              | 30-50 ms     |
| + AirPods connected   | 10-29     | 40-107 ms          | 100-490 ms   |
| AirPods disconnected  | ~100      | 10 ms              | ~30 ms       |

- `drain_max` was 1 in every second: VitaControl never had an event backlog.
- Request->reply time equals the gap between replies, so reports are not queued anywhere
  and read late; they simply arrive about 8x less often. Buttons, D-pad and sticks all lag
  because whole reports are late.
- Disabling VitaBtFix did not change it, so the A2DP timestamp patch is not involved.

## Why: SceBt only schedules known Sony pads

`0x2190434` is SceBt's HCI command sender (154 call sites, standard opcodes plus 0xFCxx
vendor commands). It sends Sniff_Mode (0x0803) from two places.

**HID link setup, `0x2193258`.** It compares the device's `PID<<16 | VID` (`[r6+40]`):

- 054C:05C4 (DS4) takes its own path at `0x21933C8`
- 054C:0268 (DS3), 054C:03D5, 054C:042F and 0E6F:0329 get a sniff interval of 18 slots
  (11.25 ms)
- 054C:0306 (PS3 BD remote) gets 72 slots (45 ms)
- anything else gets `r4 = 0` and `cbz r4` skips the Sniff_Mode command entirely

**Idle policy, `0x218DA9E`.** When a link's counter at `[r0+12]` exceeds 800, it sends
Sniff_Mode with max = min = 2 x table[link] slots. The table at `0x0219F338` holds
0x167..0x18D, so 718-794 slots, which is **449-496 ms**. Patching it changed nothing (see
below), so it is not what produces the long gaps.

A sniff link has fixed anchor points that the radio keeps even while A2DP is streaming.
A Sony pad gets one every 11.25 ms. The 8BitDo gets none, so it runs on best-effort polling,
and A2DP takes most of the airtime. Without audio there is nothing to compete with, so the
pad reaches its native ~100 Hz.

## Result with the patch (2026-10-14)

Switch mode, AirPods connected t=96..114 with audio:

| Phase                 | Reports/s | Avg request->reply | Worst gap |
| --------------------- | --------- | ------------------ | --------- |
| Controller only       | ~68       | 15 ms              | 23-34 ms  |
| + AirPods connected   | ~68       | 15 ms              | 23-45 ms  |
| AirPods disconnected  | ~68       | 15 ms              | 23-34 ms  |

The rate no longer depends on the AirPods. The cost is that the pad now runs at a steady
~68 Hz instead of the ~100 Hz it reached unsniffed with nothing else on the radio, under
the same 11.25 ms setting SceBt uses for the DS3. The interval is the `movs r4, #18` at
`0x21932EC`, shared with the Sony pads.

## The fix (shipped)

`applySniffPatches()` in `src/main.cpp`, applied with `taiInjectDataForKernel` only when SceBt's
module NID is 0x67E0C2EB (3.65); removing the plugin undoes it:

- `0x2193298`: the 054C:042F compare becomes 057E:2009, so Switch-mode pads get the
  11.25 ms sniff.
- `0x21932A4`: the 0E6F:0329 compare becomes a vendor-only compare against 2DC8
  (`uxth r2, r3; movw r1, #0x2DC8; cmp r2, r1; nop`), so any 8BitDo in D-input takes the same
  path. `r1` is rewritten on every path after it.

## Still open: 8BitDo D-input (Lite 2, 2DC8:5112)

D-input pads only send a report when the input changes. Between movements the link idles at
almost exactly 800 slots (gaps of 509-513 ms, and multiples of that), with or without AirPods,
and with the setup patch above in place. With AirPods streaming it drops to ~12 reports/s.

Tried and reverted, because neither changed the timing:

- Setting every entry of the idle-policy table at `0x0219F338` to 9 (18 slots).
- Lowering that policy's threshold at `0x218DAA0` from `> 800` to `> 18`. With this in place the
  user saw repeated inputs, most likely held buttons whose release arrived ~500 ms late.

So the 800-slot idle sniff does not come from SceBt's policy code. It is probably requested by
the pad or set by the Bluetooth controller firmware. The next step would be to log the HCI
commands SceBt sends (`0x2190434`), after checking that function is safe to hook. Until then,
use Switch mode with headphones.
