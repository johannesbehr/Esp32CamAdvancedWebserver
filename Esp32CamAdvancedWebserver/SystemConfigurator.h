#include <FS.h>
#include <ESPAsyncWebServer.h>

#define CONFIG_FILE "/config.json"
#define VERSION "3.2"

struct WiFiConfig {
  String ssid;
  String password;
  bool dhcp;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

bool loadSystemConfig(fs::FS &fs);
void systemStatus_handler(AsyncWebServerRequest *request);

