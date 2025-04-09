// === FILE: Encrypted Watch Node with Fixed Fields ===

#include "LoRaBoards.h"                                                 // LilyGo Lirary (SX1280 with PA)
#include <RadioLib.h>                                                   // Transmit & Receiving Library 
#include <WiFi.h>                                                       // Wifi Library 
#include <time.h>                                                       // Time Library 
#include <AES.h>                                                        // AES Encryptions & Library
#include <Crypto.h>                                                     // AES Decryptions
#include <string.h>                                                     // String Manipulations Library

#include <Wire.h>                                                       // LED display Library
#include "SSD1306Wire.h"                                                // LED display Library

SSD1306Wire display(0x3C, SDA, SCL);                                    // Default IC2 Display Pins

// Wifi Configurations 
const char* ssid = "naddy";
const char* password = "1312nadrah";

SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);       //Initialize LoRa Module using Pin Configurations
String deviceID = "Watch_001";                                          //Sets the unique ID for watch 
String nodeID = "Node_X";                                               // Static node ID
int rssi = -10;                                                         // Default RSSI placeholder

byte aesKey[16] = {                                                     //AES-128 Encryption Key shared between all nodes
  0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
  0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81
};
AES128 aes;                                                             //AES cipher engine used for decrypt and encrypt data

String buildPayload() {                                                 // Build JSON payload with fixed fields
  String timestamp = getCurrentTimestamp();
  return "{\"n\":\"" + nodeID +
         "\",\"d\":\"" + deviceID +
         "\",\"t\":\"" + timestamp +
         "\",\"r\":" + String(rssi) + "}";
}

void connectToWiFi() {                                                   // Starts WiFi connection process
  Serial.print("Connecting to WiFi");                                    // Prints status to serial monitor
  WiFi.begin(ssid, password);                                            // Starts connecting using credentials
  int retries = 0;                                                       // Initializes retry counter variable
  while (WiFi.status() != WL_CONNECTED && retries < 20) {                // Keep trying until connected or limit
    delay(500);                                                          // Wait 500 milliseconds per attempt
    Serial.print(".");                                                   // Print dot as connection progress
    retries++;                                                           // Increments retry count by one
  }
  if (WiFi.status() == WL_CONNECTED) {                                   // If connected, print IP address
    Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());// Show IP address on serial monitor
  } else {                                                                
    Serial.println("\nWiFi connection failed.");                         // Notify user that WiFi failed
  }
}

String getCurrentTimestamp() {                                           // Returns formatted current timestamp string
  struct tm timeinfo;                                                    // Holds time components (year, etc.)
  if (!getLocalTime(&timeinfo)) {                                        
    return "";                                                           // Get system time or fail
  }
  char buffer[25];                                                       // Buffer to store formatted time
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);      // Format time into readable string
  return String(buffer);                                                 // Convert buffer to String object
}

String encryptAES(const String& plaintext) {                             // Function to encrypt plaintext string
  char input[64] = {0};                                                  // Input buffer for AES block
  char output[64] = {0};                                                 // Output buffer for ciphertext

  String padded = plaintext;                                             // Copy input to pad safely
  while (padded.length() % 16 != 0) padded += ' ';                       // Pad string to AES block size

  padded.toCharArray(input, sizeof(input));                              // Convert string to char array
  aes.setKey(aesKey, sizeof(aesKey));                                    // Set AES encryption key

  for (int i = 0; i < padded.length(); i += 16) {                        // Loop through 16-byte blocks
    aes.encryptBlock((uint8_t*)&output[i], (uint8_t*)&input[i]);         // Encrypt one AES block chunk
  }

  String encryptedHex = "";                                              // Prepare hex string output
  for (int i = 0; i < padded.length(); i++) {                            // Loop through encrypted data
    if ((uint8_t)output[i] < 16) encryptedHex += "0";                    // Add leading zero if needed
    encryptedHex += String((uint8_t)output[i], HEX);                     // Append byte as hex string
  }

  return encryptedHex;                                                   // Return final encrypted result
}

void setup() {                                                           // Start initialization for device
  setupBoards();                                                         // Setup hardware pins/configs
  Serial.begin(115200);                                                  // Start serial for debugging
  delay(1000);                                                           // Wait briefly before setup continues
 
#ifdef RADIO_TCXO_ENABLE                                                 // Enable TCXO/power amplifier pin (if defined)
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);                                   
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  connectToWiFi();                                                       // Connect to Wi-Fi network
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");              // Set timezone and sync NTP

  struct tm timeinfo;                                                    // Wait until NTP time available
  while (!getLocalTime(&timeinfo)) {
    Serial.println("⏳ Waiting for NTP time...");
    delay(500);
  }

  WiFi.disconnect(true);                                                 // Turn off Wi-Fi to save power
  WiFi.mode(WIFI_OFF);

  display.init();                                                        // Initialize and configure OLED display
  display.flipScreenVertically();   
  display.setFont(ArialMT_Plain_16);

  display.clear();                                                       // Show deviceID on OLED screen
  display.drawString(0, 0, "Device ID:");
  display.drawString(0, 20, deviceID);
  display.display();

  radio.begin();                                                         // Start LoRa radio module
  radio.setFrequency(2400.0);                                            // Configure LoRa radio parameters
  radio.setBandwidth(812.5);
  radio.setSpreadingFactor(7);
  radio.setCodingRate(5);
  radio.setSyncWord(0xAB);
  radio.setOutputPower(3);
  radio.setPreambleLength(8);
  radio.setCRC(true);

#ifdef RADIO_RX_PIN                                                       // Set RF switch pins if available
  radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

  Serial.println("[" + deviceID + "] ✅ NTP time synced");                // Confirm setup done in serial monitor
  Serial.println("[" + deviceID + "] Ready to send packets");
}


void loop() {                                                              // Repeats continuously after setup()
  String payload = buildPayload();                                         // Create JSON payload with timestamp
  Serial.println("[" + deviceID + "] 📤 Original: " + payload);           // Print raw JSON to serial monitor

  String encryptedPayload = encryptAES(payload);                          // Encrypt payload using AES-128
  if (encryptedPayload.length() > 255) {                                  // Skip sending if encrypted string too long
    Serial.println("❌ Payload too long to send over LoRa");             
    return;
  }

  Serial.println("[" + deviceID + "] 🔒 Encrypted (hex): " + encryptedPayload);   //Print encrypted hex string to serial

  radio.standby();                                                         // Ensure radio is ready for transmission
  delay(5);
  int state = radio.transmit(encryptedPayload);                            // Send encrypted payload over LoRa

  if (state == RADIOLIB_ERR_NONE) {                                        // Print whether transmission was successful
    Serial.println("[" + deviceID + "] ✅ Sent via LoRa");
  } else {
    Serial.print("[" + deviceID + "] ❌ Send failed, code ");
    Serial.println(state);
  }

  delay(5000);
}
