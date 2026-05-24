# ESP32 WiFi Slideshow + RTC Clock

An ESP32-powered WiFi slideshow and digital clock system using an ST7789 TFT display, DS1307 RTC module, SD card storage, and a touch sensor.

The project allows you to:

* Display JPEG slideshow images from an SD card
* Upload images wirelessly through a web interface
* Automatically resize and optimize images to 240×320
* Show a digital clock and calendar using a DS1307 RTC module
* Sync RTC time directly from a phone or browser
* Switch between slideshow mode and clock mode using a touch sensor

---

# Live HTML Converter Link

> Replace this placeholder with your deployed HTML page link:

```txt
https://ashishcornelio.github.io/Desktop-calendar-/

```

---

# Project Files

| File                | Description |
| ---- | ---- |
| `image_clock.ino`   | Main ESP32 slideshow + RTC firmware |
| `image_upload.html` | Desktop/web image converter for preparing images for the ESP32 |

---

# Features

## ESP32 Firmware Features

* WiFi Access Point mode
* Wireless image upload interface
* JPEG slideshow playback
* RTC-based clock/calendar display
* Touch sensor mode switching
* SD card image management
* Automatic image scaling and fitting
* Real-time device clock sync from browser

## HTML Converter Features

* Drag-and-drop image upload
* Automatic image resizing
* JPEG compression with binary search optimization
* Folder save support
* Batch image conversion
* Preview thumbnails
* Conversion logs and statistics
* SD-card-ready image export

---

# Components Required

## Main Hardware

| Component              | Description                             |
| ---------------------- | --------------------------------------- |
| ESP32 Dev Board        | Any standard 30/38-pin ESP32            |
| ST7789 TFT Display     | 240×320 px, SPI interface               |
| MicroSD Card Module    | SPI interface (shares bus with TFT)     |
| DS1307 RTC Module      | I2C, with CR2032 coin cell battery      |
| TTP223 Touch Sensor    | Capacitive touch, single output pin     |
| SD Card                | FAT32 formatted, containing JPEG images |
| 2×3000 mAh Battery     | For powering the project                |
| LM2596 Buck Converter  | Step down the voltage from 8.4V to 5V   |
| MRB045 Charging Module | To charge the 2s battery configuration  |
| 2S 20A BMS             | For over voltage protection             |

---

# Wiring Connections

## TFT Display Connections (ST7789)

| TFT Pin    | ESP32 Pin |
| ---------- | --------- |
| CS         | GPIO 5    |
| DC         | GPIO 2    |
| RST        | GPIO 4    |
| MOSI / SDA | GPIO 23   |
| SCK / CLK  | GPIO 18   |
| VCC        | 3.3V      |
| GND        | GND       |
| LED / BL   | 3.3V      |

---

## DS1307 RTC Connections

| RTC Pin | ESP32 Pin |
| ------- | --------- |
| SDA     | GPIO 21   |
| SCL     | GPIO 22   |
| VCC     | 3.3V / 5V |
| GND     | GND       |

---

## TTP223 Touch Sensor Connections

| TTP223 Pin | ESP32 Pin |
| ---------- | --------- |
| OUT        | GPIO 32   |
| VCC        | 3.3V      |
| GND        | GND       |

---



# WiFi Access Point Details

| Setting     | Value             |
| ----------- | ----------------- |
| SSID        | `ESP32-Slideshow` |
| Password    | `12345678`        |
| Web Address | `192.168.4.1`     |

---

# Required Arduino Libraries

Install the following libraries using the Arduino Library Manager:

| Library         |
| :-------------: |
| Adafruit GFX    |
| Adafruit ST7789 |
| JPEGDEC         |
| RTClib          |
| SD              |
| SPI             |
| WiFi            |
| WebServer       |

---

# Installation Guide

## 1. Install Arduino IDE

Download and install Arduino IDE.

## 2. Install ESP32 Board Package

Add the ESP32 board package in Arduino IDE.

## 3. Install Required Libraries

Install all libraries listed above.

## 4. Wire the Components

Follow the connection tables in this README.

## 5. Upload the Firmware

Open `image_clock.ino` and upload it to the ESP32.

## 6. Prepare Images

Open the HTML converter page and:

* Drag images into the page
* Convert images to 240×320 JPEG format
* Save them to the SD card

## 7. Insert SD Card

Insert the SD card into the ESP32 SD module.

## 8. Power On the ESP32

The slideshow should start automatically.

---

# Using the Web Interface

1. Connect your phone or PC to:

```txt
ESP32-Slideshow
```

2. Enter the password:

```txt
12345678
```

3. Open your browser and visit:

```txt
http://192.168.4.1
```

4. Upload and manage slideshow images wirelessly.

---

# Touch Controls

| Action     | Function                               |
| ---------- | -------------------------------------- |
| Single Tap | Show clock and calendar for 10 seconds |

---

# Image Specifications

| Setting          | Value       |
| ---------------- | ----------- |
| Resolution       | 240×320     |
| Format           | JPEG        |
| Recommended Size | Under 45 KB |

---

# Project Workflow

```text
Images → HTML Converter → Optimized JPEG → SD Card → ESP32 Slideshow
```

---

# Troubleshooting

## SD Card Not Detected

* Check CS pin connection
* Format SD card to FAT32
* Verify SPI wiring

## TFT Display Not Working

* Check SPI pins
* Verify power connections
* Confirm ST7789 library installation

## RTC Time Not Updating

* Check SDA/SCL wiring
* Verify RTC battery
* Ensure browser time sync works

## Images Not Displaying

* Ensure images are JPEG
* Keep image size under recommended limit
* Use the provided HTML converter

---
