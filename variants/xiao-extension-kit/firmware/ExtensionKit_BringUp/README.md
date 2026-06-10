# ExtensionKit_BringUp

Per-feature hardware verification for the **Extension Kit V0.1**. No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Serial checklist (115200 baud)

| Send | Verifies |
|---|---|
| `t` | PWM out of D0 — disc in water should mist |
| `c` | One INA180 current reading on D2 |
| `s` | CSV stream for Arduino Serial Plotter — compare dry vs wet disc |
| `0`–`9` | Duty 0–90% — mist should visibly dim |
| `h` | Help |

Expected at duty 64 (50% of full): no disc ≈ 0 mA · dry disc ≈ 70–100 mA · disc in water ≈ 130–200 mA.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) examples.
