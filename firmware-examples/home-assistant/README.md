# Mist Maker × Home Assistant

Two ways to put a Programmable Mist Maker on your smart-home dashboard. Both expose the mist as a **dimmable light entity** (on/off + level slider), battery sensors, and water/disc status — and both shut the mist down gracefully when the battery runs low.

## Option A — ESPHome (recommended, no code)

1. Install the **ESPHome Builder** add-on in Home Assistant.
2. New Device → paste [`esphome-mistmaker.yaml`](esphome-mistmaker.yaml) → set your WiFi secrets.
3. Flash the XIAO ESP32-C6 over USB once; updates are over-the-air after that.
4. HA auto-discovers the device. Done.

The YAML targets the **Battery Kit V0.3**. For the Extension (BFF) V0.2, delete the boost switch, battery sensors, and button blocks.

## Option B — Arduino + MQTT

Prefer to stay in the Arduino ecosystem (or to customize behavior)? Use the **`HomeAssistant_MQTT`** example in the [MistMaker library](https://github.com/owochel/MistMaker) (v1.1+):

1. Run an MQTT broker (the Mosquitto add-on) and HA's MQTT integration.
2. `File > Examples > MistMaker > HomeAssistant_MQTT`, fill in WiFi + broker credentials, flash.
3. The sketch publishes MQTT Discovery configs — the device appears automatically with mist light, battery %, and water-status entities, plus offline detection via LWT.

## Automation ideas

- Mist on sunset, off at bedtime (`light.turn_on` with `brightness`)
- Notify your phone when water runs low (`water == "low"`)
- Auto-stop when battery < 15% (the firmware hard-stops at 3.2 V regardless)
