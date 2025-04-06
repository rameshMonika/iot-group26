iot-modified
--------------
used to subscribe to sensor/alerts to send telegram message and alerts

to run -> cd iot-modified -> docker-compose up -d --build


MasterNode: 
--------------
script to publish to the topic "sensor/data" 

RaspberryPi: 
--------------
script to subscribe to the "sensor/data" to receive and process the data from the master node, and then publish to "sensor/alerts" for cloud to receive


project-folder
----------------------
consist of reciever_node_b and c

Receiver Node A, B, C & D : receives the lora packet from the watch node via lora and send to Master Node

Open Arduino IDE  

Install esp32 by Expressif for ESP32 Board Modules 

Download and Follow the instruction from the following github : 
https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series 