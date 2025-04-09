#include "LoRaBoards.h"
#include <RadioLib.h>
#include <AES.h>
#include <Crypto.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ------------------------ LoRa & Decryption Setup ------------------------

SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
String nodeID = "Node_A";  // Local identifier for this receiver (used for debugging)
volatile bool receivedFlag = false;
String payload;

// AES-128 key (shared among all nodes)
byte aesKey[16] = {
  0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
  0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81
};
AES128 aes;

// Deduplication cache for received nodes – stores a node id (extracted from the packet's "n" field)
// and the last time (millis) we published data for that node.
struct DeviceRecord {
  String id;
  unsigned long lastTime;  // in milliseconds
};
DeviceRecord knownDevices[5];  // Adjust the size as needed

// Called by the LoRa library when a packet is received
void setFlag() {
  receivedFlag = true;
}

// Decrypt the incoming packet (provided as a hexadecimal string) using AES-128 in 16-byte blocks.
String decryptAES(const String& encryptedHex) {
  char input[64] = {0};
  char output[64] = {0};
  int len = encryptedHex.length() / 2;

  for (int i = 0; i < len; i++) {
    input[i] = strtol(encryptedHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }

  aes.setKey(aesKey, sizeof(aesKey));
  for (int i = 0; i < len; i += 16) {
    aes.decryptBlock((uint8_t*)&output[i], (uint8_t*)&input[i]);
  }

  return String(output);
}

// Check if data from this node (extracted from the "n" field) has already been published within the last 2 minutes.
// If not, update the stored timestamp with the current time and return true; otherwise, return false.
bool shouldPublish(String sensorNode) {
  unsigned long currentTime = millis();
  const unsigned long twoMinutes = 60000UL;  // 1 minutes in milliseconds
  
  // Look for an existing record for this node
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].id == sensorNode) {
      if (currentTime - knownDevices[i].lastTime < twoMinutes) {
        // Data from this node has been published too recently.
        return false;
      } else {
        knownDevices[i].lastTime = currentTime;
        return true;
      }
    }
  }
  // No record exists for this node; add it in the first empty slot.
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].id == "") {
      knownDevices[i].id = sensorNode;
      knownDevices[i].lastTime = currentTime;
      return true;
    }
  }
  // If the deduplication buffer is full, you can choose to either overwrite an old record or simply ignore.
  // For now, we choose not to publish.
  return false;
}

// ------------------------ WiFi & MQTT Setup ------------------------

const char* ssid = "CowHead";
const char* password = "xyec7603";
const char* mqtt_server = "192.168.30.146";  // Replace with your MQTT broker's IP or hostname

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void reconnect() {
  // Loop until we're reconnected to the MQTT broker.
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Master")) {  // Use a unique client ID if needed
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds");
      delay(5000);
    }
  }
}

// ------------------------ Setup ------------------------

void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);

  // Setup WiFi and MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  // Setup LoRa boards and radio parameters
  setupBoards();
  delay(1000);
#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  radio.begin();
  radio.setPacketReceivedAction(setFlag);
  radio.setFrequency(2400.0);
  radio.setBandwidth(812.5);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setSyncWord(0xAB);
  radio.setOutputPower(3);
  radio.setPreambleLength(8);
  radio.setCRC(true);

#ifdef RADIO_RX_PIN
  radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

  radio.startReceive();
  Serial.println("[" + nodeID + "] 🔊 Listening for encrypted packets...");
}

// ------------------------ Main Loop ------------------------

void loop() {
  // Ensure the MQTT client is connected
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Check if a LoRa packet has been received
  if (receivedFlag) {
    receivedFlag = false;

    if (radio.readData(payload) == RADIOLIB_ERR_NONE) {
      String decrypted = decryptAES(payload);
      Serial.println("[" + nodeID + "] 🔓 Decrypted: " + decrypted);

      // Extract JSON-like fields: sender "n", device id "d", and timestamp "t"
      int nStart = decrypted.indexOf("\"n\":\"") + 5;
      int nEnd = decrypted.indexOf("\"", nStart);
      String packetNode = (nStart > 4 && nEnd > nStart) ? decrypted.substring(nStart, nEnd) : "";
      
      int dStart = decrypted.indexOf("\"d\":\"") + 5;
      int dEnd = decrypted.indexOf("\"", dStart);
      String devID = (dStart > 4 && dEnd > dStart) ? decrypted.substring(dStart, dEnd) : "";
      
      int tStart = decrypted.indexOf("\"t\":\"") + 5;
      int tEnd = decrypted.indexOf("\"", tStart);
      String packetTimestamp = (tStart > 4 && tEnd > tStart) ? decrypted.substring(tStart, tEnd) : "";
      
      if(packetNode == "" || devID == "" || packetTimestamp == "") {
        Serial.println("[" + nodeID + "] ⚠️ Missing fields, ignored.");
        radio.startReceive();
        delay(300);
        return;
      }
      
      // Change any received "Node_X" to "Node_A"
      if(packetNode == "Node_X") {
        packetNode = "Node_A";
      }

      // Use the extracted node id from the packet for deduplication and MQTT.
      if (!shouldPublish(packetNode)) {
        Serial.println("[" + nodeID + "] Duplicate packet from node " + packetNode + " received within 2 minutes, not sending MQTT.");
        radio.startReceive();
        delay(300);
        return;
      }

      // Retrieve the received signal strength indicator (RSSI)
      int rssi = (int)radio.getRSSI();
      Serial.println("[" + nodeID + "] 📡 Received packet from " + packetNode + " with RSSI: " + String(rssi));

      // Optionally, you could use the packet's own timestamp.
      // For this example, we use our current time for the MQTT timestamp.
      unsigned long currentTime = millis();

      // Build the MQTT JSON message with the extracted fields.
      String mqttMessage = "{";
      mqttMessage += "\"device_id\":\"" + devID + "\",";
      mqttMessage += "\"node_id\":\"" + packetNode + "\",";
      mqttMessage += "\"RSSI\":" + String(rssi) + ",";
      mqttMessage += "\"timestamp\":\"" + String(currentTime) + "\"";
      mqttMessage += "}";

      // Publish the message to the MQTT topic "sensor/data"
      if (client.publish("sensor/data", mqttMessage.c_str())) {
        Serial.println("Published MQTT message: " + mqttMessage);
      } else {
        Serial.println("Failed to publish MQTT message");
      }
    } else {
      Serial.println("[" + nodeID + "] ❌ Failed to read LoRa data.");
      delay(300);
    }

    delay(1000);
    radio.startReceive();  // Resume listening for the next packet
  }
}
