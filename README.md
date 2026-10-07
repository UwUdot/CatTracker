# 🐱 Off-Grid Cat Tracker

A long-range, battery-efficient cat tracking system built on ESP32-C3 Super Mini boards paired with CC1101 433 MHz radio modules. The transmitter rides on your cat's collar, periodically waking from deep sleep to grab a GPS fix and beam encrypted coordinates over the air. The receiver sits at home, decrypts incoming packets, and serves a live web dashboard over your local Wi-Fi network — no cloud, no subscriptions, no cellular data required.
## ✨ Features

    Fully off-grid RF link — 433.92 MHz CC1101 OOK modulation, no internet or cellular needed for tracking

    AES-128 encrypted packets with per-packet random IVs

    Ultra-low power transmitter — deep sleeps between transmissions (default: every 5 minutes)

    Battery monitoring on the collar with percentage reporting

    Live Leaflet.js map dashboard hosted directly on the receiver

    Captive-style Wi-Fi setup portal — no hardcoded credentials; configure via a browser on first boot

    mDNS support — access the dashboard at http://cc1101.local

    Signal quality indicator (RSSI-based) and offline detection on the dashboard

    Auto-recovery — bad Wi-Fi credentials trigger a reset back into setup mode

## 🧰 Hardware
Transmitter (Collar Unit)
Component	Notes
ESP32-C3 Super Mini	Main MCU
CC1101 433 MHz module	SPI radio
GPS module (NMEA, 9600 baud)	e.g. NEO-6M / ATGM336H
LiPo battery + divider	On BATTERY_PIN (GPIO 0)
Antenna	433 MHz tuned
Receiver (Home Base)
Component	Notes
ESP32-C3 Super Mini	Main MCU
CC1101 433 MHz module	SPI radio
USB power supply	Mains-powered
Pin Mapping (both units)
Function	GPIO
SPI SCK	3
SPI MISO	4
SPI MOSI	2
SPI CS (CC1101)	1
GPS RX (transmitter only)	20
GPS TX (transmitter only)	21
Battery ADC (transmitter only)	0
#📡 How It Works
text

┌────────────────────┐         433.92 MHz         ┌────────────────────┐
│   CAT COLLAR       │  ───────────────────────▶  │   HOME RECEIVER    │
│  ESP32-C3 + GPS    │   AES-128 + random IV      │  ESP32-C3 + Wi-Fi  │
│  CC1101 TX         │                            │  CC1101 RX         │
│  Deep sleep 5 min  │                            │  Web dashboard     │
└────────────────────┘                            └────────────────────┘

Transmitter cycle:

    Wake from deep sleep

    Wait up to 45 s for a valid GPS fix (skips transmission if none)

    Sample battery voltage (averaged over 5 reads)

    Pack lat (int32) | lon (int32) | battery (%) into a 16-byte payload

    Encrypt with AES-128-CBC and a freshly generated IV

    Transmit IV || ciphertext over CC1101

    Enter deep sleep for 5 minutes

Receiver:

    Boots into setup AP mode if no saved Wi-Fi credentials exist (SSID CC1101-Setup, password cc1101secure, portal at http://cc1101.local or http://192.168.4.1)

    Saves credentials to NVS and reboots into station mode

    Continuously listens for RF packets, decrypts them, and updates the live values

    Serves a mobile-friendly dashboard at http://cc1101.local with a live map, battery %, RSSI quality, and stale-packet detection

## 📦 Dependencies

Install via the Arduino Library Manager:

    cc1101 (or compatible CC1101 library exposing the CC1101::Radio API)

    AESLib

    TinyGPS (transmitter only)

    SPI, WiFi, WebServer, ESPmDNS, Preferences (bundled with the ESP32 Arduino core)

Board support: ESP32 Arduino Core (tested with ESP32-C3 Super Mini).

## 🚀 Getting Started
1. Flash the transmitter

    Open the transmitter sketch, adjust TIME_TO_SLEEP_SEC if desired (default 5 min), and upload to the collar ESP32-C3.

2. Flash the receiver

    Upload the receiver sketch to the home ESP32-C3.

3. First-boot Wi-Fi setup

    On your phone/laptop, connect to Wi-Fi network CC1101-Setup (password: cc1101secure)

    A captive portal-style page opens at http://192.168.4.1 (or http://cc1101.local)

    Enter your home Wi-Fi SSID and password

    The receiver saves them and reboots onto your home network

4. Open the dashboard

Once reconnected, browse to:
text

http://cc1101.local

You'll see the live map, last-known coordinates, battery level, signal strength, and a "last signal" age counter.

    ⚠️ Both units must share the same aes_key array. Change it from the default before deploying.

## 🔐 Security Notes

    The default AES key is a placeholder — replace it with 16 random bytes on both sides.

    AES-128-CBC with a random IV per packet prevents replay of identical ciphertexts, but there's no authentication tag — an attacker with the key could forge packets. For a hobby tracker this is acceptable; for anything critical, upgrade to AES-GCM.

    The setup AP uses a fixed default password (cc1101secure). Change it in setup() if you're worried about nearby attackers provisioning your device.

## ⚙️ Configuration Cheatsheet
    Constant	File	Default	Purpose
    aes_key	both	"0123456789 012345"	Shared AES-128 key
    CS_PIN	both	1	CC1101 chip select
    TIME_TO_SLEEP_SEC	TX	60 
    GPS_RX_PIN / GPS_TX_PIN	TX	20 / 21	GPS UART
    BATTERY_PIN	TX	0	Battery ADC input
    "CC1101-Setup"	RX	—	Setup AP SSID
    "cc1101secure"	RX	—	Setup AP password
    "cc1101"	RX	—	mDNS hostname

## 🗺️ Dashboard Preview

    Live OpenStreetMap view centered on the latest fix

    Color-coded status: 🟢 connected / 🟠 tracker offline / 🔴 home hub offline

    Signal strength qualifier: 🟢 strong (≥ −75 dBm) · 🟡 good (≥ −90 dBm) · 🟠 weak

    "Last signal" age counter, refreshed every second

    Auto-detects when the collar has gone silent > 80 s

## 🧪 Tuning Tips

    Range: OOK at 433.92 MHz with a decent antenna easily covers a suburban neighborhood. Bump radio.setOutputPower() on the TX (max ~10 dBm on CC1101) if you need more reach.

    Battery life: every extra second of GPS acquisition and every extra dBm of TX power costs you. Sleep longer, transmit less.

    Antenna matters more than power. A tuned quarter-wave whip on both ends beats cranking the PA.

    Collision avoidance: the RX only listens; if you ever add more collars, the transmitter should ideally listen-before-talk.

## 📁 Repository Layout

    .
    ├── transmitter/
    │   └── transmitter.ino     # Collar unit: GPS + AES + CC1101 TX + deep sleep
    ├── receiver/
    │   └── receiver.ino        # Home base: CC1101 RX + Wi-Fi setup portal + dashboard
    └── README.md

## 🙏 Acknowledgements

    CC1101 Arduino library

    AESLib

    TinyGPS

    Leaflet + OpenStreetMap for the map tiles

Happy tracking — and may your cat never learn to remove the collar. 🐾