#include "FwUpdate.h"
#include "Update.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FS.h"
#include "SD_MMC.h"
#include "esp_camera.h"

bool taskRunning = false;
int updateProgress; 

void suspendWatchdogForCriticalTasks() {
  // IDLE-Tasks (Core 0 und Core 1)
  Serial.printf("Remove wdt for Idle task 0\r\n");
  //esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
  Serial.printf("Don't Remove wdt for Idle task 1\r\n");
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

  // Haupt-Loop
  extern TaskHandle_t loopTaskHandle;
  Serial.printf("Remove wdt for loop\r\n");
  esp_task_wdt_delete(loopTaskHandle);  // Optional, je nach Setup

  // async_tcp (oft durch WebServer erzeugt)
  TaskHandle_t asyncTcpTaskHandle = xTaskGetHandle("async_tcp");
  if (asyncTcpTaskHandle != NULL) {
    Serial.printf("Remove wdt for async_tcp\r\n");
    esp_task_wdt_delete(asyncTcpTaskHandle);
  }
  // wifi
  TaskHandle_t wifiTaskHandle = xTaskGetHandle("wifi");
  if (wifiTaskHandle != NULL) {
    Serial.printf("Remove wdt for wifi\r\n");
    esp_task_wdt_delete(wifiTaskHandle);
  }
  // Timer-Task (seltener nötig, aber möglich)
  TaskHandle_t timerTaskHandle = xTaskGetHandle("Tmr Svc");
  if (timerTaskHandle != NULL) {
    Serial.printf("Remove wdt for Tmr Svc\r\n");
    esp_task_wdt_delete(timerTaskHandle);
  }
}


void updateTaskFunction(void *parameter) {

  //printTaskList();

  //printWatchedTasks();

  File updateFile = SD_MMC.open("/update.bin");
  if (!updateFile) {
    Serial.println("Update-Datei nicht gefunden.");
    return;
  } else {
    Serial.println("Update-Datei gefunden, update wird vorbereitet.");
  }

  delay(500);

  size_t updateSize = updateFile.size();
  int divi = (updateSize / 100) / 1024;

  if (!Update.begin(updateSize)) {
    Serial.println("Update.begin() fehlgeschlagen.");
    vTaskDelete(NULL);  // Beendet den aktuellen Task
    return;
  } else {
    Serial.println("Update gestartet...");
  }

  delay(500);

  updateProgress = 0;

  const size_t bufferSize = 1024;
  uint8_t buffer[bufferSize];

  esp_camera_deinit();

  suspendWatchdogForCriticalTasks();

  pinMode(33, OUTPUT);

  int cnt;

  Serial.println("Beginning transfer of updatefile...");
  delay(100);

  while (updateFile.available()) {
    size_t len = updateFile.read(buffer, bufferSize);
    if (Update.write(buffer, len) != len) {
      Serial.println("Fehler beim Schreiben des Updates.");
      Update.printError(Serial);
      delay(500);
      vTaskDelete(NULL);  // Beendet den aktuellen Task
      return;
    }
    delay(1);
    cnt++;
    if (cnt % 20 == 0) {
      digitalWrite(33, 0);
    } else if (cnt % 20 == 10) {
      digitalWrite(33, 1);
    }

    updateProgress = cnt / divi;
  }
  updateFile.close();
  SD_MMC.remove("/update.bin");
  
  Serial.println("Update übertragen.");

  delay(500);

  if (Update.end() && Update.isFinished()) {
    Serial.println("Update abgeschlossen. Neustart...");
    delay(500);
    ESP.restart();
  } else {
    Serial.println("Update fehlgeschlagen.");
    Update.printError(Serial);
    delay(500);
  }

  taskRunning = false;
  Serial.println("Task zu ende.");
  vTaskDelete(NULL);  // Beendet den aktuellen Task
}

/*
void FwUpdate_handler(AsyncWebServerRequest *request) {

  // Todo:
    // If an Update is running, return JSON status "Update running" and the progress.
    // If no Update is running and no update file is available, return JSON status "No update available"
    // If no Update is running and the file "/update.bin" is available,
      // Check if get-parameter start is set, in this case start the update-Task. 
      // Otherwise return JSON status "Update available"
    
    if (!taskRunning) {
      taskRunning = true;
      xTaskCreatePinnedToCore(
        updateTaskFunction,
        "updateTask",  // Name des Tasks
        4096,          // Stack-Größe in Bytes
        NULL,          // Parameter für die Funktion (optional)
        1,             // Priorität (0 = niedrig, höher = wichtiger)
        NULL,          // Task-Handle (optional)
        0);
      request->send(200, "text/plain", "Update wird gestartet (falls vorhanden).");
    } else {
      request->send(200, "text/plain", "Task läuft bereits!.");
    }
  }*/

void FwUpdate_handler(AsyncWebServerRequest *request) {
    static char json_response[128]; // klein halten, spart RAM
    char *p = json_response;

    // Fall 1: Update läuft gerade
    if (taskRunning) {
        p += sprintf(p, "{\"status\":\"Update running\",\"progress\":%d}", updateProgress);
        request->send_P(200, "application/json", (const uint8_t *)json_response, strlen(json_response));
        return;
    }

    // Prüfen, ob update.bin existiert
    bool fileExists = SD_MMC.exists("/update.bin");

    if (!fileExists) {
        // Fall 2: Keine Datei vorhanden
        p += sprintf(p, "{\"status\":\"No update available\"}");
        request->send_P(200, "application/json", (const uint8_t *)json_response, strlen(json_response));
        return;
    }

    // Datei vorhanden → prüfen, ob "start" Parameter übergeben wurde
    if (request->hasParam("start")) {
        // Fall 3a: Update starten
        taskRunning = true;
        xTaskCreatePinnedToCore(
            updateTaskFunction,
            "updateTask",
            4096,
            NULL,
            1,
            NULL,
            0
        );
        p += sprintf(p, "{\"status\":\"Update started\"}");
    } else {
        // Fall 3b: Datei vorhanden, aber nicht gestartet
        p += sprintf(p, "{\"status\":\"Update available\"}");
    }

    request->send_P(200, "application/json", (const uint8_t *)json_response, strlen(json_response));
}