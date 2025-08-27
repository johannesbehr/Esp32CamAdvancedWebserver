#include <Wire.h>
#include <SPI.h>
#include "scpi.h"
#include <ESP32Servo.h>
#include <ESP32PWM.h>

#include "freertos/semphr.h"

#define BUFFER_SIZE 128
static char inputBuffer[BUFFER_SIZE];
static uint8_t inputPos = 0;
static uint8_t currentI2CAddress = 0x00;

bool scpi_i2c_init_done = false;
bool scpi_spi_init_done = false;

// Pin assignment for 6 servos
const int servoPins[6] = { 0, 1, 3, 12, 13 };
const int servo_cnt = 6;
bool servo_init = false;
SemaphoreHandle_t servoMutex = xSemaphoreCreateMutex();

// Servo array
Servo* servos[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

static String scpi_handleCommand2(char* cmdLine);
static String handleCommand_I2C(const char* subcmd);
static String handleCommand_SPI(const char* subcmd);
static String handleCommand_GPIO(const char* subcmd);
static String handleCommand_SERVO(const char* subcmd);

String getHelp() {
  String result;
  result += "SCPI-Linc Hilfe:\r\n";
  result += "*IDN?                      - Geräteidentifikation\r\n";
  result += "*RST                       - Gerät zurücksetzen\r\n";
  result += "I2C:ADDR <hex>             - I2C-Adresse setzen\r\n";
  result += "I2C:WRITE <bytes>          - I2C-Daten schreiben (hex)\r\n";
  result += "I2C:READ? <count>          - I2C-Daten lesen\r\n";
  result += "I2C:SCAN?                  - I2C-Bus nach Geräten durchsuchen\r\n";
  result += "SPI:WRITE <bytes>          - SPI-Daten schreiben/lesen (hex)\r\n";
  result += "SPI:READ? <count>          - SPI-Daten lesen\r\n";
  return result;
}

String scpi_handleCommand(String cmdLine) {
  return (scpi_handleCommand2((char*)cmdLine.c_str()));
}

static String scpi_handleCommand2(char* cmdLine) {
  String result;

  Serial.print("Handle CMD:");
  Serial.println(cmdLine);
  delay(100);

  // Trim leading spaces
  while (*cmdLine == ' ') cmdLine++;

  // Check simple commands
  if (strcasecmp(cmdLine, "*IDN?") == 0) {
    result += "Version 1.0";
  }
  if (strcasecmp(cmdLine, "*RST") == 0) {
    result += "System reset...";
    ESP.restart();
  }
  if (strcasecmp(cmdLine, "HELP") == 0 || strcmp(cmdLine, "?") == 0) {
    return (getHelp());
  }

  // Split by semicolons
  char* part = strtok(cmdLine, ";");
  bool recognized = false;

  while (part != NULL) {
    // Split by colon
    char* subcmd = strchr(part, ':');
    if (subcmd) {
      *subcmd = '\0';  // split root and subcmd
      subcmd++;        // advance to subcommand
    }

    // Convert root to uppercase
    for (char* p = part; *p; ++p) *p = toupper(*p);

    /*
    if (strcmp(part, "I2C") == 0) {
      recognized = true;
      result += handleCommand_I2C(subcmd ? subcmd : "");
    } else if (strcmp(part, "SPI") == 0) {
      recognized = true;
      result += handleCommand_SPI(subcmd ? subcmd : "");
    } else */
    if (strcmp(part, "GPIO") == 0) {
      recognized = true;
      result += handleCommand_GPIO(subcmd ? subcmd : "");
    } else if (strcmp(part, "SERVO") == 0) {
      recognized = true;
      result += handleCommand_SERVO(subcmd ? subcmd : "");
    } else {
      // Unknown root
    }

    part = strtok(NULL, ";");
  }

  if (!recognized) {
    result += F("Unbekannter Befehl. Geben Sie 'HELP' ein.");
  }

  return (result);
}
/*
static String handleCommand_I2C(const char* subcmd) {
  if (!scpi_i2c_init_done) {
    Wire.begin();
    scpi_i2c_init_done = true;
    Serial.println(F("I2C communication initialised."));
  }

  if (strncasecmp(subcmd, "ADDR ", 5) == 0) {
    const char* value = subcmd + 5;
    currentI2CAddress = strtol(value, NULL, 16);
    Serial.print(F("I2C address set to 0x"));
    Serial.println(currentI2CAddress, HEX);

  } else if (strncasecmp(subcmd, "SCAN?", 5) == 0) {
    Serial.println(F("Scanning I2C bus..."));
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      uint8_t error = Wire.endTransmission();
      if (error == 0) {
        Serial.print(F("Found device at 0x"));
        if (addr < 16) Serial.print('0');
        Serial.println(addr, HEX);
      }
    }
    Serial.println(F("Scan complete."));
  } else {
    Serial.println(F("Unbekannter I2C-Befehl."));
  }
}
*/
static String handleCommand_GPIO(const char* subcmd) {
  String result;

  // DIGITALWRITE
  if (strncasecmp(subcmd, "DIGITALWRITE", 12) == 0) {
    const char* params = subcmd + 12;
    while (*params == ' ') params++;

    // Hole ersten Parameter (pin)
    char pinStr[8];
    int i = 0;
    while (*params && *params != ' ' && i < (int)(sizeof(pinStr) - 1)) {
      pinStr[i++] = *params++;
    }
    pinStr[i] = '\0';

    // Überspringe Leerzeichen
    while (*params == ' ') params++;

    // Hole zweiten Parameter (value)
    char valStr[8];
    i = 0;
    while (*params && *params != ' ' && i < (int)(sizeof(valStr) - 1)) {
      valStr[i++] = *params++;
    }
    valStr[i] = '\0';

    if (pinStr[0] && valStr[0]) {
      int pin = atoi(pinStr);
      int value = atoi(valStr);
      pinMode(pin, OUTPUT);
      digitalWrite(pin, value ? HIGH : LOW);
      result += "GPIO " + String(pin) + " set to " + String(value) + "\r\n";
    } else {
      result += "Fehler: GPIO:DIGITALWRITE <pin> <0|1>\r\n";
    }
  }
  // DIGITALREAD?
  else if (strncasecmp(subcmd, "DIGITALREAD?", 12) == 0) {
    const char* params = subcmd + 12;
    while (*params == ' ') params++;

    if (*params) {
      int pin = atoi(params);
      pinMode(pin, INPUT);
      int val = digitalRead(pin);
      result += "GPIO " + String(pin) + " = " + String(val) + "\r\n";
    } else {
      result += "Fehler: GPIO:DIGITALREAD? <pin>";
    }
  }
  // ANALOGREAD?
  else if (strncasecmp(subcmd, "ANALOGREAD?", 11) == 0) {
    const char* params = subcmd + 11;
    while (*params == ' ') params++;

    if (*params) {
      int pin = atoi(params);
      int val = analogRead(A0 + pin);
      result += "Analog " + String(pin) + " = " + String(val) + "\r\n";
    } else {
      result += "Fehler: ANALOG:READ? <pin>";
    }
  }
  // Unbekannter Befehl
  else {
    result += "Unbekannter GPIO-Befehl: " + String(subcmd);
  }

  return result;
}
/*
static String handleCommand_SPI(const char* subcmd) {
  if (!scpi_spi_init_done) {
    SPI.begin();
    scpi_spi_init_done = true;
    Serial.println("SPI communication initialised.");
  }

  // SPI:WRITE
  if (strncasecmp(subcmd, "WRITE", 5) == 0) {
    const char* data = subcmd + 5;
    // Überspringe führende Leerzeichen
    while (*data == ' ') data++;

    while (*data) {
      // Ein Hex-Byte einlesen
      char byteStr[8];
      int i = 0;
      while (*data && *data != ' ' && i < (int)(sizeof(byteStr)-1)) {
        byteStr[i++] = *data++;
      }
      byteStr[i] = '\0';

      if (byteStr[0]) {
        uint8_t out = (uint8_t)strtol(byteStr, NULL, 16);
        uint8_t in = SPI.transfer(out);
        Serial.print("-> 0x");
        if (in < 16) Serial.print("0");
        Serial.println(in, HEX);
      }

      // Überspringe Leerzeichen vor nächstem Byte
      while (*data == ' ') data++;
    }
  }
  // SPI:READ?
  else if (strncasecmp(subcmd, "READ?", 5) == 0) {
    const char* params = subcmd + 5;
    while (*params == ' ') params++;

    int count = atoi(params);
    for (int i = 0; i < count; i++) {
      uint8_t in = SPI.transfer(0x00);
      Serial.print("0x");
      if (in < 16) Serial.print("0");
      Serial.print(in, HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
  // Unbekannt
  else {
    Serial.println("Unbekannter SPI-Befehl.");
    printHelp();
    return false;
  }

  return true;
}

*/

// Servo functions
int servo_attach(int pin) {

  if(!servo_init){
    	ESP32PWM::allocateTimer(3);
      servo_init = true;
  }

  int servoIndex = -1;

  //find index
  for (int i = 0; i < 6; i++) {
    if (servoPins[i] == pin) {
      servoIndex = i;
    }
  }

  if (servoIndex != -1) {
    if (servos[servoIndex] == nullptr) {
      servos[servoIndex] = new Servo();
      servos[servoIndex]->attach(servoPins[servoIndex], 500,2400);
      Serial.print("Servo attached on pin: ");
      Serial.println(pin);
    }
  } else {
    Serial.println("Error: Pin does not support servo: ");
    Serial.println(pin);
  }

  return servoIndex;
}

//Servo *myServo = new Servo();
//Servo myServo;

void servo_write(int pin, int angle) {
  //  if (xSemaphoreTake(servoMutex, pdMS_TO_TICKS(100))) {
  // myServo.attach(pin, 1000, 2000); // attaches the servo on pin 18 to the servo object
  // myServo.write(angle);
  // delay(200);
  // myServo.detach();
  //   xSemaphoreGive(servoMutex);
  // } else {
  //   Serial.println("Could not acquire servo mutex!");
  // }
  int servoIndex = servo_attach(pin);
  if (servoIndex != -1) {
    Serial.print("Servo write: ");
    Serial.println(angle);
    delay(100);
    servos[servoIndex]->write(angle);
  }
}
static String handleCommand_SERVO(const char* subcmd) {
  String result;

  // SERVO:WRITE <pin> <angle>
  if (strncasecmp(subcmd, "WRITE", 5) == 0) {
    const char* params = subcmd + 5;
    while (*params == ' ') params++;

    // Erstes Argument (pin)
    const char* p1 = params;
    while (*params && *params != ' ') params++;
    int len1 = params - p1;

    // Überspringe Leerzeichen vor zweitem Argument
    while (*params == ' ') params++;

    if (len1 > 0 && *params) {
      char buf[8];

      // pin extrahieren
      int copyLen = (len1 < (int)(sizeof(buf) - 1)) ? len1 : (int)(sizeof(buf) - 1);
      strncpy(buf, p1, copyLen);
      buf[copyLen] = '\0';
      int pin = atoi(buf);

      int value = atoi(params);

      if (xSemaphoreTake(servoMutex, pdMS_TO_TICKS(100))) {
        servo_write(pin, value);
        xSemaphoreGive(servoMutex);
        result += "Servo on ";
        result += String(pin);
        result += " set to ";
        result += String(value) + "\r\n";
      } else {
        Serial.println("Could not acquire servo mutex!");
        result += "Could not acquire servo mutex!\r\n";
      }

    } else {
      result += "Fehler: SERVO:WRITE <pin> <angle>\r\n";
    }
  }

  // SERVO:ATTACH <pin>
  else if (strncasecmp(subcmd, "ATTACH", 6) == 0) {
    const char* params = subcmd + 6;
    while (*params == ' ') params++;

    if (*params) {
      int pin = atoi(params);
      if (xSemaphoreTake(servoMutex, pdMS_TO_TICKS(100))) {
        int res = servo_attach(pin);
        xSemaphoreGive(servoMutex);
        if (res != -1) {
          result += "Servo on pin " + String(pin) + " attached!\r\n";
        }
      } else {
        Serial.println("Could not acquire servo mutex!");
        result += "Could not acquire servo mutex!\r\n";
      }
    } else {
      result += "Fehler: SERVO:ATTACH <pin>\r\n";
    }
  }

  // Unbekannter Befehl
  else {
    result += "Unbekannter SERVO-Befehl: ";
    result += String(subcmd) + "\r\n";
  }

  return result;
}

// Otto Stuff

#define LeftLeg 2
#define RightLeg 3
#define LeftFoot 4
#define RightFoot 5
