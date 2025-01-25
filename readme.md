# autobru

An ESP32-based automatic coffee brewing control system that interfaces with Bluetooth scales and Breville Dual Boiler espresso machines.

## Features

- Connects to BOOKOO Bluetooth scales
- Reads real-time weight and flow rate data 
- Automated shot control based on target weight with dynamic learning
- WebSocket-based real-time weight updates
- Asynchronous web server for control and monitoring
- RESTful API endpoints for complete control
- Integration with [Bru](https://github.com/xvca/bru) PWA for coffee brewing/bean tracking
- Weight triggered Preinfusion mode that uses low pressure until the first drops of coffee are detected (as an alternative to preinfusing for a pre-defined duration)
- OTA Updates using ESPAsyncHTTPUpdateServer

## Hardware Requirements

- ESP32 development board 
- Breville Dual Boiler espresso machine
- BOOKOO Bluetooth coffee scale
- Basic wiring for brew switch control

## Hardware Details

The custom PCB includes:

- 2x JST S11B-PH-K-S connectors
  - One accepts stock female connector from machine
  - One connects PCB to mainboard
- ESP32-S3-MINI module
- Mini360 buck converter (12V to 5V)
- 4-channel optocoupler for button control
  - Only one channel needed for brew control
  - Any brew button can stop an active brew
  - e.g. 2-cup start → manual stop is valid

The PCB design allows for non-destructive installation by intercepting the stock button panel connector. Power is drawn from the machine's 12V line, converted to 5V for the ESP32.

The brew control is simplified since the Breville Dual Boiler allows any brew button to stop an active shot - you don't need to use the same button that started the brew to stop it.

## Dependencies

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) - Lightweight Bluetooth LE library
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - Async HTTP and WebSocket server
- [ESPAsyncHTTPUpdateServer](https://github.com/IPdotSetAF/ESPAsyncHTTPUpdateServer) - OTA Updates

## Setup

1. Install PlatformIO IDE extension in VS Code
2. Clone this repository
3. Build and upload to your ESP32 using PlatformIO

## Configuration

The device will automatically:
1. Scan for BOOKOO scales
2. Connect when found
3. Begin receiving weight measurements
4. Start the web server for control and monitoring
5. Enable WebSocket connection for real-time data

## Web Interface

The ESP32 hosts a web interface accessible via its IP address, offering:
- Real-time weight display via WebSocket
- Scale control (tare, timer functions)
- RESTful API endpoints for automation

## API Endpoints

- `/prefs` - GET/POST brew preferences (enabled state, presets, preinfusion mode)
- `/start` - POST to start a brew with target weight
- `/stop` - POST to stop current brew
- `/clear-data` - POST to clear stored shot data
- `/wake` - POST to wake ESP from sleep mode

The system includes a learning algorithm that tracks shot history and automatically adjusts timing to ensure the final brew weight matches the target weight. This compensation factor is stored and refined over time based on actual results.

## Status

This is currently a work in progress. Future updates will include:
- Integration with bru PWA
- Configurable target weights
- Remote control capabilities
- Web interface features

## License

VIRAL PUBLIC LICENSE
Copyleft (ɔ) All Rights Reversed

This WORK is hereby relinquished of all associated ownership, attribution and copy
rights, and redistribution or use of any kind, with or without modification, is
permitted without restriction subject to the following conditions:

1.	Redistributions of this WORK, or ANY work that makes use of ANY of the
	contents of this WORK by ANY kind of copying, dependency, linkage, or ANY
	other possible form of DERIVATION or COMBINATION, must retain the ENTIRETY
	of this license.
2.	No further restrictions of ANY kind may be applied.
