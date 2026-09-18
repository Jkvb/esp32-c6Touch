# IAWICHU // NERVE OS

Cyberpunk wearable interface for a prosthetic-hand prototype built on the
Waveshare ESP32-C6 Touch LCD 1.9. The current milestone provides a responsive
watchface, touch navigation, gesture-preset previews, live diagnostics, IMU
rotation, and optional background NTP synchronization.

## Current controls

- Swipe left/right: `CORE`, `HAND`, and `SENSE` pages.
- Double tap on `CORE`: cycle `NERVE`, `SYNTH`, and `MATRIX` watchfaces.
- Swipe up/down on `HAND`: browse gesture presets.
- Tap `<` or `>`: browse presets with large touch targets.
- Tap `SELECT PRESET`: select a preview. This does **not** move motors.

The six built-in previews are `OPEN`, `POWER`, `PINCH`, `POINT`, `TRIPOD`, and
`PEACE`. Each profile displays its five finger positions as animated tendon
bars. Profiles live in `main/gesture_profiles.c` and are deliberately separate
from the UI.

## Safety boundary

The UI publishes a gesture request through `ui_gesture_request_cb_t`, but the
application currently logs it only. A future motor supervisor must enforce
travel limits, current limits, speed limits, fault handling, and an emergency
open action before connecting this callback to actuators.

## Confirmed hardware map

| Device | Signal | GPIO / value |
|---|---|---|
| ST7789V2 | MOSI | GPIO4 |
| ST7789V2 | SCLK | GPIO5 |
| ST7789V2 | DC | GPIO6 |
| ST7789V2 | CS | GPIO7 |
| ST7789V2 | RESET | GPIO14 |
| Backlight | BL | GPIO15, active-low |
| Shared I2C | SCL | GPIO8 |
| Shared I2C | SDA | GPIO18 |
| CST816 family | address | `0x15` |
| QMI8658C | address | `0x6B` (`0x6A` fallback) |

The visible LCD area is `170x320` with `X gap = 35`, `Y gap = 0`. Do not change
these pins, gaps, or backlight polarity without a hardware revision.

## Software baseline

- ESP-IDF `6.1.x`
- LVGL `9.5.x`
- ESP32-C6 target
- 8 MB physical flash header
- Custom 2 MB factory application partition
- Montserrat 48 and 32 only, to keep the firmware compact

Touch and IMU share one board-I2C owner (`main/board_i2c.c`). Rotation requests
are queued by the IMU task and applied inside the LVGL task; background tasks do
not call LVGL directly.

## Build and flash

Linux:

```bash
cd /home/charly/PT2025-esp32
. "$IDF_PATH/export.sh"
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Adjust the serial port if the board enumerates as `/dev/ttyUSB0` or another
device.

PowerShell with the existing ESP-IDF installation:

```powershell
cd "$env:USERPROFILE\esp32\esp32-c6Touch"
Set-ExecutionPolicy -Scope Process Bypass -Force
. C:\esp\esp-idf\export.ps1
idf.py set-target esp32c6
idf.py -p COM6 build flash monitor
```

Wi-Fi is optional. If `IAWICHU_WIFI_SSID` is empty, `CORE` starts immediately
with monotonic uptime and the network task stays quiet. When SNTP succeeds, the
watchface changes to real local time without blocking the interface.
