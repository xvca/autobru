# Autobru

Autobru is a DIY project I've been working on to add brew-by-weight functionality to my Breville Dual Boiler, compatible with Bookoo scales.

While this runs the machine, you might want the [Bru Web Interface](https://github.com/xvca/bru) to easily set your target weight, visualize shots, and track your history.

## What it does

*   **Connects to the Scale:** Automatically scans for and connects to Bookoo Bluetooth scales.
*   **Controls the Machine:** Intercepts the brew button signals to start/stop shots electronically.
*   **Adaptive Stop:** Uses a learning algorithm that attempts to learn from your previous shots. It calculates when to cut the pump so the final drips land on your target weight. This means you can switch between spouted and bottomless portafilters and autobru will adapt to whichever you're using over the course of a few shots.
*   **Weight-Triggered Pre-infusion (Optional):** Instead of a fixed time, it can hold the machine in low-pressure pre-infusion until the first drops actually hit the cup (detected by the scale), then ramp up to full pressure.
*   **API & WebSocket:** Exposes a REST API and real-time WebSocket stream for weight/flow data.

## Hardware & DIY Guide

<img src="https://github.com/user-attachments/assets/6b753872-b7ba-4e36-aa85-fffcd82899c3" alt="pcb" width="250"/>
<img src="https://github.com/user-attachments/assets/7e20d250-a7f0-4608-a933-aaea8918ad65" alt="pcb in machine" width="270"/>

This project is meant to be DIY-friendly. The PCB sits inside the white plastic box behind the screen, intercepting the connection between the button panel and the mainboard. It draws 12V from the machine and converts it to 5V for the ESP32.

### Required parts

You can order the PCB from a fab house like JLCPCB using the files in the `pcb/` folder. Here is exactly what you need to populate it:

*   **Microcontroller:** [Waveshare ESP32-S3-Zero](https://www.waveshare.com/esp32-s3-zero.htm)
*   **Optocoupler:** 1x PC847 (DIP-16 package)
*   **Resistor:** 1x 100Ω resistor
*   **Buck Converter:** 1x Mini360 (12V to 5V)
*   **Connectors:**
    *   1x [JST PH 11-Pin Right Angle Socket](https://www.digikey.com.au/en/products/detail/jst-sales-america-inc/S11B-PH-K-S/926635) (S11B-PH-K-S) - *Soldered to the PCB*
    *   1x [JST PH 11-Pin Housing](https://www.digikey.com.au/en/products/detail/jst-sales-america-inc/PHR-11/608599) (PHR-11) - *For the cable*
*   **Cables:**
    *   You need to connect the PCB to the mainboard. Since both fit inside the white box, you don't need much length.
    *   Recommended: [JST PH Pre-crimped 2" Cables](https://www.digikey.com.au/en/products/detail/jst-sales-america-inc/asphsph24k51/6009457) (ASPHSPH24K51). These are super easy to work with, just snap them into the housing.

## Brewing Logic & Presets

Autobru has some internal logic for how it handles the physical buttons on your machine.

### Presets & Decaf Mode
You can configure two weight presets via the `/prefs` endpoint (or the Bru settings page): **Regular** and **Decaf**.
*   **Time-based Switching:** If you define your timezone and a "Decaf Start Hour" (e.g., 14:00), the system automatically switches to the Decaf target weight after that time and switches back at midnight.

### Button Mapping
*   **Manual Button:** Starts a shot with the **full preset weight** as the target.
*   **1-Cup Button:** Starts a shot with the **half preset weight** as the target (nice to have if brewing single shots with a spouted portafilter).
*   **2-Cup Button:** Currently, this just wakes the ESP if it's sleeping. I use it to purge my machine mainly. It doesn't trigger a specific functionality by default, but since you have the source code, you can map this to whatever you want!

*Note:* If you use **Weight-Triggered Pre-infusion**, preferably use the Manual button. It allows the ESP to hold the pre-infusion for an arbitrary duration until the first drops hit the cup. Doing this on the volumetric (1-cup/2-cup) buttons requires some funky workarounds to stop and restart the shot logic. That logic is already implemented but I would consider it experimental/unstable.


## API & Automation

While the [Bru Web Interface](https://github.com/xvca/bru) is the intended way to use this, Autobru exposes a standard HTTP API.

This means you don't *have* to use the web app. You can trigger shots via **iOS Shortcuts**, Home Assistant, or curl commands if you want to build your own automations.

**Key Endpoints:**
*   `POST /start` - Start a brew (params: `weight`).
*   `POST /stop` - Kill the shot immediately.
*   `POST /wake` - Wake the ESP32 start scanning bluetooth connections to find the scale.
*   `GET /prefs` - Get current settings (presets, pre-infusion mode).
*   `POST /prefs` - Change settings.
*   `WS /ws` - Real-time stream of weight, time, and flow rate.

## Setup

1.  **WiFi Configuration:**
    *   Create a file named `credentials.h` in the `include/` folder.
    *   Add your WiFi details:
        ```cpp
        #define WIFI_SSID "your_ssid"
        #define WIFI_PASSWORD "your_password"
        ```
    *   *Note: I might add a captive portal later so you can set this from your phone, but since you're already compiling the code yourself, this gets the job done.*

2.  **Firmware:**
    *   Install VS Code + PlatformIO.
    *   Clone this repo.
    *   Build and upload to the ESP32 via USB.

3.  **Network Setup (Important):**
    *   Once the device connects to your WiFi, log into your router and **set a DHCP reservation (Static IP)** for it.
    *   The Bru web app needs a consistent IP address to talk to the machine, so you don't want it changing on you.

## Wireless Updates (OTA)

If all goes well, should only need to plug the ESP32 into your computer once to flash it.

For future updates, just navigate to `http://[esp-ip]/update` in your browser. You can upload the `.bin` file (generated by PlatformIO in `.pio/build/...`) directly. This saves you from having to open up the machine every time an update is released.

## Power Management

To save power (the ESP can get quite hot when scanning for bluetooth devices AND running the webserver), Autobru puts itself to sleep and disconnects Bluetooth after 10 minutes of inactivity. It wakes up automatically when you interact with the API (e.g., hitting the "Wake" button in the web app) or physically press a button on the machine.

## ⚠️ Work in Progress

This is active development. The flow compensation algorithm works quite well, but features and API endpoints might change as I refine the integration with the web frontend.
