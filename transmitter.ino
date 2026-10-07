#include <SPI.h>
#include <cc1101.h>
#include <TinyGPS.h>
#include <AESLib.h>

AESLib aesLib;

byte aes_key[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 
                   0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };

using namespace CC1101;
const int CS_PIN = 1; 
Radio radio(CS_PIN);

const int GPS_RX_PIN = 20;
const int GPS_TX_PIN = 21;
const int BATTERY_PIN = 0;
HardwareSerial gpsSerial(1);
TinyGPS gps;

#define TIME_TO_SLEEP_SEC  (1 * 60)
#define uS_TO_S_FACTOR     1000000ULL

void setup() {
  Serial.begin(115200);
  delay(1000);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  SPI.begin(3, 4, 2, 1);

  analogSetAttenuation(ADC_11db);

  bool radioReady = true;
  if (radio.begin() == STATUS_CHIP_NOT_FOUND) {
    Serial.println("CC1101 chip not found!");
    radioReady = false;
  } else {
    radio.setModulation(MOD_ASK_OOK);
    radio.setFrequency(433.92);
    radio.setOutputPower(10);
    radio.setPacketLengthMode(PKT_LEN_MODE_VARIABLE);
    aesLib.set_paddingmode((paddingMode)0);
  }

  if (radioReady) {
    Serial.println("Woke up. Searching for GPS fix...");

    unsigned long gpsStartTime = millis();
    bool gotFix = false;
    float flat = 0, flon = 0;
    unsigned long age = 0;

    while (millis() - gpsStartTime < 45000) {
      while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
      }
      gps.f_get_position(&flat, &flon, &age);
      if (age != TinyGPS::GPS_INVALID_AGE && age < 5000) {
        gotFix = true;
        break;
      }
    }

    if (gotFix) {
      int32_t lat_int = (int32_t)(flat * 1e6);
      int32_t lon_int = (int32_t)(flon * 1e6);

      long rawSum = 0;
      for (int i = 0; i < 5; i++) {
        rawSum += analogRead(BATTERY_PIN);
        delay(5);
      }
      int rawValue = rawSum / 5;

      float pinVoltage = (rawValue / 2800.0) * 2.075; 
      float batteryVoltage = pinVoltage * 2.0;

      float pct = (batteryVoltage - 3.3) / (4.15 - 3.3) * 100.0;
      if (pct < 0.0) pct = 0.0;
      if (pct > 100.0) pct = 100.0;
      uint8_t battery_pct = (uint8_t)pct;

      byte payload[16] = {0};
      memcpy(payload, &lat_int, sizeof(lat_int));
      memcpy(payload + 4, &lon_int, sizeof(lon_int));
      payload[8] = battery_pct;

      byte packet[64] = {0};
      byte iv[16];
      aesLib.gen_iv(iv);
      
      memcpy(packet, iv, 16);
      uint16_t cipherLen = aesLib.encrypt(payload, sizeof(payload), packet + 16, aes_key, 128, iv);
      uint8_t totalPacketSize = 16 + cipherLen;

      Status status = radio.transmit(packet, totalPacketSize);
      if (status == STATUS_OK) {
        Serial.print("Sent Lat: "); Serial.print(flat, 6);
        Serial.print(" Lon: "); Serial.print(flon, 6);
        Serial.print(" | Voltage: "); Serial.print(batteryVoltage); 
        Serial.print("V | Battery: "); Serial.print(battery_pct); Serial.println("%");
      } else {
        Serial.println("Transmission failed.");
      }
    } else {
      Serial.println("GPS fix timeout. Skipping transmission for this cycle.");
    }
  }

  esp_sleep_enable_timer_wakeup((uint64_t)TIME_TO_SLEEP_SEC * uS_TO_S_FACTOR);
  Serial.println("Entering deep sleep for 5 minutes...");
  Serial.flush();
  
  esp_deep_sleep_start();
}

void loop() {
}