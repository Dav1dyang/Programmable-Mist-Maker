# Does mist output max out at 50% PWM duty? — research memo

**Date:** 2026-07-03 **Method:** deep-research sweep (5 search angles, 19 sources
fetched, 25 claims adversarially verified 3-vote; refuted claims discarded) +
**first-party bench sweep on the physical Battery Kit V0.4** (DutySweep_Test,
disc in water, USB power).
**Board context:** single N-MOSFET (UCC27511A-driven Q4) switching one leg of a
CD75-style 3-terminal autotransformer (L1) from the ~5 V rail; piezo disc across
the other legs forms the resonant tank (~80 Vpp at 108.7 kHz).

---

## Verdict

**"Mist maxes out at 50% duty" is FALSE for this topology — but "cap the duty
at 50%" is still the right rule.** The bench sweep showed mist *increasing*
past 50%, and the topology-aware sources agree it can. What actually peaks at
50% is *efficiency*: past it, input current grows superlinearly, the
autotransformer saturates and heats, soft switching is lost, and (on USB) the
supply browns out. 127/255 is the efficiency/stability knee — the correct
default operating cap, not a physical output ceiling.

## Why the classic "50% is optimal" argument doesn't bind this board

The textbook argument is solid **for what it assumes**: the fundamental of a
rectangular pulse train of *fixed amplitude A* is ∝ `A·sin(π·D)`, maximal at
exactly D = 0.5 [1][2], and a resonant tank responds almost only to the
fundamental (tank-current THD ≈ 5% at loaded Q = 2.5 [3]; the disc voltage on a
commercial mist board measures near-sinusoidal [4]). Under those premises,
mist-vs-duty is a broad hump symmetric about 50%.

**The premise that fails here is "fixed amplitude."** This board is a
flyback-style single-switch drive: the drain isn't clamped to a rail — it
*rings up* during the off-time, and the ring amplitude depends on how much
energy the on-time stored in L1. Longer on-time → more stored energy → bigger
swing. The verification pass flagged exactly this: the sin(πD) formula
"strictly assumes a voltage-stiff rectangular drain waveform; the real
single-switch drain node rings during off-time and its waveform shape is
itself duty-dependent" — and fundamental-only analyses err by up to ~60% at
low loaded Q (Kazimierczuk & Puczko, IEEE TCAS 1987 [5]).

Two topology-aware sources predict output *rising* past 50%:
- **Re-tuned Class-E analysis** (hardware-validated): output grows
  monotonically with duty when the network follows (V_out 2.1 → 6.4 → 10.4 V
  at D = 0.25/0.5/0.75), at the cost of falling efficiency and steeply rising
  switch stress [6].
- **S.C. Johnson patent US 6,439,474** — the *same* single-FET
  tapped-autotransformer piezo atomizer circuit — uses gate pulse width
  (~19% up to ~68–78% duty) precisely as its **amplitude control** [7].

## The bench data (V0.4, 2026-07-03) — the deciding evidence

The research's open questions asked for exactly this measurement; we have it:

| duty % | drive current | observation |
|---|---|---|
| 18–50% | 170 → 225 mA (near-plateau) | resonant regime; mist rises; best mist/W |
| 56% | 314 mA | mist still rising |
| 62% | 485 mA | **L1 hot to touch** — saturation onset |
| 69% | 783 mA | superlinear current = saturation + lost soft switching |
| 75% | 1095 mA (≈1.5 A total on VBUS) | mist still visibly rising → **USB VBUS sag → TPS2116 drops USB (<4.0 V PR1) → brownout reset** (2×, reproducible) |

Mist output: **monotonically increasing to the crash point** (operator
observation) — consistent with [6][7], contradicting the fixed-amplitude
sin(πD) peak. Input power at 75% ≈ 5× the 50% point for a visibly modest mist
gain: the efficiency knee is real even though the output peak is not.

## Why commercial practice still sits at 50%

Microchip's ~117 kHz single-MOSFET nebulizer reference design (AN2265) drives
the FET at a **fixed 50% duty** and tunes *frequency* (90–150 kHz sweep) to
find transducer resonance [8]; teardowns of commodity 5 V mist modules show
the same square-ish OTP-MCU gate drive [4]. Duty is a poor throttle past 50%
(all pain, thin gain); frequency/amplitude are the professional knobs. A 1 MHz
Class-E study likewise recommends D = 0.5 as the design point — off it, ZVS is
lost and switching losses rise (98.4% efficiency at D = 0.5 with optimum load)
[9] — matching our heat observations.

## Expected curves for this board (synthesis)

- **Drive current vs duty:** near-flat plateau through the resonant regime
  (~18–50%), then superlinear growth (saturation + hard switching) — measured ✓.
- **Mist vs duty:** rising through 50% and continuing to rise slowly while
  current explodes; practical end = supply collapse (USB ~75%) or thermal
  limit (L1), not an output peak.
- **Mist-per-watt vs duty:** peaks at/below 50% and collapses beyond — this is
  the curve the 127 cap actually optimizes.

## Practical guidance

1. **Keep `dutyMax = 127` as the library default** (efficiency, USB budget,
   L1 thermals, brownout margin).
2. A supervised **"turbo" band ~143–160 (56–63%)** exists for short bursts on
   a strong supply — watch L1 temperature; DutySweep_Test now aborts >600 mA.
3. If more mist is ever a product requirement, the honest levers are **rail
   voltage** (boost 4.95 → 5.5 V via R12) and **frequency fine-tuning to the
   disc's true resonance** (AN2265-style sweep — a good future library
   experiment), not duty.
4. Open question worth a scope session: drain waveform vs duty (does ring-back
   clip into the next on-time past 50%?), and whether co-tuning frequency with
   duty recovers output more efficiently ([7]'s approach).

## Sources

[1] S. Smith, *The Scientist & Engineer's Guide to DSP*, ch. 13 — dspguide.com/ch13/4.htm
[2] UIUC Physics 406 lecture notes, Fourier analysis of rectangular waves (duty-dependent harmonics, DC offset >50%)
[3] Spirov et al., *Eng. Proc.* 122(1):11 (2026) — square-wave-fed series resonant tank, THD vs Q, FHA limits
[4] T.K. Hareendran, EDN "Mist maker" teardown — commodity 5 V module, near-sine disc voltage
[5] Kazimierczuk & Puczko, IEEE Trans. Circuits & Systems (1987), DOI 10.1109/TCS.1987.1086114 — exact Class-E analysis at any Q
[6] Frequency-domain re-tuned Class-E analysis, hardware-validated (ShanghaiTech, metal.shanghaitech.edu.cn/publication/J5.pdf) — output monotonic in D when re-tuned
[7] US Patent 6,439,474 B2 (S.C. Johnson) — tapped-autotransformer piezo atomizer, pulse width as amplitude control
[8] Microchip AN2265 — vibrating-mesh nebulizer reference design, fixed 50% duty, frequency-tuned
[9] "Design and Analysis of 1 MHz Class-E Power Amplifier for Load and Duty Cycle Variations" (IRF510 prototype)

*Discarded in verification (do not cite): the claim that the 1 MHz Class-E
duty sweep showed output falling both sides of 50% (refuted 0–3); its specific
efficiency-vs-duty percentages (refuted 0–3); "50% has zero DC content
uniquely" as stated (refuted on technicality). The patent's >50% amplitude
claim passed only 2–1 (design assertion, unmeasured) — our bench data is the
stronger evidence on that point.*

---
---

# PART 2 — The full curve: Mist(D), measured and modeled (same day, complete)

**Bench campaign (2026-07-03, V0.4 + Extension V0.1, DutySweep_Test v3):**

| Supply | Result |
|---|---|
| Laptop USB + V0.4 (cell charging) | power CUT (~1.5 A port breaker) at ~75% duty — `reset: power-on`, not brownout |
| Laptop USB + Extension | full range 0→90%; apparent current "rollover" at 87–90% was VBUS sag, not the tank |
| 2 A wall brick + Extension | full range; drive current pinned at the **~1.1 A ADC ceiling** from 75% |
| 2 A wall brick + V0.4 (no cell) | inline meter read **2.1 A total (~1.9 A drive) at 80–85%** → brick fold-back → power cut. The boards' own sensor ceiling is the ESP32 ADC (~1.1 A), on both variants |
| Charged cell (4.02 V rest) + V0.4 | reached **exactly 70%** (822 mA drive ≈ 1.34 A from the cell), then the 3.5 V loaded floor; sag staircase gives **total source resistance ≈ 0.40 Ω** |
| 2 A wall brick + V0.4 **with cell attached** | still reset after the 80% step — **the cell cannot catch a brick fold-back at ~2 A**: the TPS2116's ~1.3 ms break-before-make gap vs ~30–40 µs of capacitor hold-up (42 µF @ 2 A), with the boost dragging the rail down during the gap. Cell sat untouched at 4.22–4.25 V throughout. **V0.4 hard ceiling ≈ 80% duty on any realistic supply** — comfortably above the 70% mist peak, so no operational loss |

**Operator mist observations (the output axis):** onset threshold — large
jump between 10% and 20% duty; steady rise through 50–65%; **visual maximum
at ~70% (duty 178)**; smaller at 80% and 90%; unstable/intermittent
("sometimes bigger, sometimes smaller") above ~75% on both laptop and brick.

## The model (deep-research pass 2: drive side verified, atomization side fitted)

```
Mist(D) = k · max(0, A(D) − A_th)^n        valid below D_unstable ≈ 0.75
A(D)    = a · D · (1 − D)^q
```

| Parameter | Meaning | Value from bench |
|---|---|---|
| `A_th` | capillary-wave atomization onset amplitude — below it, ripples but no droplets | crossed at **D ≈ 0.12–0.15** (the 10→20% jump) |
| `q` | ring-back clipping severity (how hard truncating the resonant half-period kills delivered amplitude) | peak at D\* = 1/(1+q) = 0.70 → **q ≈ 0.43** |
| `n` | super-threshold atomization-rate exponent | **free fit** — the literature claims (incl. onset-amplitude data and Faraday f/2 mechanism) could not be adversarially verified this run (infrastructure errors, not refutations); treat as mainstream-but-uncited |
| `D_unstable` | onset of intermittency | **≈ 0.75** observed |

**Drive-side physics, verified 3-0 against primary sources:**
- Unsaturated single-switch flyback/ringing-choke: `I_pk = V·t_on/L`, energy
  per cycle `½·L·I_pk² ∝ t_on²`, power `∝ (V·D)²/L` (ST AN1326 eq. 6/9,
  AN1262 eq. 4, US 4,862,338) → amplitude grows ~linearly with D. This is
  why "50% is the max" fails on this topology.
- The drain ring reaches its first valley exactly **one resonant half-period
  `Tv = π√(Lp·Cd)` after demagnetization** (AN1326 eq. 4). At T = 9.2 µs with
  Tv ≈ 4.6 µs, any D > ~0.5 truncates the ring — matching the current
  departing its plateau at exactly 50%.
- Clipped ring = non-ZVS turn-on: the residual drain-capacitance energy dumps
  through the FET every cycle (`P ≈ [(5/6)·Coss·ΔV^1.5 + ½·Cd·ΔV²]·f_sw`,
  AN1326) — the documented mechanism converting the superlinear input current
  into switch/inductor heat instead of mist. L1's measured heating and the
  current blow-up are also consistent with core saturation (the `∝ t_on²` law
  explicitly assumes an unsaturated core); distinguishing the two needs a
  scope on the drain (late-ramp upward curvature = saturation; turn-on spike
  = capacitive dump).

**Instability above ~75% — three documented candidate classes** (any or all):
period-doubling/chaotic dynamics in switching converters (arXiv 1210.7295);
uneven alternating cycles when quantized resonant timing fights per-cycle
energy balance (AN1326); high-intensity fountain-structure breakdown — "the
drop-chain fountain structure becomes less defined" (PMC 4428615; note that
study saw atomization still *increasing*, so the mist *decline* here is
better attributed to the clipped drive than to the fountain).

## Bottom line (unchanged, now quantified)

- **50% duty** = efficiency knee and the battery-sustainable operating point
  (~0.3 A cell current). Library default `dutyMax = 127` stands.
- **~70% duty** = true mist maximum ≈ the v2.0 "turbo" `setMaxDuty(178)`:
  ~3.5–4× the input power of 50%, wall-power territory, brief cell sprints
  only (a full cell reaches it once, riding the LDO floor).
- **>75%** = unstable, declining mist, ~10 W in, D1 at ~2× rating on stiff
  supplies. No operational value; do not park there.

## Open questions (future scope session, optional)

1. Saturation vs non-ZVS dump: scope Q4's drain at 60–75% duty.
2. Subharmonic (f_PWM/2) content on the piezo above 75% would confirm
   period-doubling; slow (seconds) drift would implicate thermal detuning.
3. A kitchen-scale water-loss measurement per duty step would turn the visual
   mist axis into numbers and pin the exponent `n`.
4. Literature value of `n` near 100 kHz remains uncited (verification
   infrastructure failed, not refuted) — re-run if it ever matters.

**Sources (part 2):** ST AN1326 (L6565 quasi-resonant, eq. 4/6/9 + capacitive
loss + uneven-cycle note); ST AN1262 (L6590, eq. 4); US Patent 4,862,338
(ringing-choke converter); arXiv 1210.7295 (period doubling in DC-DC
converters); arXiv 1609.03607 (TI DRV2700-class flyback piezo driver context);
PMC 4428615 (ultrasonic fountain structure at high intensity).
