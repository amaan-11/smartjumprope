# Smart Jump Rope

Smart Jump Rope is an ESP32-C6 based training project that counts jumps, measures heart rate and SpO2, shows live stats on an OLED, and streams workout data over BLE to a small web dashboard.

The repository contains two parts:

- Embedded firmware built with ESP-IDF and CMake
- A Node.js + Express web app that connects over Web Bluetooth and stores workout history in SQLite

## Features

- Real-time jump detection from an MPU6050 accelerometer
- Heart rate and SpO2 sampling from a MAX30102 sensor
- OLED status pages for calibration, jump totals, and vitals
- BLE control + telemetry for live workout streaming
- Browser dashboard for connecting, starting/stopping workouts, and saving sessions
- SQLite-backed workout history per user

## Hardware

The firmware is structured around these devices:

- `ESP32-C6` as the main controller
- `MPU6050` for motion sensing
- `MAX30102` for heart rate / SpO2
- `SSD1306` OLED over I2C

All sensor/display peripherals are accessed over the shared I2C manager in `components/i2cInit`.

## Firmware Overview

The main application entry point is `main/main.cpp`. On boot it initializes I2C, BLE, the OLED, and the sensor pipeline, then starts several FreeRTOS tasks:

- `jumpDetectionTask`: continuously updates the jump detector
- `displayTask`: cycles OLED pages for calibration, jump totals, and vitals
- `bleUpdateTask`: publishes the current workout snapshot over BLE
- `heartRateTask`: reads MAX30102 samples and runs the SpO2 / heart-rate algorithm

### Current jump-counting behavior

- A 3-second calibration phase starts when a workout begins
- Multiple timing configurations are tracked internally
- One timing configuration is treated as authoritative for the official jump total
- The selected configuration is index `2`, described in code as the `200/200 ms` quick-fix setting

## BLE Data Flow

The custom BLE service is implemented in `components/ble/jr_ble.cpp`.

The browser-side BLE client in `app/public/ble.js` expects a 12-byte packet:

- bytes `0-3`: timestamp
- bytes `4-7`: jump count
- byte `8`: heart rate
- bytes `9-10`: SpO2
- byte `11`: flags

Flag meanings currently used by the web app:

- bit `0`: heart rate valid
- bit `1`: SpO2 valid
- bit `2`: calibration active

## Web App Overview

The web backend lives in `app/private/webApp.js` and serves the frontend in `app/public`.

Main responsibilities:

- serve the frontend pages
- handle user sign-up and login
- store workouts in SQLite
- expose workout history APIs

Database logic is in `app/private/data_base.js`. The SQLite database file is created at:

- `app/private/database.db`

The backend currently listens on port `3000`.

## Repository Layout

```text
smartjumprope/
|- components/
|  |- ble/                # BLE service and packet publishing
|  |- common/             # shared helpers such as mutex utilities
|  |- display/            # SSD1306 OLED driver/wrapper
|  |- gyro/               # MPU6050 reading task and sensor abstraction
|  |- heartbeatSensor/    # MAX30102 driver and RF algorithm
|  |- i2cInit/            # shared I2C manager
|  `- vmotor/             # vibration motor-related component
|- main/                  # app entry point and jump detection logic
|- app/
|  |- private/            # Express server + SQLite access
|  `- public/             # frontend pages, BLE client, history UI
|- flash.sh               # helper script for local ESP-IDF flashing
`- CMakeLists.txt         # ESP-IDF project root
```

## Building the Firmware

This project uses ESP-IDF with CMake, not PlatformIO.

### Prerequisites

- ESP-IDF installed and exported in your shell
- ESP32-C6 toolchain available
- USB connection to the board

### Build

```bash
idf.py set-target esp32c6
idf.py build
```

### Flash and monitor

```bash
idf.py -p <YOUR_PORT> flash monitor
```

There is also a helper script in `flash.sh`, but it contains machine-specific paths and should be adjusted before use.

## Running the Web App

From `app/private`:

```bash
npm install
npm start
```

Then open:

```text
http://localhost:3000
```

Optional environment variables:

- `SESSION_SECRET` for Express session signing

## Workflow

1. Flash the ESP32-C6 firmware.
2. Start the web server.
3. Open the dashboard in a Chromium-based browser with Web Bluetooth support.
4. Connect to the jump rope over BLE.
5. Press Start to begin a workout.
6. Wait for calibration to finish, then begin jumping.
7. Press Stop to end the session and save the workout to SQLite.

## Important Notes

- The heart-rate task only publishes values when the MAX30102 algorithm marks them as valid.
- During calibration, the BLE layer reports `0` jumps to the client.
- The current Linux flashing script is not directly usable on Windows without edits.
- The heartbeat sensor component currently includes a tester source file in its CMake registration, which may not be desirable for production builds.

## Key Files

- `main/main.cpp`
- `main/jump.cpp`
- `components/gyro/gyro.cpp`
- `components/heartbeatSensor/max30102.cpp`
- `components/ble/jr_ble.cpp`
- `app/private/webApp.js`
- `app/public/ble.js`
