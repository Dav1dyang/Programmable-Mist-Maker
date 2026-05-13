# V1.4 — Historical Reference

This folder is the original (and only) released revision of the Programmable Mist Maker, kept intact for anyone reproducing the kit as documented in the root `README.md`.

It has been **superseded** by the modular variant tree under [`../../variants/`](../../variants/). New work targets one of the four V0.x variants (Xiao Extension Kit, Battery Kit, Block Kit, I2C MultiPack Kit). V1.4 will not receive new firmware features.

Contents:

- `hardware/` — KiCad project, schematic + PCB PDFs, BOM.
- `example-code/` — Two reference sketches: `WithLibrary/` (using the `MistMaker` Arduino library) and `WithoutLibrary-108kHz_Output_3V3_XIAOC6/` (bare-metal 108.7 kHz PWM).
- `EnclosureDesignMaterials/` — STEP/STL/DXF files for the 3D-printed shell.

Safety, cleaning, and known-issues guidance still lives in the root `README.md`.
