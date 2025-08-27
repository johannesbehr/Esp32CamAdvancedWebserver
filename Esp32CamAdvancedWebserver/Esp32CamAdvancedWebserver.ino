#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "AsyncWebdav.h"
#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"

#include <time.h>
#include <ESPDateTime.h>
#include "esp_partition.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"

#include "SystemConfigurator.h"  // Configures Wifi and other System stuff
#include "FwUpdate.h"            // Allows OTA-FW Update
#include "scpi.h"                // Exceutes SCPI-Commands
#include "cameraServer.h"        // Makes the camera available

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
const char *timeZone = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // Europa/Berlin

AsyncWebServer server(80);
AsyncWebdav *dav;

// Hilfsfunktion zur Content-Type-Ermittlung (optional nützlich)
String getContentType(const String &filename) {
  if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".jpg")) return "image/jpeg";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".json")) return "application/json";
  return "text/plain";
}

void printTaskList() {
  // Genügend Speicher für die Taskliste bereitstellen
  const size_t bufferSize = 1024;  // ggf. vergrößern bei vielen Tasks
  char *taskListBuffer = (char *)malloc(bufferSize);
  if (taskListBuffer == nullptr) {
    Serial.println("Fehler: Kein Speicher für Taskliste!");
    return;
  }

  // Task-Infos in den Puffer schreiben
  vTaskList(taskListBuffer);

  Serial.println("Task Name        Status Prio Stack #ID");
  Serial.println("=======================================");
  Serial.print(taskListBuffer);

  free(taskListBuffer);
}


void printPartitionTable() {
  Serial.println("Partitionstabelle:");
  const esp_partition_t *part = nullptr;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);

  while (it != nullptr) {
    part = esp_partition_get(it);
    if (part != nullptr) {
      Serial.printf("Name: %-16s | Typ: 0x%02x | Subtyp: 0x%02x | Offset: 0x%08x | Größe: %d KB\n",
                    part->label,
                    part->type,
                    part->subtype,
                    part->address,
                    part->size / 1024);
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);

  it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr) {
    part = esp_partition_get(it);
    if (part != nullptr) {
      Serial.printf("Name: %-16s | Typ: 0x%02x | Subtyp: 0x%02x | Offset: 0x%08x | Größe: %d KB\n",
                    part->label,
                    part->type,
                    part->subtype,
                    part->address,
                    part->size / 1024);
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
}


time_t getFileLastWrite(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) return st.st_mtime;
  return 0;
}

bool initSDCard() {
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("Card Mount Failed");
    return (false);
  }
  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return (false);
  } else {
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    return (true);
  }
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void setupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setenv("TZ", timeZone, 1);
  tzset();
  printLocalTime();
}


void setup() {

  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  printf("Advanced ESP Webserver Version: %s\r\n", VERSION);

  // Turn on red LED to show that we are alive
  pinMode(33, OUTPUT);
  digitalWrite(33, 0);

  // Turnout flash
  pinMode(4, OUTPUT);
  digitalWrite(4, 0);

  initSDCard();

  loadSystemConfig(SD_MMC);
  server.on("/system/", HTTP_GET, systemStatus_handler);

  // printPartitionTable();
  setupTime();
  Serial.println("Zeit synchronisiert: " + DateTime.toString());


  // Create WebDav-Server to SD-Card
  dav = new AsyncWebdav("/dav", SD_MMC);

  // Start Camera config
  camera_cfg(&server);


  server.on("/scpi", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("cmd")) {
      request->send(404, "text/plain", "Requiered parameter: cmd");
      return;
    }
    String res = scpi_handleCommand(request->getParam("cmd")->value());
    request->send(200, "text/plain", res);
  });


  server.on("/update/", HTTP_GET, FwUpdate_handler);

  server.addHandler(dav);
  server.serveStatic("/", SD_MMC, "/www/").setDefaultFile("index.html");

  server.begin();
}

void loop() {
  delay(5000);
}