#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "CowHead";
const char* password = "xyec7603";
const char* mqtt_server = "192.168.30.146";  // Example: "192.168.1.100"

WiFiClient espClient;
PubSubClient client(espClient);

// Define the node IDs and their baseline RSSI values for simulation
const char* nodes[4] = {"Node_A", "Node_B", "Node_C", "Node_D"};
// Baseline RSSI values for each node (in dBm)
int baseRSSI[4] = { -40,- 43, -45, -47 };

unsigned long roundTimestamp = 17102025;  // Starting timestamp

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
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Master")) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  // Seed the random generator for variation
  randomSeed(analogRead(0));
  setup_wifi();
  client.setServer(mqtt_server, 1883);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Increment the timestamp for each round (for testing aggregation)
  roundTimestamp++;

  // Publish one message per node with a small random variation in RSSI
  for (int i = 0; i < 4; i++) {
    // Generate a small variation between -3 and +2
    int variation = random(-3, 3);
    int simulatedRSSI = baseRSSI[i] + variation;
    
    // Construct the JSON message
    String message = "{\"device_id\": \"Watch_001\", \"node_id\": \"";
    message += nodes[i];
    message += "\", \"RSSI\": ";
    message += String(simulatedRSSI);
    message += ", \"timestamp\": ";
    message += String(roundTimestamp);
    message += "}";
    
    // Publish the message to the MQTT topic "sensor/data"
    client.publish("sensor/data", message.c_str());
    Serial.println("Published: " + message);
    
    // Small delay between messages in one round
    delay(500);
  }
  
  // Wait 5 seconds before starting the next round of messages
  delay(5000);
}
