#include <ArduinoJson.h>
#include "SystemConfigurator.h"
#include <WiFi.h>
#include <DNSServer.h>

DNSServer *dnsServer;

bool loadWiFiConfig(JsonArray wifiArray, WiFiConfig& selectedConfig);
void startAccessPointFallback();

String hostname;

bool loadSystemConfig(fs::FS& fs) {
  File file = fs.open(CONFIG_FILE);
  if (!file) {
    Serial.println("Failed to open config file");
    return false;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("Failed to parse config file: ");
    Serial.println(error.c_str());
    return false;
  }

  if(doc.containsKey("device")){
    hostname = doc["device"]["name"] |"";
    WiFi.setHostname(hostname.c_str());
  }

  if (!doc.containsKey("wifi") || !doc["wifi"].is<JsonArray>()) {
    Serial.println("Missing or invalid 'wifi' array in config");
    return false;
  }

  JsonArray wifiArray = doc["wifi"];
  WiFiConfig config;

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);

  if (!loadWiFiConfig(wifiArray, config)) {
    Serial.println("No known networks found. Starting fallback AP...");
    startAccessPointFallback();
    return true;
  }

  if (!config.dhcp) {
    if (!WiFi.config(config.ip, config.gateway, config.subnet, config.dns)) {
      Serial.println("Failed to set static IP config");
    }
  }

  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  Serial.printf("Connecting to %s ...", config.ssid.c_str());

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected, IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect");
    return false;
  }
}

bool loadWiFiConfig(JsonArray wifiArray, WiFiConfig& selectedConfig) {
  int n = WiFi.scanNetworks();

  for (JsonObject entry : wifiArray) {
    String ssid = entry["ssid"] | "";
    String password = entry["password"] | "";
    bool dhcp = entry["dhcp"] | true;

    // Prüfen, ob dieses Netz in der Umgebung gefunden wurde
    for (int i = 0; i < n; i++) {
      if (ssid == WiFi.SSID(i)) {
        selectedConfig.ssid = ssid;
        selectedConfig.password = password;
        selectedConfig.dhcp = dhcp;

        if (!dhcp && entry.containsKey("static")) {
          JsonObject staticCfg = entry["static"];
          selectedConfig.ip.fromString(staticCfg["ip"] | "0.0.0.0");
          selectedConfig.gateway.fromString(staticCfg["gateway"] | "0.0.0.0");
          selectedConfig.subnet.fromString(staticCfg["subnet"] | "255.255.255.0");
          selectedConfig.dns.fromString(staticCfg["dns"] | "8.8.8.8");
        }
        Serial.printf("Selected known network: %s\n", ssid.c_str());
        return true;
      }
    }
  }

  return false;  // kein bekanntes Netz gefunden
}

void startAccessPointFallback() {
  const char* fallbackSSID = "ESP32_cam";
  const char* fallbackPassword = "";  // Optional: leer lassen für offenen AP

  IPAddress apIP("192.168.4.1");
  IPAddress apGateway("192.168.4.1");
  IPAddress apSubnet("255.255.255.0");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(fallbackSSID, fallbackPassword);

  Serial.println("Started Access Point (fallback):");
  Serial.print("SSID: ");
  Serial.println(fallbackSSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

   // Starte DNS, antworte auf alle Anfragen mit 192.168.4.1
  dnsServer = new DNSServer();
  dnsServer->start(53, "*", apIP);
}

void systemStatus_handler(AsyncWebServerRequest *request) {
  static char json_response[1024];
  char *p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"version\":%s", VERSION);
  *p++ = '}';
  request->send_P(200, "application/json", (const uint8_t *)json_response, strlen(json_response));
}

