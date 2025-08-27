#include "cameraServer.h"
#include "camera_pins.h"
#include "esp_camera.h" 
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "FS.h"
#include "SD_MMC.h"


void startCameraServer(AsyncWebServer *server);
void setupLedFlash();

void camera_cfg(AsyncWebServer *server) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  startCameraServer(server);
  Serial.print("Camera Ready! Use 'http://");
}

/*
int camera_cfg2() {
  camera_config_t config;

  // Camera pins configuration for ESP32-S3
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.print("Camera init failed with error 0x");
    Serial.println(err, HEX);
    return 0;
  }
  Serial.println("Camera initialized");
  return 1;
}

void camserver_handleGet(AsyncWebServerRequest *request){
    request->send_P(200, "text/html", R"rawliteral(
    <html>
      <head>
        <title>ESP32-CAM</title>
      </head>
      <body style="margin:0;">
        <img src="/cam/stream" style="width:100vw;height:auto;" />
      </body>
    </html>
  )rawliteral");
}
*/

// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// LED FLASH setup
#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255

int led_duty = 0;
bool isStreaming = false;

#endif

typedef struct {
  size_t size;   //number of values used for filtering
  size_t index;  //current value index
  size_t count;  //value count
  int sum;
  int *values;  //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));

  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) {
    return NULL;
  }
  memset(filter->values, 0, sample_size * sizeof(int));

  filter->size = sample_size;
  return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) {
    return value;
  }
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size) {
    filter->count++;
  }
  return filter->sum / filter->count;
}
#endif

#if defined(LED_GPIO_NUM)
void enable_led(bool en) {  // Turn LED On or Off
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_GPIO_NUM, duty);
  //ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
  //ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL);
  log_i("Set LED intensity to %d", duty);
}
#endif

/*

static esp_err_t bmp_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_start = esp_timer_get_time();
#endif
  fb = esp_camera_fb_get();
  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted) {
    log_e("BMP Conversion failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_end = esp_timer_get_time();
#endif
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len);
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}
*/
void capture_handler(AsyncWebServerRequest *request) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif

#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);  // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  fb = esp_camera_fb_get();              // or it won't be visible in the frame. A better way to do this is needed.
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

  if (!fb) {
    log_e("Camera capture failed");
    request->send_P(500, "text/html", "<html><body>Camera capture failed!</body></html>");
    return;
  }

  if (fb->format == PIXFORMAT_JPEG) {
    AsyncWebServerResponse *response = request->beginResponse_P(
      200,
      "image/jpeg",
      fb->buf,
      fb->len);

    response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
    response->addHeader("Access-Control-Allow-Origin", "*");

    char ts[32];
    snprintf(ts, sizeof(ts), "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
    response->addHeader("X-Timestamp", ts);

    request->send(response);
    esp_camera_fb_return(fb);
  } else {

    // Umwandlung in JPEG
    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;

    if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
      esp_camera_fb_return(fb);
      request->send(500, "text/plain", "JPEG-Konvertierung fehlgeschlagen");
      return;
    }

    esp_camera_fb_return(fb);  // Frame zurückgeben, Puffer wurde dupliziert

    // Stream-Response erzeugen
    AsyncResponseStream *response = request->beginResponseStream("image/jpeg", jpg_len);
    response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
    response->addHeader("Access-Control-Allow-Origin", "*");

    char ts[32];
    gettimeofday(&fb->timestamp, nullptr);  // fallback für Timestamp
    snprintf(ts, sizeof(ts), "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
    response->addHeader("X-Timestamp", ts);

    response->write(jpg_buf, jpg_len);
    free(jpg_buf);  // nicht vergessen!

    request->send(response);
  }
}



void control_handler(AsyncWebServerRequest *request) {
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (!request->hasParam("var") || !request->hasParam("val")) {
    request->send(404, "text/plain", "Parameter fehlt");
    return;
  }

  // Inhalte in char[] kopieren
  strlcpy(variable, request->getParam("var")->value().c_str(), sizeof(variable));
  strlcpy(value, request->getParam("val")->value().c_str(), sizeof(value));

  int val = atoi(value);

  log_i("%s = %d", variable, val);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) {
      res = s->set_framesize(s, (framesize_t)val);
    }
  } else if (!strcmp(variable, "quality")) {
    res = s->set_quality(s, val);
  } else if (!strcmp(variable, "contrast")) {
    res = s->set_contrast(s, val);
  } else if (!strcmp(variable, "brightness")) {
    res = s->set_brightness(s, val);
  } else if (!strcmp(variable, "saturation")) {
    res = s->set_saturation(s, val);
  } else if (!strcmp(variable, "gainceiling")) {
    res = s->set_gainceiling(s, (gainceiling_t)val);
  } else if (!strcmp(variable, "colorbar")) {
    res = s->set_colorbar(s, val);
  } else if (!strcmp(variable, "awb")) {
    res = s->set_whitebal(s, val);
  } else if (!strcmp(variable, "agc")) {
    res = s->set_gain_ctrl(s, val);
  } else if (!strcmp(variable, "aec")) {
    res = s->set_exposure_ctrl(s, val);
  } else if (!strcmp(variable, "hmirror")) {
    res = s->set_hmirror(s, val);
  } else if (!strcmp(variable, "vflip")) {
    res = s->set_vflip(s, val);
  } else if (!strcmp(variable, "awb_gain")) {
    res = s->set_awb_gain(s, val);
  } else if (!strcmp(variable, "agc_gain")) {
    res = s->set_agc_gain(s, val);
  } else if (!strcmp(variable, "aec_value")) {
    res = s->set_aec_value(s, val);
  } else if (!strcmp(variable, "aec2")) {
    res = s->set_aec2(s, val);
  } else if (!strcmp(variable, "dcw")) {
    res = s->set_dcw(s, val);
  } else if (!strcmp(variable, "bpc")) {
    res = s->set_bpc(s, val);
  } else if (!strcmp(variable, "wpc")) {
    res = s->set_wpc(s, val);
  } else if (!strcmp(variable, "raw_gma")) {
    res = s->set_raw_gma(s, val);
  } else if (!strcmp(variable, "lenc")) {
    res = s->set_lenc(s, val);
  } else if (!strcmp(variable, "special_effect")) {
    res = s->set_special_effect(s, val);
  } else if (!strcmp(variable, "wb_mode")) {
    res = s->set_wb_mode(s, val);
  } else if (!strcmp(variable, "ae_level")) {
    res = s->set_ae_level(s, val);
  }
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity")) {
    led_duty = val;
    if (isStreaming) {
      enable_led(true);
    }
  }
#endif
  else {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0) {
    request->send(500, "text/html", "Unknown command.");
  }

  request->send(200, "text/html", "Control ok.");
}


static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask) {
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

void status_handler(AsyncWebServerRequest *request) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';

  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
    for (int reg = 0x3400; reg < 0x3406; reg += 2) {
      p += print_reg(p, s, reg, 0xFFF);  //12 bit
    }
    p += print_reg(p, s, 0x3406, 0xFF);

    p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
    p += print_reg(p, s, 0x3503, 0xFF);
    p += print_reg(p, s, 0x350a, 0x3FF);   //10 bit
    p += print_reg(p, s, 0x350c, 0xFFFF);  //16 bit

    for (int reg = 0x5480; reg <= 0x5490; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5380; reg <= 0x538b; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5580; reg < 0x558a; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }
    p += print_reg(p, s, 0x558a, 0x1FF);  //9 bit
  } else if (s->id.PID == OV2640_PID) {
    p += print_reg(p, s, 0xd3, 0xFF);
    p += print_reg(p, s, 0x111, 0xFF);
    p += print_reg(p, s, 0x132, 0xFF);
  }

  p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
  p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#if defined(LED_GPIO_NUM)
  p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
  p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;

  //String result(json_response, strlen(json_response));
  //request->send_P(200, "application/json", json_response, strlen(json_response));
  request->send_P(200, "application/json", (const uint8_t *)json_response, strlen(json_response));
}

void xclk_handler(AsyncWebServerRequest *request) {
  char *buf = NULL;
  char _xclk[32];

  if (!request->hasParam("xclk")) {
    request->send(404, "text/plain", "Parameter fehlt");
    return;
  }

  strlcpy(_xclk, request->getParam("xclk")->value().c_str(), sizeof(_xclk));

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  if (res) {
    request->send(500, "text/html", "Set xclk failed.");
  } else {
    request->send(200, "text/html", "Set xclk ok.");
  }
}

void reg_handler(AsyncWebServerRequest *request) {
  char _reg[32];
  char _mask[32];
  char _val[32];

  if (!request->hasParam("reg") || !request->hasParam("mask") || !request->hasParam("val")) {
    request->send(404, "text/plain", "Requiered parameter: reg, mask and val!");
    return;
  }

  // Inhalte in char[] kopieren
  strlcpy(_reg, request->getParam("reg")->value().c_str(), sizeof(_reg));
  strlcpy(_mask, request->getParam("mask")->value().c_str(), sizeof(_mask));
  strlcpy(_val, request->getParam("val")->value().c_str(), sizeof(_val));

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);

  if (res) {
    request->send(500, "text/html", "Set reg failed.");
  } else {
    request->send(200, "text/html", "Set reg ok.");
  }
}

void greg_handler(AsyncWebServerRequest *request) {
  char *buf = NULL;
  char _reg[32];
  char _mask[32];

  if (!request->hasParam("reg") || !request->hasParam("mask")) {
    request->send(404, "text/plain", "Requiered parameter: reg and mask!");
    return;
  }

  // Inhalte in char[] kopieren
  strlcpy(_reg, request->getParam("reg")->value().c_str(), sizeof(_reg));
  strlcpy(_mask, request->getParam("mask")->value().c_str(), sizeof(_mask));

  int reg = atoi(_reg);
  int mask = atoi(_mask);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->get_reg(s, reg, mask);

  if (res) {
    request->send(500, "text/html", "Get reg failed.");
  } else {
    log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);
    char buffer[20];
    const char *val = itoa(res, buffer, 10);
    request->send(200, "text/html", (const uint8_t *)buffer, strlen(buffer));
  }
}

/*
static int parse_get_var(char *buf, const char *key, int def) {
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) {
    return def;
  }
  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req) {
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul = parse_get_var(buf, "mul", 0);
  int sys = parse_get_var(buf, "sys", 0);
  int root = parse_get_var(buf, "root", 0);
  int pre = parse_get_var(buf, "pre", 0);
  int seld5 = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}
*/
int parse_get_var(AsyncWebServerRequest *request, const char *key, int def) {
  if (request->hasParam("val")) {
    return (request->getParam("val")->value().toInt());
  } else {
    return (def);
  }
}

void resolution_handler(AsyncWebServerRequest *request) {

  int startX = parse_get_var(request, "sx", 0);
  int startY = parse_get_var(request, "sy", 0);
  int endX = parse_get_var(request, "ex", 0);
  int endY = parse_get_var(request, "ey", 0);
  int offsetX = parse_get_var(request, "offx", 0);
  int offsetY = parse_get_var(request, "offy", 0);
  int totalX = parse_get_var(request, "tx", 0);
  int totalY = parse_get_var(request, "ty", 0);  // codespell:ignore totaly
  int outputX = parse_get_var(request, "ox", 0);
  int outputY = parse_get_var(request, "oy", 0);
  bool scale = parse_get_var(request, "scale", 0) == 1;
  bool binning = parse_get_var(request, "binning", 0) == 1;

  log_i(
    "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
    totalX, totalY, outputX, outputY, scale, binning  // codespell:ignore totaly
  );
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);  // codespell:ignore totaly
  if (res) {
    request->send(500, "text/html", "Set resolution failed.");
  } else {
    request->send(200, "text/html", "Set resolution ok.");
  }
}

void index_handler(AsyncWebServerRequest *request) {
  // httpd_resp_set_type(req, "text/html");
  //httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    if (s->id.PID == OV3660_PID) {
      request->send(SD_MMC, "/cam/index_ov3660.html", "text/html");
    } else if (s->id.PID == OV5640_PID) {
      request->send(SD_MMC, "/cam/index_ov5640.html", "text/html");
    } else {
      request->send(SD_MMC, "/cam/index_ov2640.html", "text/html");
    }
  } else {
    log_e("Camera sensor not found");
    request->send_P(200, "text/html", "<html><body>Camera sensor not found!</body></html>");
  }
}


#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";


void stream_handler(AsyncWebServerRequest *request) {
  Serial.println("[stream_handler] MJPEG-Stream wird gestartet...");

  static camera_fb_t *fb = NULL;
  static size_t bytes_sent = 0;
  static size_t state = 0;
  static char header_buf[128];
  static size_t header_len = 0;
  static bool frame_ready = false;

  #if defined(LED_GPIO_NUM)
    isStreaming = true;
    enable_led(true);
  #endif

  AsyncWebServerResponse *response = request->beginChunkedResponse(_STREAM_CONTENT_TYPE,
    [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      if (!frame_ready) {
        if (fb) {
          esp_camera_fb_return(fb);
        }


        fb = esp_camera_fb_get();
        if (!fb || fb->format != PIXFORMAT_JPEG) {
          Serial.println("[stream_handler] ❌ Kein JPEG-Frame verfügbar.");
          #if defined(LED_GPIO_NUM)
            isStreaming = false;
            enable_led(false);
          #endif
          return 0;
        }

        size_t blen = strlen(_STREAM_BOUNDARY);
        memcpy(buffer, _STREAM_BOUNDARY, blen);
        bytes_sent = 0;

        // Header vorbereiten
        header_len = snprintf(header_buf, sizeof(header_buf), _STREAM_PART,
                              fb->len, fb->timestamp.tv_sec, fb->timestamp.tv_usec);

        state = 0;
        frame_ready = true;

       // Serial.printf("[stream_handler] 📸 Frame vorbereitet: %u Bytes JPEG\n", fb->len);
        return blen;  // Erst nur Boundary senden
      }

      size_t bytes_left = 0;

      if (state == 0) {  // Header senden
        bytes_left = header_len - bytes_sent;
        if (bytes_left > maxLen) bytes_left = maxLen;
        memcpy(buffer, header_buf + bytes_sent, bytes_left);
        bytes_sent += bytes_left;
        if (bytes_sent == header_len) {
          state = 1;
          bytes_sent = 0;
        }
        return bytes_left;
      }

      if (state == 1) {  // JPEG-Daten senden
        bytes_left = fb->len - bytes_sent;
        if (bytes_left > maxLen) bytes_left = maxLen;
        memcpy(buffer, fb->buf + bytes_sent, bytes_left);
        bytes_sent += bytes_left;

        if (bytes_sent == fb->len) {
          state = 2;
          frame_ready = false;
        }
        return bytes_left;
      }


      #if defined(LED_GPIO_NUM)
        isStreaming = false;
        enable_led(false);
      #endif

      return 0;
    });

  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
 // Serial.println("[stream_handler] Antwort wurde gesendet.");
}

void startCameraServer(AsyncWebServer *server) {

  ra_filter_init(&ra_filter, 20);

  server->on("/cam/", HTTP_GET, index_handler);
  server->on("/cam/capture", HTTP_GET, capture_handler);
  server->on("/cam/status", HTTP_GET, status_handler);
  server->on("/cam/control", HTTP_GET, control_handler);
  server->on("/cam/reg", HTTP_GET, reg_handler);
  server->on("/cam/greg", HTTP_GET, greg_handler);
  server->on("/cam/resolution", HTTP_GET, resolution_handler);
  server->on("/cam/stream", HTTP_GET, stream_handler);
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
#endif
}

