# Desktop-calendar


# ESP32 WiFi Slideshow + Clock with DS1307 RTC

An ESP32-based desk display that cycles JPEG images from a microSD card and shows a full clock/calendar screen on demand. Time is kept persistently by a DS1307 RTC module and can be synced wirelessly from any phone or browser.

---

## Features

- 🖼️ **Slideshow** — cycles JPEG images from SD card every 4 seconds, auto-fitted to the 240×320 display
- 🕐 **Clock + Calendar** — tap the touch sensor to show the current time and a full month calendar for 10 seconds, then automatically return to the slideshow
- 📡 **WiFi Access Point** — always-on AP (`ESP32-Slideshow` / `12345678`); connect your phone and open `192.168.4.1` to sync time or upload/manage images
- 🔋 **Persistent time** — DS1307 RTC keeps time through power cycles; no internet required

---

## Hardware Required

| Component | Description |
|---|---|
| ESP32 Dev Board | Any standard 30/38-pin ESP32 |
| ST7789 TFT Display | 240×320 px, SPI interface |
| MicroSD Card Module | SPI interface (shares bus with TFT) |
| DS1307 RTC Module | I2C, with CR2032 coin cell battery |
| TTP223 Touch Sensor | Capacitive touch, single output pin |
| SD Card | FAT32 formatted, containing JPEG images |
| 2*3000 mah battery | For powering the project |
| LM2596 Buck Converter | Step down the voltage from 8.4 v to 5v |
| MRB045 charging module| To charge the 2s battery configuration|
| 2S 20A BMS | For over voltage protection |


---

## Wiring / Pin Connections

### SPI Bus (shared by TFT display and SD card)

| ESP32 GPIO | Pin Name | Connected To |
|---|---|---|
| GPIO 23 | MOSI | TFT SDA + SD MOSI |
| GPIO 19 | MISO | SD MISO |
| GPIO 18 | SCK | TFT SCL + SD SCK |

> **Note:** The TFT is write-only so it does not use the MISO line.

---

### ST7789 TFT Display (240×320)

| ESP32 GPIO | TFT Pin | Description |
|---|---|---|
| GPIO 5 | CS | Chip Select |
| GPIO 2 | DC / RS | Data / Command select |
| GPIO 4 | RST | Hardware Reset |
| GPIO 23 | SDA / MOSI | SPI Data |
| GPIO 18 | SCL / SCK | SPI Clock |
| 3.3V | VCC | Power |
| GND | GND | Ground |

---

### MicroSD Card Module

| ESP32 GPIO | SD Module Pin | Description |
|---|---|---|
| GPIO 15 | CS | Chip Select |
| GPIO 23 | MOSI | SPI Data In |
| GPIO 19 | MISO | SPI Data Out |
| GPIO 18 | SCK | SPI Clock |
| 3.3V | VCC | Power |
| GND | GND | Ground |

---

### DS1307 RTC Module (I2C)

| ESP32 GPIO | DS1307 Pin | Description |
|---|---|---|
| GPIO 21 | SDA | I2C Data |
| GPIO 22 | SCL | I2C Clock |
| 3.3V | VCC | Power |
| GND | GND | Ground |

> **Note:** Install a CR2032 coin cell battery on the RTC module to keep time when the ESP32 is powered off.

---

### TTP223 Capacitive Touch Sensor

| ESP32 GPIO | TTP223 Pin | Description |
|---|---|---|
| GPIO 32 | OUT / I/O | Touch output signal |
| 3.3V | VCC | Power |
| GND | GND | Ground |

---

## Full Wiring Summary (Quick Reference)

```
ESP32              ST7789 TFT
─────              ──────────
GPIO  5  ────────► CS
GPIO  2  ────────► DC
GPIO  4  ────────► RST
GPIO 23  ────────► SDA (MOSI)
GPIO 18  ────────► SCL (SCK)
3.3V     ────────► VCC
GND      ────────► GND

ESP32              SD Card Module
─────              ──────────────
GPIO 15  ────────► CS
GPIO 23  ────────► MOSI
GPIO 19  ◄────────  MISO
GPIO 18  ────────► SCK
3.3V     ────────► VCC
GND      ────────► GND

ESP32              DS1307 RTC
─────              ──────────
GPIO 21  ◄───────► SDA
GPIO 22  ────────► SCL
3.3V     ────────► VCC
GND      ────────► GND

ESP32              TTP223 Touch
─────              ────────────
GPIO 32  ◄────────  OUT
3.3V     ────────► VCC
GND      ────────► GND
```

---

## Libraries Required

Install these via the Arduino Library Manager:

| Library | Purpose |
|---|---|
| `Adafruit GFX Library` | Graphics primitives |
| `Adafruit ST7735 and ST7789 Library` | TFT display driver |
| `JPEGDEC` | JPEG decoding |
| `RTClib` (Adafruit) | DS1307 RTC driver |
| `SD` | MicroSD card access |
| `SPI` | SPI bus (built-in) |
| `Wire` | I2C bus (built-in) |
| `WiFi` | ESP32 WiFi (built-in) |
| `WebServer` | HTTP server (built-in) |

---

## SD Card Setup

1. Format the SD card as **FAT32**
2. Copy JPEG images (`.jpg`) directly to the **root directory**
3. Images are displayed in alphabetical order
4. Maximum **80 images** supported

---

## WiFi & Time Sync

1. Power on the ESP32 — it creates a WiFi access point automatically
   - **SSID:** `ESP32-Slideshow`
   - **Password:** `12345678`
2. Connect your phone or laptop to that network
3. Open a browser and go to **`http://192.168.4.1`**
4. The web page automatically reads your device's current time and sends it to the ESP32 + RTC
5. Time is saved to the DS1307 and survives power-off

---

## Usage

| Action | Result |
|---|---|
| Power on | Loads time from RTC, starts slideshow |
| Single tap (touch sensor) | Shows clock + calendar for 10 seconds |
| After 10 seconds | Automatically returns to slideshow |
| Connect to WiFi AP + open browser | Syncs time from your device, allows image upload |

---

## Configuration (in `imageclock.ino`)

| `#define` | Default | Description |
|---|---|---|
| `SLIDE_DELAY` | `4000` | Milliseconds between image changes |
| `CLOCK_SHOW_MS` | `10000` | How long clock stays on after tap (ms) |
| `WIFI_SSID` | `"ESP32-Slideshow"` | Access point name |
| `WIFI_PASS` | `"12345678"` | Access point password |
| `MAX_IMAGES` | `80` | Maximum number of images loaded from SD |
| `DISP_W / DISP_H` | `240 / 320` | Display resolution |
