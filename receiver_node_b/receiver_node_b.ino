// === FILE: Receiver Node (Decrypt ➜ Modify ➜ Encrypt ➜ Forward) ===

#include "LoRaBoards.h"                                     // LilyGo Lirary (SX1280 with PA)
#include <RadioLib.h>                                       // Transmit & Receiving Library 
#include <AES.h>                                            // AES Encryptions & Library
#include <Crypto.h>                                         // AES Decryptions

#include <Wire.h>                                           // LED display Library
#include "SSD1306Wire.h"

SSD1306Wire display(0x3C, SDA, SCL);                        // Default IC2 Display Pins

SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);    // Initialize LoRa Module using Pin Configurations
String nodeID = "Node_B";                                   // Set uniquely for each receiver node
volatile bool receivedFlag = false;                         // Flag to indicate packet reception
String payload;                                             // Storage for received packet data

// AES-128 key (shared among all nodes)
byte aesKey[16] = {
  0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
  0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81
};
AES128 aes;

// Slot delay settings (tweak per receiver)
const int mySlot = 2;       // Set to 0,1,2 for different nodes
const int slotDelay = 5000;  // ms per slot

// Deduplication cache
struct DeviceRecord {
  String id;
  String lastTimestamp;
};
DeviceRecord knownDevices[5];

void setFlag() {                                             // Set flag when packet is received
  receivedFlag = true;
}

void setup() {
  setupBoards();                                             // Initialize hardware 
  Serial.begin(115200);                                      // Start serial communication
  delay(1000);

#ifdef RADIO_TCXO_ENABLE                                     // Enable TCXO if defined
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif
  
  // Initialize OLED display
  display.init();
  display.flipScreenVertically(); 
  display.setFont(ArialMT_Plain_16);

  display.clear();
  display.drawString(0, 0, "Node ID:");
  display.drawString(0, 20, nodeID);
  display.display();

  // Initialize LoRa radio
  radio.begin();
  radio.setPacketReceivedAction(setFlag); // Set ISR for packet reception
  radio.setFrequency(2400.0);             // Set frequency (2.4 GHz)
  radio.setBandwidth(812.5);              // Set bandwidth
  radio.setSpreadingFactor(7);            // Set spreading factor
  radio.setCodingRate(5);                 // Set coding rate
  radio.setSyncWord(0xAB);                // Sync word to match network
  radio.setOutputPower(3);                // Transmit power
  radio.setPreambleLength(8);             // Preamble length
  radio.setCRC(true);                     // Enable CRC check

#ifdef RADIO_RX_PIN                       // RF switch setup
  radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

  radio.startReceive();                   // Start listening
  Serial.println("[" + nodeID + "] 🔊 Listening for encrypted packets...");
}

// Check if device and timestamp have already been seen to avoid duplicates
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
  // Add new device if not found
  for (int i = 0; i < 5; i++) {
    if (knownDevices[i].id == "") {
      knownDevices[i].id = devID;
      knownDevices[i].lastTimestamp = ts;
      return false;
    }
  }
  return false;
}
// Decrypt AES-encrypted hex string
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

// Encrypt plaintext string into AES-encrypted hex
String encryptAES(const String& plaintext) {
  char input[64] = {0};
  char output[64] = {0};

  // Pad the plaintext to be multiple of 16 bytes
  String padded = plaintext;
  while (padded.length() % 16 != 0) padded += ' ';

  padded.toCharArray(input, sizeof(input));
  aes.setKey(aesKey, sizeof(aesKey));
  
  // Encrypt 16-byte blocks
  for (int i = 0; i < padded.length(); i += 16) {
    aes.encryptBlock((uint8_t*)&output[i], (uint8_t*)&input[i]);
  }
  
  // Convert encrypted bytes to hex string
  String encryptedHex = "";
  for (int i = 0; i < padded.length(); i++) {
    if ((uint8_t)output[i] < 16) encryptedHex += "0";
    encryptedHex += String((uint8_t)output[i], HEX);
  }

  return encryptedHex;
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;
    
    if (radio.readData(payload) == RADIOLIB_ERR_NONE) {
      // Decrypt incoming data
      String decrypted = decryptAES(payload);
      Serial.println("[" + nodeID + "] 🔓 Decrypted: " + decrypted);

      // Extract fields from decrypted JSON string
      int nStart = decrypted.indexOf("\"n\":\"") + 5;
      int nEnd = decrypted.indexOf("\"", nStart);
      String originalNode = (nStart > 4 && nEnd > nStart) ? decrypted.substring(nStart, nEnd) : "";

      int dStart = decrypted.indexOf("\"d\":\"") + 5;
      int dEnd = decrypted.indexOf("\"", dStart);
      int tStart = decrypted.indexOf("\"t\":\"") + 5;
      int tEnd = decrypted.indexOf("\"", tStart);
      
      // Validate data
      if (dStart <= 4 || dEnd <= dStart || tStart <= 4 || tEnd <= tStart) {
        Serial.println("[" + nodeID + "] ⚠️ Malformed packet, ignored.");
        radio.startReceive();
        delay(300);
        return;
      }

      String devID = decrypted.substring(dStart, dEnd);
      String timeVal = decrypted.substring(tStart, tEnd);
      
      // Check for duplicates
      if (alreadySeen(devID, timeVal)) {
        Serial.println("[" + nodeID + "] 🔁 Duplicate detected, ignored.");
        radio.startReceive();
        delay(300);
        return;
      }
      
      // Only forward messages from Node_X
      if (originalNode != "Node_X") {
        Serial.println("[" + nodeID + "] 🚫 Not from Node_X, not forwarded.");
        radio.startReceive();
        delay(300);
        return;
      }
      
      // Capture RSSI value
      int rssi = (int)radio.getRSSI();

      // Modify "n" (node ID) and "r" (RSSI)
      decrypted.replace("\"n\":\"" + originalNode + "\"", "\"n\":\"" + nodeID + "\"");

      int rStart = decrypted.indexOf("\"r\":");
      if (rStart != -1) {
        int rEnd = decrypted.indexOf("}", rStart);
        if (rEnd == -1) rEnd = decrypted.length(); // fallback if there's no trailing }

        String oldRSSIEntry = decrypted.substring(rStart, rEnd);
        String newRSSIEntry = "\"r\":" + String(rssi);

        decrypted.replace(oldRSSIEntry, newRSSIEntry);
      }
      
      // Wait for assigned time slot to avoid collisions
      Serial.println("[" + nodeID + "] ⏳ Waiting for slot...");
      delay(mySlot * slotDelay);  // Staggered send window
      
      // Encrypt modified message and send
      String reEncrypted = encryptAES(decrypted);
      Serial.println("[" + nodeID + "] 🔒 Re-encrypted: " + reEncrypted);

      int state = radio.transmit(reEncrypted);

      if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[" + nodeID + "] ✅ Encrypted packet sent to Master");
      } else {
        Serial.print("[" + nodeID + "] ❌ Transmission failed, code ");
        Serial.println(state);
      }

      delay(300);
    } else {
      Serial.println("[" + nodeID + "] ❌ Failed to read LoRa data.");
      delay(300);
    }

    delay(3000);           // Cooldown before listening again

    radio.startReceive();  // Resume listening
  }
}
