import paho.mqtt.client as mqtt
import json
import numpy as np

# Global dictionary to hold aggregated data per device.
# Structure:
#   aggregated_data = {
#       device_id: {
#           "device_id": device_id,
#           "nodes": {          # holds the most recent reading from each node
#               "Node_A": { ... },
#               "Node_B": { ... },
#               "Node_C": { ... },
#               "Node_D": { ... }
#           }
#       },
#       ...
#   }
aggregated_data = {}

# Global dictionary to maintain moving average history for each node.
moving_average_history = {}
WINDOW_SIZE = 5

# Mapping of node_id to its physical location (x, y)
node_locations = {
    "Node_A": [2, 5],
    "Node_B": [10, 2],
    "Node_C": [5, 7],
    "Node_D": [9, 2]
}

# Define the safe zone as a rectangular region.
# Lower-left corner (4, 0.2) and upper-right corner (6.7, 5.0)
SAFE_ZONE = {
    "xmin": 4.0,
    "xmax": 6.7,
    "ymin": 0.2,
    "ymax": 5.0
}

### Helper Functions ###

def validate_message(payload):
    """
    Validate that the message has the required keys and valid values.
    """
    required_keys = ["device_id", "node_id", "RSSI", "timestamp"]
    for key in required_keys:
        if key not in payload:
            return False, f"Missing key: {key}"
    try:
        rssi_val = float(payload["RSSI"])
    except Exception as e:
        return False, "RSSI is not a valid number"
    if rssi_val < -150 or rssi_val > 0:
        return False, f"RSSI value {rssi_val} out of range"
    if payload["node_id"] not in node_locations:
        return False, f"Unknown node_id: {payload['node_id']}"
    return True, "Valid"

def noise_filter(node_id, rssi):
    """
    Apply a moving average filter to the RSSI for the given node.
    It maintains a history of the last WINDOW_SIZE readings and returns the average.
    """
    if node_id not in moving_average_history:
        moving_average_history[node_id] = []
    moving_average_history[node_id].append(rssi)
    if len(moving_average_history[node_id]) > WINDOW_SIZE:
        moving_average_history[node_id].pop(0)
    avg = sum(moving_average_history[node_id]) / len(moving_average_history[node_id])
    return avg

def rssi_to_distance(filtered_rssi):
    """
    Convert filtered RSSI to distance using a simple linear model.
    For testing: distance = (abs(filtered_rssi) - 30) / 10.
    """
    distance = (abs(filtered_rssi) - 30) / 10
    return round(distance, 1)

def trilateration(distance_data):
    """
    Compute an estimated (x,y) location using a least-squares trilateration approach.
    If fewer than 3 nodes are available, uses a fallback weighted average method.
    """
    if len(distance_data) < 3:
        total_weight = 0
        x_sum = 0
        y_sum = 0
        for data in distance_data:
            d = data["distance"]
            weight = 1 / d if d != 0 else 1
            total_weight += weight
            x_sum += data["location"][0] * weight
            y_sum += data["location"][1] * weight
        return [round(x_sum / total_weight, 1), round(y_sum / total_weight, 1)]
    
    # Use the first node as the reference.
    ref = distance_data[0]
    x1, y1 = ref["location"]
    d1 = ref["distance"]
    
    A = []
    b = []
    for data in distance_data[1:]:
        xi, yi = data["location"]
        di = data["distance"]
        A.append([2 * (xi - x1), 2 * (yi - y1)])
        b.append(xi**2 - x1**2 + yi**2 - y1**2 + di**2 - d1**2)
    A = np.array(A)
    b = np.array(b)
    solution, residuals, rank, s = np.linalg.lstsq(A, b, rcond=None)
    estimated_x = round(solution[0], 1)
    estimated_y = round(solution[1], 1)
    return [estimated_x, estimated_y]

def is_within_safe_zone(location):
    """
    Check if the (x, y) location is within the SAFE_ZONE boundaries.
    """
    x, y = location
    return (SAFE_ZONE["xmin"] <= x <= SAFE_ZONE["xmax"] and
            SAFE_ZONE["ymin"] <= y <= SAFE_ZONE["ymax"])

def process_aggregated_data(agg_entry):
    """
    Process the data by:
      1. Applying a moving average filter.
      2. Converting filtered RSSI to distances.
      3. Performing trilateration.
      4. Checking the geofence and publishing alerts.
    """
    # Apply noise filtering for each node’s RSSI
    filtered_data = []
    for entry in agg_entry["RSSI_data"]:
        original_rssi = entry["RSSI"]
        filtered_rssi = noise_filter(entry["node_id"], original_rssi)
        filtered_data.append({
            "node_id": entry["node_id"],
            "filtered_RSSI": filtered_rssi,
            "location": entry["location"]
        })
    
    # Convert the filtered RSSI to distance for each node
    distance_data = []
    for entry in filtered_data:
        distance = rssi_to_distance(entry["filtered_RSSI"])
        distance_data.append({
            "node_id": entry["node_id"],
            "distance": distance,
            "location": entry["location"]
        })
    
    # Estimate location using trilateration
    estimated_location = trilateration(distance_data)
    
    print("Aggregated Data:")
    print(json.dumps(agg_entry, indent=2))
    print("\nDistance Data:")
    print(json.dumps({
        "device_id": agg_entry["device_id"],
        "timestamp": agg_entry["timestamp"],
        "distance_data": distance_data
    }, indent=2))
    print("\nEstimated Location (Trilateration):", estimated_location)
    
    # Perform geofencing check
    inside_zone = is_within_safe_zone(estimated_location)
    status = "Safe" if inside_zone else "ALERT: Patient Outside Safe Zone!"
    
    final_result = {
        "device_id": agg_entry["device_id"],
        "timestamp": agg_entry["timestamp"],
        "status": status,
        "location": estimated_location
    }
    print("\nGeofencing & Alerts:")
    print(json.dumps(final_result, indent=2))
    print("-" * 50, "\n")
    
    # Publish the final result to the alerts topic
    client.publish("sensor/alerts", json.dumps(final_result))

### MQTT Callback Functions ###

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT Broker with result code {rc}")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    global aggregated_data
    try:
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        print("Failed to decode JSON:", e)
        return

    # Validate the incoming message
    valid, validation_msg = validate_message(payload)
    if not valid:
        print("Invalid message:", validation_msg)
        return

    # Extract fields from the payload
    device_id = payload["device_id"]
    node_id = payload["node_id"]
    rssi = float(payload["RSSI"])
    timestamp = payload["timestamp"]

    # Aggregate by device_id. For each device, we store the most recent data per node.
    if device_id not in aggregated_data:
        aggregated_data[device_id] = {
            "device_id": device_id,
            "nodes": {}  # key: node_id, value: reading
        }
    
    # Update the entry for this node (overwriting any previous reading)
    aggregated_data[device_id]["nodes"][node_id] = {
        "node_id": node_id,
        "RSSI": rssi,
        "timestamp": timestamp,
        "location": node_locations[node_id]
    }
    
    print(f"Received Data: Device: {device_id}, Node: {node_id}, RSSI: {rssi}, Timestamp: {timestamp}")
    
    # Process only if we have readings from all 4 distinct nodes.
    if len(aggregated_data[device_id]["nodes"]) == 4:
        # Build the list from the node data
        node_list = list(aggregated_data[device_id]["nodes"].values())
        # Compute the maximum timestamp numerically
        max_ts = max([int(n["timestamp"]) for n in node_list])
        max_timestamp = str(max_ts)
        
        agg_entry = {
            "device_id": device_id,
            "timestamp": max_timestamp,
            "RSSI_data": node_list
        }
        
        process_aggregated_data(agg_entry)
        
        # Clear the stored nodes after processing so that a new set must be received.
        aggregated_data[device_id]["nodes"] = {}

### MQTT Configuration ###
MQTT_BROKER = "localhost"   # Broker is running on the Raspberry Pi itself
MQTT_PORT = 1883
MQTT_TOPIC = "sensor/data"

# Create MQTT client and assign callbacks.
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Connect and start the loop.
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()
