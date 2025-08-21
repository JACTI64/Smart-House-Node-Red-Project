import serial
import time
import socket
import os
import sys
import json  # Import json module

esp32_devices = ["/dev/rfcomm0", "/dev/rfcomm1"]

# Get the device's actual IPv4 address on the network
def get_network_ipv4():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip_address = s.getsockname()[0]
        s.close()
        return ip_address
    except Exception as e:
        print("Error getting network IP address:", e)
        return "Unknown"

# Function to reset the script after execution
def restart_script():
    print("\nRestarting script in 5 seconds...\n")
    time.sleep(5)
    os.execl(sys.executable, sys.executable, *sys.argv)  # Restart the script

# Check if the combined SSID and password argument is provided
if len(sys.argv) < 2:
    print("Error: SSID and Password (comma-separated) are required as an argument")
    sys.exit(1)

# Get the input as a string (expecting a JSON array format)
ssid_password = sys.argv[1]  # This should be like '["WSU Guest", ""]'

# Parse the input using json.loads()
try:
    ssid_password_list = json.loads(ssid_password)  # Convert string to list
    
    if len(ssid_password_list) != 2:
        raise ValueError("Input must be an array with exactly two elements: SSID and Password")

    ssid = ssid_password_list[0].strip()
    password = ssid_password_list[1].strip()

except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)

# Debug: Print received values
print(f"SSID: '{ssid}'")
print(f"Password: '{password}'")

ip_address = get_network_ipv4()

# Add double quotes around each field to match ESP32 parsing expectations
wifi_data = f"\"{ssid}\",\"{password}\",\"{ip_address}\"\n"

for esp32_mac in esp32_devices:
    try:
        print(f"Connecting to {esp32_mac}...")
        ser = serial.Serial(esp32_mac, baudrate=115200, timeout=3)
        time.sleep(2)
        
        ser.write(wifi_data.encode())  # Send Wi-Fi credentials and IP
        print(f"Sent: {wifi_data.strip()}")
        
        response = ser.readline().decode().strip()
        print(f"ESP32 Response: {response}")
        
        ser.close()
    except Exception as e:
        print(f"Error with {esp32_mac}: {e}")

print("Wi-Fi credentials and IP sent to all ESP32 devices.")
