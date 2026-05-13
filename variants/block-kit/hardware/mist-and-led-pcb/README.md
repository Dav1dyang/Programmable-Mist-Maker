# Block Kit Mist & LED PCB (V0.1)

Hosts the IS31FL3731 LED driver (14 LEDs populated on the Matrix B side, CB1–CB9), reed switch `SW1`, piezo connector `J1` (PZ+/PZ−), Qwiic connector `J6`, and the `SW+`/`SW−` pads (`J2`) that mate with the Button PCB.

Drop the KiCad project here:

```
Block-Kit-Mist-and-LED-PCB-V0-1.kicad_pro
Block-Kit-Mist-and-LED-PCB-V0-1.kicad_sch
Block-Kit-Mist-and-LED-PCB-V0-1.kicad_pcb
Block-Kit-Mist-and-LED-PCB-V0-1.kicad_prl
bom.csv
2026-05-02_Block-Kit-Mist-and-LED-PCB-V0-1_SCH.pdf
2026-05-02_Block-Kit-Mist-and-LED-PCB-V0-1_BRD.pdf
```

## LED layout

14 LEDs split into two rows wired off Matrix B of the IS31FL3731:

- Row 1: CB1 anode → CB2…CB9 cathodes → **8 LEDs**
- Row 2: CB2 anode → CB3…CB8 cathodes → **6 LEDs**

The firmware lights them through David's [`Adafruit_IS31FL3731` fork](https://github.com/Dav1dyang/Adafruit_IS31FL3731) in Matrix B mode and verifies the (x,y) mapping with the `w` (`ledWalk`) Serial command at bring-up.

## V0.1 reed-switch rework

See [`../README.md`](../README.md) for the blue-wire instructions that route the reed switch to D1 instead of the SW+/SW− series circuit.
