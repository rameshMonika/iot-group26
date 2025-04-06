// === FILE: Receiver Node (Resend Watch Packet to Master with Smart Deduplication) ===

#include "LoRaBoards.h"
#include <RadioLib.h>

SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
String nodeID = "Node_D";  // Receiver Node ID
volatile bool receivedFlag = false;
String payload;

// Store recent device/timestamp combos for deduplication
struct DeviceRecord {
  String id;
  String lastTimestamp;
};
DeviceRecord knownDevices[5]; // Support 5 devices

void setFlag() {
  receivedFlag = true;
}

void setup() {
  setupBoards();
  Serial.begin(115115);
  delay(1000);

#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  radio.begin();
  radio.setPacketReceivedAction(setFlag);  // Keep this for handling received packets
  radio.setFrequency(2400.0);              // Assuming you're using 2.4 GHz LoRa — okay
  radio.setBandwidth(812.5);               // Max BW = faster data rate for short range
  radio.setSpreadingFactor(7);             // Lower SF = faster, ideal for short range
  radio.setCodingRate(5);                  // 4/5 is standard, fast and reliable
  radio.setSyncWord(0xAB);                 // OK, optional if using unique network ID
  radio.setOutputPower(3);                 // Keep low for indoor tests (can go up to ~10 if needed)
  radio.setPreambleLength(8);              // Shorter preamble = faster startup, less delay
  radio.setCRC(true);                      // Enable CRC for packet integrity


#ifdef RADIO_RX_PIN
  radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

  radio.startReceive();
  Serial.println("[" + nodeID + "] Listening for packets...");
}

bool alreadySeen(String devID, String ts) {
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].id == devID) {
      if (knownDevices[i].lastTimestamp == ts) {
        return true;
      } else {
        knownDevices[i].lastTimestamp = ts;
        return false;
      }
    }
  }
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].id == "") {
      knownDevices[i].id = devID;
      knownDevices[i].lastTimestamp = ts;
      return false;
    }
  }
  return false;
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    if (radio.readData(payload) == RADIOLIB_ERR_NONE) {
      Serial.println("[" + nodeID + "] 📡 Received: " + payload);

      // Extract original node BEFORE replacing
      int nStart = payload.indexOf("\"n\":\"") + 5;
      int nEnd = payload.indexOf("\"", nStart);
      String originalNode = (nStart > 4 && nEnd > nStart) ? payload.substring(nStart, nEnd) : "";

      int dStart = payload.indexOf("\"d\":\"") + 5;
      int dEnd = payload.indexOf("\"", dStart);
      int tStart = payload.indexOf("\"t\":\"") + 5;
      int tEnd = payload.indexOf("\"", tStart);

      if (dStart <= 4 || dEnd <= dStart || tStart <= 4 || tEnd <= tStart) {
        Serial.println("[" + nodeID + "] ⚠️ Malformed packet, ignored.");
        radio.startReceive();
        delay(300);
        return;
      }

      String devID = payload.substring(dStart, dEnd);
      String timeVal = payload.substring(tStart, tEnd);

      if (alreadySeen(devID, timeVal)) {
        Serial.println("[" + nodeID + "] 🔁 Duplicate for this device/timestamp, ignored.");
        radio.startReceive();
        delay(300);
        return;
      }

      if (originalNode != "Node_X") {
        Serial.println("[" + nodeID + "] 🚫 Not from Node_X, not forwarded.");
        radio.startReceive();
        delay(300);
        return;
      }

      int rssi = (int)radio.getRSSI();

      // Replace node ID and RSSI
      payload.replace("\"n\":\"" + originalNode + "\"", "\"n\":\"" + nodeID + "\"");
      int rStart = payload.indexOf("\"r\":") + 4;
      int rEnd = payload.indexOf("}", rStart);
      if (rStart > 3 && rEnd > rStart) {
        payload.replace(payload.substring(rStart, rEnd), String(rssi));
      }

      Serial.println("[" + nodeID + "] 📤 Forwarding to Master: " + payload);

      int state = radio.transmit(payload);

      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[" + nodeID + "] ✅ JSON sent to Master");
      } else {
        Serial.print("[" + nodeID + "] ❌ Transmission failed, code ");
        Serial.println(state);
      }

      delay(300);
    } else {
      Serial.println("[" + nodeID + "] ❌ Failed to read LoRa data.");
      delay(300);
    }
  }
}