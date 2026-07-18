# ExtensionKit_BringUp

Per-feature hardware verification for the **Extension Kit V0.1**. No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Automated self-test (start here)

**Press the XIAO's BOOT button** (the Extension has no button of its own — BOOT is safe to press after boot) or send `a` over serial @ 115200. The sketch runs a few-second pass/fail checkout, disc in water recommended:

| Row | Measures | Pass window |
|---|---|---|
| `RESET` | Last reset reason | anything but `BROWNOUT` |
| `IDLE_MA` | INA180 zero, PWM off | ≤ 10 mA |
| `LOAD_MA` | 50% duty burst ≈ 1 s | 60–115 mA dry · 115–280 mA in water · < 10 mA = no disc (skip) |
| `BOOT_BTN` | The trigger itself (or a 5 s press window) | press seen |

The run ends with `VERDICT: PASS/FAIL` plus one machine-parseable line (`SELFTEST,EXTENSION_KIT,<fw>,<verdict>,<counts>` — grep `^SELFTEST,` when batch-logging). Same report format as the Battery Kit self-test, so batch QC across both kits logs identically.

## Serial checklist (115200 baud)

| Send | Verifies |
|---|---|
| `a` (or BOOT press) | Automated self-test (table above) |
| `t` | PWM out of D0 — disc in water should mist |
| `c` | One INA180 current reading on D2 |
| `s` | CSV stream for Arduino Serial Plotter — compare dry vs wet disc |
| `0`–`9` | Duty 0–90% — mist should visibly dim |
| `h` | Help |

Expected at duty 64 (50% of full): no disc ≈ 0 mA · dry disc ≈ 70–100 mA · disc in water ≈ 130–200 mA.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) (≥ 2.1.0) examples.
