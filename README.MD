iot-modified
--------------
used to subscribe to sensor/alerts to send telegram message and alerts

MasterNode: 
--------------
script to publish to the topic "sensor/data" 

RaspberryPi: 
--------------
script to subscribe to the "sensor/data" to receive and process the data from the master node, and then publish to "sensor/alerts" for cloud to receive


project-folder
----------------------
Open Arduino IDE  

Install esp32 by Expressif for ESP32 Board Modules 

Download and Follow the instruction from the following github : 
https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series 