#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <Adafruit_AHTX0.h>
#include <LittleFS.h> // Use LittleFS for file system
#include <WiFiManager.h>

// AHT20 Sensor
Adafruit_AHTX0 aht;

// Button setup
#define MODE_BUTTON_PIN 16 // GPIO16 for the mode button

// MQTT settings (to be configured via WiFiManager)
char mqttServer[40] = "default.mqtt.server";
char mqttUser[40] = "defaultuser";
char mqttPassword[40] = "defaultpass";
char mqttTopic[64] = "sensor/aht20"; // Default topic
char deviceId[40] = "ESP8266Client"; // Default device ID

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastPublishTime = 0;
unsigned long publishInterval = 5000; // Publish every 5 seconds

// WiFiManager custom parameters
WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttServer, 40);
WiFiManagerParameter custom_mqtt_user("user", "MQTT Username", mqttUser, 40);
WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqttPassword, 40);
WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqttTopic, 64);
WiFiManagerParameter custom_device_id("deviceid", "Device ID", deviceId, 40);

// Method declarations
void initializeSensor();
void connectToMQTT();
void publishSensorData();
bool saveConfigToFlash();
bool loadConfigFromFlash();
void printConfigToSerial();
void configModeCallback(WiFiManager *myWiFiManager);
void startWiFiManagerConfig(); // Start WiFiManager config portal
void checkModeButton();        // Check if button is pressed during boot

void setup()
{
  Serial.begin(115200);

  // Initialize button pin for mode change (GPIO16)
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);

  // Initialize LittleFS
  if (!LittleFS.begin())
  {
    Serial.println("Failed to mount LittleFS. Formatting...");
    LittleFS.format();
    LittleFS.begin(); // Retry after formatting
  }

  // Load config from LittleFS
  if (!loadConfigFromFlash())
  {
    Serial.println("Using default configuration...");
  }

  // Initialize AHT20 sensor
  initializeSensor();

  // Check if the mode button is pressed during boot
  checkModeButton(); // Call the method to check button status

  // Start WiFiManager for automatic connection or configuration
  startWiFiManagerConfig();

  // Connect to MQTT after WiFi is connected
  connectToMQTT();
}

void loop()
{
  // Reconnect to MQTT if not connected
  if (!client.connected())
  {
    connectToMQTT();
  }

  // Publish sensor data at intervals
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime > publishInterval)
  {
    publishSensorData();
    lastPublishTime = currentMillis;
  }

  client.loop();
}

// Method to start WiFiManager configuration portal
void startWiFiManagerConfig()
{
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback); // Set callback for when AP mode is entered

  // Add custom parameters for MQTT configuration
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_device_id);

  // Automatically connect or start the AP for configuration
  if (!wifiManager.autoConnect("Sensor AP"))
  {
    Serial.println("Failed to connect to WiFi. Restarting...");
    ESP.restart(); // Restart if WiFi connection fails
  }

  // Save custom parameters after WiFi connection
  strcpy(mqttServer, custom_mqtt_server.getValue());
  strcpy(mqttUser, custom_mqtt_user.getValue());
  strcpy(mqttPassword, custom_mqtt_password.getValue());
  strcpy(mqttTopic, custom_mqtt_topic.getValue());
  strcpy(deviceId, custom_device_id.getValue());

  if (saveConfigToFlash())
  {
    Serial.println("Config saved successfully.");
  }
  else
  {
    Serial.println("Failed to save config.");
  }

  // Print saved configuration to the serial console
  printConfigToSerial();
}

// Method to check if the mode button is pressed during boot
void checkModeButton()
{
  if (digitalRead(MODE_BUTTON_PIN) == LOW)
  { // Button pressed (LOW signal)
    Serial.println("Mode button pressed during boot. Starting AP mode...");
    delay(500); // Debounce delay

    // Force AP mode using WiFiManager
    startWiFiManagerConfig(); // Reuse the same function to start WiFiManager setup

    // Reset after configuration
    ESP.restart();
  }
}

// Method to initialize the AHT20 sensor
void initializeSensor()
{
  if (!aht.begin())
  {
    Serial.println("Failed to find AHT20 sensor!");
    while (1)
      delay(10);
  }
  Serial.println("AHT20 sensor found.");
}

// Method to connect to the MQTT server
void connectToMQTT()
{
  client.setServer(mqttServer, 1883);
  while (!client.connected())
  {
    Serial.println("Connecting to MQTT...");
    if (client.connect(deviceId, mqttUser, mqttPassword))
    {
      Serial.println("Connected to MQTT.");
    }
    else
    {
      Serial.print("Failed MQTT connection, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// Method to publish sensor data to the MQTT topic
void publishSensorData()
{
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  char payload[100];
  snprintf(payload, sizeof(payload), "{\"device_id\": \"%s\", \"temperature\": %.2f, \"humidity\": %.2f}", deviceId, temp.temperature, humidity.relative_humidity);

  Serial.print("Publishing to MQTT: ");
  Serial.println(payload);
  client.publish(mqttTopic, payload);
}

// Method to save WiFi, MQTT, and device settings to flash (LittleFS)
bool saveConfigToFlash()
{
  File configFile = LittleFS.open("/config.txt", "w");
  if (!configFile)
  {
    Serial.println("Failed to open config file for writing.");
    return false;
  }

  // Write each parameter as a separate line
  configFile.println(mqttServer);
  configFile.println(mqttUser);
  configFile.println(mqttPassword);
  configFile.println(mqttTopic);
  configFile.println(deviceId);

  configFile.close();
  Serial.println("Config saved to LittleFS.");
  return true;
}

// Method to load WiFi, MQTT, and device settings from flash (LittleFS)
bool loadConfigFromFlash()
{
  if (!LittleFS.exists("/config.txt"))
  {
    Serial.println("Config file does not exist. Using default settings.");
    return false;
  }

  File configFile = LittleFS.open("/config.txt", "r");
  if (!configFile)
  {
    Serial.println("Failed to open config file for reading.");
    return false;
  }

  // Read each line and store it in the corresponding variable
  if (configFile.available())
    configFile.readBytesUntil('\n', mqttServer, sizeof(mqttServer));
  if (configFile.available())
    configFile.readBytesUntil('\n', mqttUser, sizeof(mqttUser));
  if (configFile.available())
    configFile.readBytesUntil('\n', mqttPassword, sizeof(mqttPassword));
  if (configFile.available())
    configFile.readBytesUntil('\n', mqttTopic, sizeof(mqttTopic));
  if (configFile.available())
    configFile.readBytesUntil('\n', deviceId, sizeof(deviceId));

  configFile.close();
  Serial.println("Config loaded from LittleFS.");
  printConfigToSerial();
  return true;
}

// Method to print the configuration (WiFi, MQTT, and device settings) to the serial console
void printConfigToSerial()
{
  Serial.println("=== Current Configuration ===");
  Serial.print("MQTT Server: ");
  Serial.println(mqttServer);
  Serial.print("MQTT User: ");
  Serial.println(mqttUser);
  Serial.print("MQTT Password: ");
  Serial.println(mqttPassword); // Caution: printing passwords is a potential security risk
  Serial.print("MQTT Topic: ");
  Serial.println(mqttTopic);
  Serial.print("Device ID: ");
  Serial.println(deviceId);
  Serial.println("=============================");
}

// Callback when entering configuration mode (AP mode)
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Connect to AP: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
