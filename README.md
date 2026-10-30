# UV Guard – Smart UV Risk Classification and Ambient Light Monitoring System

<p align="center">

**An IoT-Based Ultraviolet Radiation Monitoring and Risk Classification System**

Built using ESP8266 NodeMCU, Analog UV Sensor, ST7735 TFT Display and ThingSpeak Cloud.

</p>

---

## 1. Project Overview

**UV Guard** is a low-cost, portable IoT-based environmental monitoring system designed to measure ultraviolet (UV) radiation, classify UV exposure into different risk levels, and provide immediate visual alerts.

The system uses an ESP8266 NodeMCU as its central controller, an analog UV sensor for radiation measurement, and an LDR for ambient light detection. The measured data is processed using averaging, exponential smoothing and threshold-based classification with hysteresis.

A 1.8-inch ST7735 TFT display provides real-time information about the UV sensor reading, risk category, ambient light condition and protective recommendations. Green and red LEDs provide additional visual alerts.

The ESP8266's built-in Wi-Fi enables the system to upload sensor readings to the ThingSpeak cloud platform for remote monitoring and historical data visualization.

**Key design feature:** Local sensing, display and alert generation continue independently even when Wi-Fi or cloud connectivity is unavailable.

## 2. Problem Statement

Excessive exposure to ultraviolet radiation can cause sunburn, premature skin ageing, eye damage and skin cancer.

Conventional UV monitoring solutions have certain limitations:

- Professional UV meters are relatively expensive.
- Weather applications provide regional UV forecasts rather than measurements at the user's exact location.
- Basic microcontroller-based systems often display raw sensor values without meaningful risk classification.
- Cloud-dependent monitoring systems may become unavailable during network failures.
- Unfiltered sensor readings can fluctuate, producing unstable classifications and display flickering.

UV Guard addresses these limitations by providing affordable local UV monitoring, risk classification, visual alerts and cloud-based data logging.

## 3. Project Objectives

- Measure UV radiation continuously using an analog UV sensor.
- Acquire sensor readings at 25 ms intervals.
- Reduce measurement noise using one-second averaging and exponential smoothing.
- Classify readings into four risk categories using configurable thresholds and hysteresis.
- Monitor ambient brightness using an LDR.
- Display sensor readings, risk levels and protective advice on a TFT display.
- Provide immediate alerts through green and red LEDs.
- Upload sensor information to ThingSpeak every 16 seconds.
- Maintain local monitoring during network or cloud failures.
- Support serial-based testing of all four risk levels.

## 4. System Architecture

The system consists of four major functional stages:

1. **Perception Layer:** Collects UV radiation and ambient light information.
2. **Processing Layer:** Filters sensor readings and classifies UV risk using ESP8266.
3. **Application Layer:** Displays risk information and generates visual alerts.
4. **Cloud Layer:** Uploads sensor readings to ThingSpeak for remote monitoring.

### System Block Diagram

```text
       ANALOG UV SENSOR
               |
               | Analog Signal
               v
       +------------------+
       |                  |
       |  ESP8266 NODEMCU |
       |                  |
       +------------------+
               ^
               |
       LDR SENSOR
     (Bright / Dark)
               |
               v
    +-----------------------+
    | SENSOR DATA PROCESSING|
    |                       |
    | 1. Sampling           |
    | 2. Averaging          |
    | 3. Smoothing          |
    | 4. Risk Classification|
    | 5. Hysteresis         |
    +-----------------------+
               |
       +-------+-------+
       |               |
       v               v
  ST7735 TFT       LED INDICATORS
   DISPLAY          GREEN / RED
       |
       |
       v
  ESP8266 WI-FI
       |
       v
  THINGSPEAK CLOUD
       |
       v
  WEB DASHBOARD
  AND DATA ANALYTICS
```

## 5. Hardware Components

| Component | Quantity | Purpose |
|---|---:|---|
| NodeMCU ESP8266 (ESP-12E) | 1 | Main controller and Wi-Fi connectivity |
| Analog UV Sensor (GUVA-S12SD) | 1 | UV radiation measurement |
| LDR | 1 | Ambient light detection |
| 10 kΩ Resistor | 1 | LDR voltage divider |
| ST7735 TFT Display (1.8-inch) | 1 | Real-time monitoring interface |
| Green LED | 1 | Indicates lower risk levels |
| Red LED | 1 | Indicates higher risk levels |
| 220 Ω Resistors | 2 | LED current limiting |
| Breadboard / Perfboard | 1 | Circuit assembly |
| Jumper Wires | As required | Hardware connections |
| USB Cable | 1 | Power and programming |

## 6. Hardware Pin Configuration

### 6.1 UV Sensor and LDR

| Component Pin | ESP8266 Pin | Function |
|---|---|---|
| UV Sensor VCC | 3.3V | Power supply |
| UV Sensor GND | GND | Ground |
| UV Sensor OUT | A0 | Analog UV measurement |
| LDR Divider Output | D1 | Digital bright/dark detection |

### 6.2 TFT Display – ST7735

| TFT Pin | NodeMCU Pin | Function |
|---|---|---|
| VCC | 3.3V | Power supply |
| GND | GND | Ground |
| CS | D8 | Chip Select |
| RST | D4 | Display Reset |
| DC / A0 | D3 | Data / Command |
| SDA / MOSI | D7 | SPI Data |
| SCK | D5 | SPI Clock |
| LED | 3.3V | Backlight |

### 6.3 LED Connections

| LED | NodeMCU Pin | Operating Condition |
|---|---|---|
| Green LED | D2 | LOW and MODERATE |
| Red LED | D0 | HIGH and VERY HIGH |

**Hardware Note:** Each LED is connected through a 220 Ω current-limiting resistor. Check the NodeMCU board's analog input voltage range before connecting the UV sensor. ESP8266 boot-configuration pins D3, D4 and D8 also require appropriate startup logic levels.

## 7. Firmware Design and Signal Processing

The firmware is developed using C++ with the Arduino framework in the Arduino IDE.

### 7.1 Sensor Sampling

The analog UV sensor is connected to A0.

- ADC resolution: 10 bits.
- ADC range: 0–1023.
- Sampling interval: 25 milliseconds.
- Samples per processing window: Approximately 40.
- Processing interval: 1 second.

The samples are accumulated and averaged to reduce short-term fluctuations.

**Average calculation:**

\[
UV_{avg}=\frac{\sum_{i=1}^{N}UV_i}{N}
\]

Where:

- \(UV_i\) = Individual ADC reading.
- \(N\) = Number of samples in the window.

### 7.2 Exponential Smoothing Filter

An exponential smoothing filter is applied to stabilize the measured readings.

\[
UV_f[n]=\alpha UV_{avg}[n]+(1-\alpha)UV_f[n-1]
\]

Where:

- \(UV_f[n]\) = Current filtered reading.
- \(UV_{avg}[n]\) = Current averaged reading.
- \(\alpha\) = Smoothing factor.
- \(UV_f[n-1]\) = Previous filtered reading.

This reduces noise and provides more stable risk classification.

### 7.3 UV Risk Classification

The filtered ADC reading is compared with configurable thresholds.

| Level | Risk Category | ADC Threshold | TFT Colour | LED |
|---|---|---:|---|---|
| 1 | LOW | Below 100 | Green | Green |
| 2 | MODERATE | 100 and above | Yellow | Green |
| 3 | HIGH | 200 and above | Orange | Red |
| 4 | VERY HIGH | 300 and above | Red | Red |

*Note: These are preliminary raw ADC thresholds, not calibrated UV Index boundaries.*

### 7.4 Hysteresis Mechanism

Hysteresis prevents rapid switching between adjacent risk levels when readings fluctuate around a threshold.

The system uses an 8-count hysteresis margin when transitioning back to a lower level.

This improves classification stability and avoids unnecessary changes in display colour and LED indication.

### 7.5 Ambient Light Detection

The LDR is connected through a voltage divider and read through digital pin D1.

- HIGH = BRIGHT
- LOW = DARK

A time-based debounce of approximately 3 seconds is applied before changing the stored light state.

## 8. Software Architecture

The firmware is organized into modular functions.

| Function | Responsibility |
|---|---|
| `sampleUv()` | Reads the analog UV sensor |
| `sampleLdr()` | Reads ambient light state |
| `processUvWindow()` | Performs averaging and exponential smoothing |
| `classify()` | Determines the UV risk level |
| `setLeds()` | Controls green and red LEDs |
| `uploadReading()` | Sends readings to ThingSpeak |
| `networkTask()` | Handles Wi-Fi and cloud communication |
| `printSerial()` | Displays debugging information |
| `handleSerial()` | Processes serial test commands |
| `updateScreenFlow()` | Manages TFT screen transitions |

The main firmware uses `millis()`-based scheduling to coordinate sensor sampling, display updates and network operations.

## 9. Communication Protocols

| Interface | Application |
|---|---|
| ADC | UV sensor data acquisition |
| Digital GPIO | LDR input and LED control |
| SPI | TFT display communication |
| UART / USB Serial | Debugging and test commands |
| Wi-Fi (IEEE 802.11 b/g/n) | Wireless cloud connectivity |
| HTTP over TCP/IP | ThingSpeak data upload |

### Cloud Communication

The ESP8266 operates in Wi-Fi station mode and connects to the configured wireless network.

Sensor information is transmitted to ThingSpeak using HTTP requests.

The upload interval is 16 seconds to comply with the minimum update interval of the free ThingSpeak service used in the project.

### ThingSpeak Field Mapping

| ThingSpeak Field | Data |
|---|---|
| Field 1 | Raw UV ADC reading (0–1023) |
| Field 2 | UV risk level (1–4) |
| Field 3 | Ambient light state (1 = Bright, 0 = Dark) |

### Cloud Status Indication

| Indicator | Meaning |
|---|---|
| Grey | Wi-Fi disconnected |
| Yellow | Wi-Fi connected, but upload unsuccessful or not yet confirmed |
| Green | Successful cloud upload |

## 10. User Interface

The ST7735 TFT display operates in landscape orientation at 160 × 128 pixels.

The interface contains three screens:

**1. Startup Screen**
- UV Guard project title.
- Sensor initialization status.
- Wi-Fi connection status.
- Startup progress indication.

**2. Main Monitoring Screen**
- Current UV ADC reading.
- Four-level risk classification.
- Colour-coded risk indicator.
- Ambient light status.
- Protective recommendation.
- Cloud connectivity indicator.
- Visual heartbeat indicator.

**3. Sensor Information Screen**
- Raw UV reading.
- Risk classification.
- Sensor condition.
- Ambient light status.
- Cloud status.

Partial screen updates are used to reduce unnecessary TFT redraws and visible flickering.

## 11. Implementation Workflow

The overall implementation follows these stages:

1. Assemble the NodeMCU ESP8266 and peripheral components.
2. Interface the analog UV sensor with A0.
3. Connect the LDR voltage divider to D1.
4. Interface the ST7735 display using SPI.
5. Connect the green and red LED indicators.
6. Develop the sensor acquisition and signal-processing functions.
7. Implement four-level risk classification with hysteresis.
8. Develop the TFT monitoring interface.
9. Integrate Wi-Fi and ThingSpeak cloud communication.
10. Verify all risk levels using serial test mode.
11. Perform indoor, shaded and outdoor sensor tests.
12. Verify local operation during network disconnection.

## 12. Testing and Validation

The prototype was evaluated under different environmental conditions.

| Test Condition | Observed ADC Reading | Classified Level | LED |
|---|---:|---|---|
| Sensor Covered | 2 | LOW | Green |
| Indoor Lighting | 3 | LOW | Green |
| Near Window | 40 | LOW | Green |
| Outdoor Shade | 120 | MODERATE | Green |
| Direct Sunlight | 400 | VERY HIGH | Red |

These observations validate the system's response and classification behaviour using the preliminary thresholds.

### Functional Verification

- UV sensor acquisition and filtering.
- Four-level risk classification.
- Hysteresis operation.
- Ambient light detection.
- TFT display and partial updates.
- LED response.
- Wi-Fi connectivity.
- ThingSpeak cloud upload.
- Offline monitoring.
- Serial test mode.

### Serial Test Mode

The serial monitor operates at 115200 baud.

| Command | Function |
|---|---|
| `1` | Force LOW level |
| `2` | Force MODERATE level |
| `3` | Force HIGH level |
| `4` | Force VERY HIGH level |
| `0` | Cancel test mode |

Each forced level remains active for 15 seconds. Test-mode readings are not uploaded to the cloud.

## 13. Repository Structure

A recommended GitHub repository structure is:

```text
UV-Guard/
│
├── README.md
│
├── Firmware/
│   ├── UV_Guard.ino
│   └── secrets.example.h
│
├── Hardware/
│   ├── circuit_diagram.png
│   ├── system_architecture.png
│   ├── pin_configuration.png
│   └── prototype.jpg
│
├── Dashboard/
│   └── thingspeak_dashboard.png
│
├── Documentation/
│   └── UV_Guard_Project_Report.pdf
│
└── LICENSE
```

*The structure above is a suggested repository organization. Add the corresponding files when they are available.*

## 14. Software Requirements

- Arduino IDE.
- ESP8266 Arduino Core.
- NodeMCU 1.0 (ESP-12E Module) board support.
- Adafruit GFX Library.
- Adafruit ST7735 and ST7789 Library.
- ESP8266WiFi.
- ESP8266HTTPClient.
- SPI.
- ThingSpeak account and configured channel.

## 15. Installation and Execution

### Step 1 – Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/UV-Guard.git

cd UV-Guard
```

### Step 2 – Install Board Support

Open Arduino IDE and install the ESP8266 board package through Boards Manager.

Select:

```text
Board: NodeMCU 1.0 (ESP-12E Module)
```

### Step 3 – Install Required Libraries

Install the following through Arduino IDE Library Manager:

```text
Adafruit GFX Library
Adafruit ST7735 and ST7789 Library
```

### Step 4 – Configure Credentials

Create a private `secrets.h` file using the example template:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define THINGSPEAK_WRITE_API_KEY "YOUR_WRITE_API_KEY"

#endif
```

Include the credentials file in the firmware and ensure that the actual `secrets
