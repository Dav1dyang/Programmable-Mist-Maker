# Block Kit Driver PCB (V0.1)

Hosts the XIAO ESP32-C6 socket and the full power chain: USB-C input, LP4068BF Li-Po charger, TPS2116DRLR power mux, AP7361C 3V3 LDO, TPS61023 3V3→5V boost, UCC27518 level shifter, DMT10H009LCG MOSFET piezo drive, and INA180A3 + 30 mΩ shunt current sense.

Drop the KiCad project here:

```
Block-Kit-V0-1.kicad_pro
Block-Kit-V0-1.kicad_sch
Block-Kit-V0-1.kicad_pcb
Block-Kit-V0-1.kicad_prl
bom.csv
2026-05-02_Block-Kit-V0-1_SCH.pdf
2026-05-02_Block-Kit-V0-1_BRD.pdf
```

See [`../README.md`](../README.md) for inter-board wiring and the V0.1 rework note for the reed switch.
