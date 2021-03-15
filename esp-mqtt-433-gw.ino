
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <ArduinoJson.h>
#include <RCSwitch.h>

#include "LittleFS.h" 

// MQTT / 433 Mhz Gateway for ESP8266 module.
//
// Simple gateway between MQTT and 433 Mhz devices.  See README.md for further information.
// 
// Inspiration:
//   https://www.mdeditor.tw/pl/2ypT
//   Uses ArduinoJSON for serialization and deserialization, see https://arduinojson.org/
//
#define PIN_TX          12                // PIN for the 433 Mhz transmission module.

const char* cfg_file = "/config.json";    // Configuration file stored in LittleFS
String ca_file;                           // Trusted Root Certificate stored in LittleFS (name in configuration)

uint8_t ca[1500];        // Root CA of server certificate (PEM format).
String fingerprint;      // Fingerprint of the server certificate.

String ap_ssid;          // AP SSID.
String ap_password;      // AP password.

String mqtt_client_id;   // MQTT client id.
String mqtt_host;        // MQTT hostname, for example mqtt.mosquitto.org.
int mqtt_port;           // MQTT port. The port 8883 is by convention the SSL/TLS port.
String mqtt_user;        // MQTT username.
String mqtt_password;    // MQTT password.

String mqtt_pub_topic;   // MQTT topic for publishing data (received 433 Mhz signals).
String mqtt_sub_topic;   // MQTT topic for subscribing to commands (data for sending 433 Mhz signals).

StaticJsonDocument<500> json;    // Static allocated JSON document (max. 200 bytes).
char timestamp[30];              // Static allocated timestamp as string.
char payload[200];               // Payload for MQTT messages.

WiFiUDP udp;                     // UDP connection used by NTP client.
NTPClient ntp_client(udp);       // NTP client.

WiFiClientSecure client;         // TCP SSL/TLS connection.
PubSubClient mqtt(client);       // MQTT connection.
X509List caX509;                 // CA certificate parsed.

void subscribe_callback(char *topic, byte *payload, unsigned int length);

RCSwitch rc433 = RCSwitch();

void setup() 
{
  Serial.begin(9600);
  delay(2500);

  // Startup banner.
  Serial.println("\n\nMQTT/433 Mhz Gateway, v1.0");
  Serial.println("Copyright (C) 2021, conspicio.dk");

  // Read configuration
  read_config();
  
  // Connect to WIFI.
  WiFi.mode(WIFI_STA);
  WiFi.begin(ap_ssid, ap_password);
  
  Serial.print("Connecting to AP "); Serial.print(ap_ssid); Serial.print(" ");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected.");
  Serial.print(" - IP address: "); Serial.println(WiFi.localIP());

  // Establish secure SSL/TLS connection.
  client.setTrustAnchors(&caX509);                // Load CA into trust-store.
  
  // client.allowSelfSignedCerts();               // Enable self-signed cert support.
  client.setFingerprint(fingerprint.c_str());     // Load SHA1 fingerprint.
  
  // client.setInsecure();                        // We don't really want to be insecure.
  verifytls();

  // Connect to MQTT server.
  Serial.print("MQTT server: "); Serial.print(mqtt_host); Serial.print(":"); Serial.println(mqtt_port);
  mqtt.setServer(mqtt_host.c_str(), mqtt_port);
  mqtt.setCallback(subscribe_callback);

  // Add MAC address suffix to create a unique MQTT client id.
  mqtt_client_id += WiFi.macAddress();
  Serial.print("Client Id: "); Serial.println(mqtt_client_id);

  // Start the NTP client and update it.
  ntp_client.begin();
  ntp_client.update();

  // GPIO 0 (Flash Button) ==> DATA PIN on receiver module MUST be connected to GPIO 0 which is D3 on the WeMos D1 Mini.
  rc433.enableReceive(0); 
  
  // The DATA PIN for the transmitter module is defined as PIN_TX.
  rc433.enableTransmit(PIN_TX); 
}

void loop() 
{
  // If we are not connected to MQTT server, try to reconnect.
  if(!mqtt.connected()) reconnect();

  if (rc433.available()) {
    // Data is available from the 433 MHz module.
    unsigned long rc_value = rc433.getReceivedValue();
    unsigned int rc_bits = rc433.getReceivedBitlength();
    unsigned int rc_protocol = rc433.getReceivedProtocol();
    rc433.resetAvailable();

    // Publish data received to MQTT topic.
    publish(rc_value, rc_bits);
  }

  // Process MQTT messages.
  mqtt.loop();
}

void publish(unsigned long value, unsigned int bits) {
  
    // Read sensor values (or simulate it in this case).
    time_t  now = (time_t)ntp_client.getEpochTime();
    tm* timeinfo =  localtime(&now);
    strftime(timestamp, 30, "%FT%T", timeinfo);
    
    // Update the JSON document.
    json.clear();
    json["timestamp"] = timestamp;
    json["value"] = value;
    json["bits"] = bits;
  
    // Serialize JSON document into payload.
    serializeJson(json, payload);
  
    // Print published data.
    Serial.print(timestamp); Serial.print(": "); Serial.print(mqtt_pub_topic); Serial.print("\n  "); 
    Serial.println(payload);
  
    // Publish.
    mqtt.publish(mqtt_pub_topic.c_str(), payload);
}

void subscribe_callback(char *topic, byte *payload, unsigned int length)
{
  // We have received a MQTT message.
  
  // Get current time from NTP client.
  time_t now = (time_t) ntp_client.getEpochTime();
  tm* timeinfo =  localtime(&now);
  
  // Convert time into string (char[]).
  strftime(timestamp, 30, "%FT%T", timeinfo);

  // Print information on data received.
  Serial.print(timestamp); Serial.print(": "); Serial.println(topic);

  // Deserialise the binary payload received. We assume it is JSON formatted.
  DeserializationError err = deserializeJson(json, payload);
  if (err) {
    Serial.println("Deserialization of JSON payload failed.");
    return;
  }

  // Extact "value" and "bits" ...
  unsigned long value = json["value"];
  unsigned int bits = json["bits"];
  Serial.print("  value="); Serial.print(value); Serial.print(", bits="); Serial.println(bits);

  // Send via 433 Mhz.
  rc433.send(value, bits);
}

void subscribe() {
  // Subscribe to the MQTT topics in question.
  mqtt.subscribe(mqtt_sub_topic.c_str());
}

void reconnect() {  
  /* Loop until we're reconnected */
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT broker ... ");

    if (mqtt.connect(mqtt_client_id.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) {
      // We are connected to the MQTT broker. 
      Serial.println("connected.");

      // Call function to establish subscriptions.
      subscribe();
    } 
    else 
    {
      // Connection failed.
      Serial.print("connection failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(". Retry in 5 seconds.");
      delay(5000);
    }
  }
}

void read_config() {
  LittleFS.begin();
  File f;

  // Read configuration from file.
  f = LittleFS.open(cfg_file, "r");
  if (f) {
      deserializeJson(json, f);
      ap_ssid = json["ap"]["ssid"].as<String>();
      ap_password = json["ap"]["password"].as<String>();

      mqtt_client_id = json["mqtt"]["client_id"].as<String>();    
      mqtt_host = json["mqtt"]["host"].as<String>();
      mqtt_port = json["mqtt"]["port"].as<int>();
      mqtt_user = json["mqtt"]["user"].as<String>();
      mqtt_password = json["mqtt"]["password"].as<String>();
      mqtt_pub_topic = json["mqtt"]["publish"].as<String>();
      mqtt_sub_topic = json["mqtt"]["subscribe"].as<String>();

      ca_file = json["server"]["trustedca"].as<String>();
      fingerprint = json["server"]["fingerprint"].as<String>();
      f.close();
  }
  else {
    Serial.println("FATAL! No configuration.");
  }
    

  // Read CA certificate from file.
  f = LittleFS.open(ca_file.c_str(), "r"); 
  if (f) {
    f.read(ca, sizeof(ca)); 
    caX509 = X509List((char*)ca);
    f.close();
  }
  else
    Serial.println("WARNING! No root CA certificate.");    
}

bool verifytls() 
{
  bool success = false;
    
  Serial.print("Verifying TLS connection to: ");
  Serial.print(mqtt_host); Serial.print(":"); Serial.println(mqtt_port);

  success = client.connect(mqtt_host, mqtt_port);
  if (success) 
    Serial.println("Connection complete, valid certificate, and valid fingerprint.");
  else 
    Serial.println("Connection failed!");
  return success;
}
