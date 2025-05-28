# Programmable Mist Maker V1

**Created by shuang cai & David Yang**

![Mist Maker PCB](assets/Ducky_and_Container.jpg)


This [Open Source Hardware Certified](https://certification.oshwa.org/us002742.html) ultrasonic mist maker project transforms recycled containers and a custom PCB into a small-form-factor mist device. It is a fully documented, low-cost circuit with accessible explanations of misting mechanics, design challenges, and power needs.

Full Documentation on Notion: [Mist Maker Documentation](https://dav1dyang.notion.site/programmable-mist-maker)

## Overview

![PCB Board Design](assets/2025-05-22_V1-4_Assembled.jpg)

This project explain ultrasonic mist maker designs and provides:

* A tested, open-source reference circuit
* Lessons learned through prototyping and debugging
* A replicable and modifiable electronics system

Most mist circuits online lack documentation—this guide fills that gap with working examples and full KiCad/Arduino files.

## How It Works

![PCB Board Progess](assets/PCB_Board_Design_Progress.gif)

Mist generation involves:

* **Ultrasonic Vibration**: A piezo disc (108.7 kHz) oscillates rapidly to turn water into mist.
* **Voltage Boosting**: A 3-legged inductor (auto-transformer) amplifies 5V input to approximately 30–40 Vpp.
* **PWM Switching**: An ESP32-C6 sends a 108.7 kHz PWM signal to a MOSFET.
* **Power Supply**: TPS61023 provides stable 5V from LiPo or USB input.

More details: [Notion Documentation](https://dav1dyang.notion.site/programmable-mist-maker)

## Key Components
![PCB Board Components](assets/2025-05-22_V1-4_Board_Only.jpg)

| Component                      | Function                            |
| ----------------------------   | ----------------------------------- |
| Piezo Disc (108.7 kHz)         | Vibrates water to generate mist     |
| Tapped Inductor (3-legged)     | Boosts voltage through LC resonance |
| AO3400A MOSFET                 | Switches circuit at high frequency  |
| TPS61023 Boost Converter       | Powers piezo from battery or USB    |
| MCP73831                       | LiPo charging and protection        |
| [Seeed Studio XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-Pre-Soldered-p-6328.html)     | Controls mist and PWM               |

## Circuit Operation

1. Boost 3.3V/USB to 5V
2. ESP32 outputs PWM (108.7 kHz)
3. MOSFET switches inductor-piezo loop
4. LC resonance boosts voltage
5. Piezo emits mist

## Known Issues & Fixes

| Issue                 | Fix                                                        |
| --------------------- | ---------------------------------------------------------- |
| Mist fails on battery | Bypass Xiao's 3.3V regulator with external boost converter |
| Uploading code fails  | Add pull-down to MOSFET gate; disable boost during upload  |
| Startup delay         | Disable OTA; add delay before activating mist              |

## Assembly Instructions

**Materials:**

* PCB (v1.4)
* Piezo disc
* 3-legged inductor
* Xiao ESP32-C6
* Soldering tools
* Battery or USB-C cable
* Recycled container

**Steps:**

1. Solder all components
2. Connect piezo and inductor
3. Attach battery or USB
4. Upload firmware via Arduino
5. Seal in container (keep electronics dry)
6. Power on and activate mist

## Programming Notes

### Important Note on Power Sequence and Serial Connectivity

When connecting the board for development or testing:

* Always connect the USB cable first before applying battery power
* If battery power is connected before USB, the serial port will not be detected in the Arduino IDE
* The proper sequence is: connect USB → establish serial connection → apply battery power (if needed)
* Once the USB connection is established, battery power can be applied without disrupting the serial connection
* When operating in standalone mode without a computer, either power source can be used independently

## Code & Firmware

This repository includes:

Library Example Code:
You must install the library for this example to work. [Library installation instruction](https://github.com/owochel/MistMaker/tree/main)

* [SimpleControl.ino](example-code/WithLibrary/SimpleControl/SimpleControl.ino) - Basic test with button control using the libary

The example sketch provides:
* 108.7kHz PWM output for the mist maker
* Button toggle for mist on/off
* Serial output with status information

* [Blink.ino](example-code/WithLibrary/Blink/Blink.ino) - Basic blink test that turn on and off the mist maker on a 2 second interval

The example sketch provides:
* 108.7kHz PWM output for the mist maker
* Blink toggle of the mist maker
* Serial output with status information



* Example code: [WithoutLibrary-108kHz_Output_3V3_XIAOC6.ino](example-code/WithoutLibrary-108kHz_Output_3V3_XIAOC6/WithoutLibrary-108kHz_Output_3V3_XIAOC6.ino) - Basic test with button control without using the libary

The example sketch provides:
* 108.7kHz PWM output for the mist maker
* Button toggle for mist on/off
* Serial output with status information



## Files & Downloads

* KiCad Files:
  * [MistMakerV1-4.kicad_sch](hardware/MistMakerV1-4.kicad_sch)
  * [MistMakerV1-4.kicad_pcb](hardware/MistMakerV1-4.kicad_pcb)
* PDF Exports:
  * [Schematic (PDF)](hardware/2025-05-13_MistMaker_V1-4_SCH.pdf)
  * [PCB Layout (PDF)](hardware/2025-05-13_MistMaker_V1-4_BRD.pdf)
* [Bill of Materials (CSV)](hardware/bom.csv)
* [3D printable models for enclosures](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/3DPrintModels)

## References

* TPS61023 Datasheet – Texas Instruments
* MCP73831 Datasheet – Microchip
* GreatScott! Ultrasonic Mist Explanation (YouTube)
* BigCliveDotCom Mist Maker Teardown (YouTube)

Full reference list in: [Notion Documentation](https://dav1dyang.notion.site/programmable-mist-maker)

## Documentation Notes

This README and technical documentation were written with the support of ChatGPT (GPT-4o) to:

* Summarize logs and prototyping notes
* Clarify complex circuit behavior
* Reformat documentation for accessibility

All technical data was reviewed and validated against physical test results and datasheets.

## License & Attribution

Open-source under MIT License. Designed and tested by [David Yang](https://davidyang.work/) and [shuang cai](https://shuangcai.cargo.site/)

Inspired by community reverse-engineering, YouTube teardowns, and a desire to share back with the open hardware world.
