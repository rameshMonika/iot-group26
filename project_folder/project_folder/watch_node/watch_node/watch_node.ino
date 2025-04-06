// === FILE: Watch Node (Plain Compact JSON Sender with WiFi Timestamp) ===

#include "LoRaBoards.h"
#include <RadioLib.h>
#include <WiFi.h>
#include <time.h>

const char* ssid = "naddy";
const char* password = "1312nadrah";

SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
String deviceID = "Watch_001";
String nodeID = "Node_X";
int rssi = -10;

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed.");
  }
}

void setup() {
  setupBoards();
  Serial.begin(115200);
  delay(1000);

#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  connectToWiFi();
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  radio.begin();
  radio.setFrequency(2400.0);              // Assuming you're using 2.4 GHz LoRa — okay
  radio.setBandwidth(812.5);               // Max BW = faster data rate for short range
  radio.setSpreadingFactor(7);             // Lower SF = faster, ideal for short range
  radio.setCodingRate(5);                  // 4/5 is standard, fast and reliable
  radio.setSyncWord(0xAB);                 // OK, optional if using unique network ID
  radio.setOutputPower(3);                 // Keep low for indoor tests (can go up to ~10 if needed)
  radio.setPreambleLength(8);              // Shorter preamble = faster startup, less delay
  radio.setCRC(true);       

#ifdef RADIO_RX_PIN
  radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

  Serial.println("[" + deviceID + "] Ready to send packets");
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void loop() {
  String timestamp = getCurrentTimestamp();
  if (timestamp == "") {
    Serial.println("[" + deviceID + "] ⚠️ Timestamp not available.");
    delay(2000);
    return;
  }

  String jsonPayload = "{\"n\":\"" + nodeID + "\",\"d\":\"" + deviceID + "\",\"t\":\"" + timestamp + "\",\"r\":" + String(rssi) + "}";
  Serial.println("[" + deviceID + "] 📤 Sending: " + jsonPayload);

  radio.standby();
  delay(5);
  int state = radio.transmit(jsonPayload);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[" + deviceID + "] ✅ Sent via LoRa");
  } else {
    Serial.print("[" + deviceID + "] ❌ Send failed, code ");
    Serial.println(state);
  }

  delay(5000);
}
