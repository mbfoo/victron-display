# Victron Display

A live display for one or more (up to 10) **Victron SmartSolar MPPT** charge controllers built on the 
**Waveshare ESP32-C6 Touch LCD 1.47"** development board. It uses Victron's Bluetooth LE advertising
for reading the data.


## Features

- **Victron MPPT display** – Total and per device PV power, battery voltage and charger state per device
- **Web UI** – View Victron data and configure Wi-Fi profiles, MQTT
- **MQTT publishing** – periodic telemetry to any broker
- **Touchscreen UI**

## Hardware

| Component | Details |
|---|---|
| MCU board | Waveshare ESP32-C6 Touch LCD 1.47" (320×172, SPI LCD + capacitive touch) |
| Solar | Victron SmartSolar MPPT (Bluetooth LE advertising) |

## Build & Flash

The project uses **PlatformIO**. No Arduino IDE setup is required.

### Prerequisites

- [Visual Studio Code](https://code.visualstudio.com/) with the
  [PlatformIO extension](https://platformio.org/install/ide?install=vscode), **or**
- PlatformIO Core CLI: `pip install platformio`

### Steps

```bash
# 1. Clone the repository
git clone https://github.com/mbfoo/victron-display.git
cd bms-monitor

# 2. Copy and edit defaults in config.sample.h
cp src/config.sample.h src/config.h
vim src/config.h

# 3. Build
pio run

# 4. Flash
pio run --target upload

# 5. (Optional) open serial monitor
pio device monitor --baud 115200
```

PlatformIO will automatically install all required libraries
(LVGL, NimBLE-Arduino, PubSubClient, ArduinoJson, …) declared in `platformio.ini`.

### First boot

On first boot the device starts in **Wi-Fi AP mode** (check GUI
for the password). Open `http://192.168.4.1` in a browser to configure your Wi-Fi
credentials and MQTT broker. Settings are persisted to flash.

## Victron MPPT Setup

Victron SmartSolar MPPT chargers encrypt their Bluetooth advertisements with a
per-device **AES-128 key**. You must retrieve this key from the official Victron
Connect app and enter it into the Web UI before the device can be decoded.

### How to get the AES key

1. Install the **[VictronConnect app](https://www.victronenergy.com/panel-systems-remote-monitoring/victronconnect)**
   on your phone (iOS or Android).
2. Open the app, connect to your SmartSolar MPPT via Bluetooth.
3. Tap the **⋮ menu → Product info**.
4. Scroll down to **Encryption data** (or _Instant Readout key_) and note the
   32-character hex key (e.g. `a1b2c3d4e5f6...`).
5. Repeat for every Victron device you want to monitor.

> The key is static and tied to the hardware. It does not change unless you
> explicitly reset the device in VictronConnect.

### Entering the keys in the Web UI

1. Connect to the same Wi-Fi network as the Victron Monitor (or connect to its AP —
   see _First boot_ below).
2. Open `http://<device-ip>` in a browser.
3. Navigate to **Settings → Victron Devices**.
4. For each device enter a **display name** and paste the **AES key**.
5. Click **Save**.