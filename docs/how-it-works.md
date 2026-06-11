# How It Works

Every board in this project is the same idea with different power plumbing: an ESP32
shakes a piezo disc at its resonant frequency until water turns into mist.

![How a cheap humidifier works](https://raw.githubusercontent.com/Dav1dyang/Programmable-Mist-Maker/main/assets/Exsiting_Cheap_Humidifier_Design.gif)

## The mist engine

1. **Ultrasonic vibration** — a piezo disc oscillating at **108.7 kHz** tears water
   into droplets small enough to float.
2. **PWM switching** — the XIAO ESP32-C6 outputs a 108.7 kHz PWM signal (up to 50%
   duty) into a gate driver + MOSFET.
3. **Voltage boosting** — the MOSFET switches a 3-legged tapped inductor
   (auto-transformer) in a loop with the disc; LC resonance boosts the 5 V rail to the
   **~30–40 Vpp** the disc actually needs.
4. **Current sensing** — an INA180A3 amplifier reads the voltage across a 30 mΩ shunt
   (3.0 V per amp). A missing disc, a dry disc, and a disc in water each draw
   distinctly different current — so one ADC pin gives you disc detection *and* a
   water sensor for free.

```
5V ──► gate driver ──► MOSFET ──► 3-leg inductor ──► piezo disc (108.7 kHz)
        ▲ PWM from ESP32              (LC resonance: 5 V → ~30–40 Vpp)
5V current ──► 30 mΩ shunt ──► INA180A3 ──► ADC pin
```

## Design evolution

![PCB design progress](https://raw.githubusercontent.com/Dav1dyang/Programmable-Mist-Maker/main/assets/PCB_Board_Design_Progress.gif)

The project started by reverse-engineering cheap commercial humidifier circuits, then
rebuilding them as documented, hackable boards. The full design story is on
[Hackster](https://www.hackster.io/dav1dyang/waste-into-wonder-making-programmable-mist-makers-fe3ae7).

## Known issues & fixes

| Issue | Fix |
|---|---|
| Mist fails on battery | Bypass the XIAO's 3.3 V regulator with an external boost converter |
| Uploading code fails | Add a pull-down to the MOSFET gate; disable the boost rail during upload |
| Startup delay | Disable OTA; add a delay before activating mist |

## Programming notes

!!! note "Power sequence matters"
    When developing, always connect **USB first, then battery**. If battery power is
    applied before USB, the serial port may not enumerate in the Arduino IDE. Once
    serial is up, battery can be connected freely. Standalone (no computer), either
    power source works alone.

## Use and care

!!! warning
    Because the device uses a water reservoir and cotton sticks to move water near
    electronics, careless operation can lead to bacterial growth over time. Use
    distilled or clean tap water, clean the container regularly, and let cotton
    sticks dry fully between uses. Keep electronics dry where they should be dry.

## References

- TPS61023 datasheet — Texas Instruments
- MCP73831 datasheet — Microchip
- GreatScott! ultrasonic mist explanation (YouTube)
- BigCliveDotCom mist maker teardown (YouTube)
