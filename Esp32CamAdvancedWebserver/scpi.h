#ifndef SCPI_PARSER_H
#define SCPI_PARSER_H

#define VERSION "Arduino,SCPI-Linc/SPI,1.0,2025"
#define GIT_URL "https://github.com/johannesbehr/SCPI-Linc.git"


#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

String scpi_handleCommand(String cmdLine);

#endif