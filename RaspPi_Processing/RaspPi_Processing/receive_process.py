import paho.mqtt.client as mqtt
import json
import numpy as np

# Global dictionary to hold aggregated data.
# Key is a tuple (device_id, timestamp), value is an aggregation dict.
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

# Define the safe zone as a rectangular region:
# Lower-left corner (4,3) and upper-right corner (6,5).
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
    except:
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
    For this demo, we use:
        distance = (abs(filtered_rssi) - 30) / 10
    (This formula is arbitrary and used only for testing.)
    """
    distance = (abs(filtered_rssi) - 30) / 10
    return round(distance, 1)

def trilateration(distance_data):
    """
    Compute an estimated location (x, y) using a least-squares trilateration approach.
    
    Given at least three nodes with known locations and distances,
    we linearize the circle equations by subtracting the equation for a reference node.
    For each node i (i=2,...,N), we get:
      2*(xi - x1)*x + 2*(yi - y1)*y = xi^2 - x1^2 + yi^2 - y1^2 + di^2 - d1^2
    We then solve the resulting linear system via least squares.
    """
    if len(distance_data) < 3:
        # Fallback: use weighted average if fewer than 3 nodes
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
    
    # Use the first node as reference.
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
    For a given aggregated data entry:
    1. Apply moving average filtering to each node’s RSSI.
    2. Convert the filtered RSSI to a distance.
    3. Use least-squares trilateration to estimate the device location.
    4. Perform a geofencing check and output final status.
    5. Publish final geofencing result to sensor/alerts topic.
    """
    # Apply moving average filtering per node
    filtered_data = []
    for entry in agg_entry["RSSI_data"]:
        original_rssi = entry["RSSI"]
        filtered_rssi = noise_filter(entry["node_id"], original_rssi)
        filtered_data.append({
            "node_id": entry["node_id"],
            "filtered_RSSI": filtered_rssi,
            "location": entry["location"]
        })
    
    # Convert filtered RSSI to distance for each node
    distance_data = []
    for entry in filtered_data:
        distance = rssi_to_distance(entry["filtered_RSSI"])
        distance_data.append({
            "node_id": entry["node_id"],
            "distance": distance,
            "location": entry["location"]
        })
    
    # Perform trilateration to estimate the location of the device using the proper algorithm.
    estimated_location = trilateration(distance_data)
    
    # Print the aggregated data, distance data, and estimated location
    print("Aggregated Data:")
    print(json.dumps(agg_entry, indent=2))
    print("\nDistance Data:")
    print(json.dumps({
        "device_id": agg_entry["device_id"],
        "timestamp": agg_entry["timestamp"],
        "distance_data": distance_data
    }, indent=2))
    print("\nEstimated Location (Trilateration):", estimated_location)

    # Geofencing Check
    inside_zone = is_within_safe_zone(estimated_location)
    status = "Safe" if inside_zone else "ALERT: Patient Outside Safe Zone!"

    # Final result output (what you see in the image)
    final_result = {
        "device_id": agg_entry["device_id"],
        "timestamp": agg_entry["timestamp"],
        "status": status,
        "location": estimated_location
    }
    print("\nGeofencing & Alerts:")
    print(json.dumps(final_result, indent=2))
    print("-" * 50, "\n")

    # Publish the final_result to a new topic "sensor/alerts"
    # Convert the dictionary to JSON string before publishing
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

    # Validate the received message
    valid, validation_msg = validate_message(payload)
    if not valid:
        print("Invalid message:", validation_msg)
        return

    # Extract fields from the payload
    device_id = payload["device_id"]
    node_id = payload["node_id"]
    rssi = float(payload["RSSI"])
    timestamp = payload["timestamp"]

    # Create an aggregation key (device_id, timestamp)
    key = (device_id, timestamp)
    if key not in aggregated_data:
        aggregated_data[key] = {
            "device_id": device_id,
            "timestamp": timestamp,
            "RSSI_data": []
        }
    
    # Check if this node's data is already present; if so, update it.
    found = False
    for entry in aggregated_data[key]["RSSI_data"]:
        if entry["node_id"] == node_id:
            entry["RSSI"] = rssi
            found = True
            break
    if not found:
        aggregated_data[key]["RSSI_data"].append({
            "node_id": node_id,
            "RSSI": rssi,
            "location": node_locations[node_id]
        })
    
    print(f"Received Data: Device: {device_id}, Node: {node_id}, RSSI: {rssi}, Timestamp: {timestamp}")
    
    # For demonstration, assume an aggregated set is complete when data from all 4 nodes is received.
    if len(aggregated_data[key]["RSSI_data"]) >= 4:
        process_aggregated_data(aggregated_data[key])
        # Remove the processed aggregation to avoid reprocessing
        del aggregated_data[key]

### MQTT Configuration ###
MQTT_BROKER = "localhost"  # Broker is running on the Raspberry Pi itself
MQTT_PORT = 1883
MQTT_TOPIC = "sensor/data"

# Create MQTT client and assign callbacks
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Connect and start the loop
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()
