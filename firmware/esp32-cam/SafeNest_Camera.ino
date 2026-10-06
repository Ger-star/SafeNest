/*
 * ============================================================================
 *  Проект: SafeNest v1.0
 *  Модуль: Подсистема видеофиксации и тревожной отправки кадров (ESP32-CAM)
 *  Назначение: Обработка событий тревоги, управление питанием и передача JPEG
 * ============================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include <FirebaseESP32.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Конфигурационные константы сети и сервисов
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
#define DATABASE_URL "YOUR_FIREBASE_DATABASE_URL"
#define DATABASE_SECRET "YOUR_FIREBASE_DATABASE_SECRET"

// Аппаратная конфигурация пинов AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

String targetUserChatId = "DEFAULT_CHAT_ID"; 
unsigned long lastCamPoll = 0;

FirebaseData fbdoCam;
FirebaseConfig fbConfigCam;
FirebaseAuth fbAuthCam;

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 12;
  config.fb_count = 1;
  esp_err_t err = esp_camera_init(&config);
  return (err == ESP_OK);
}

void captureAndSend() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  WiFiClientSecure client;
  client.setInsecure(); 
  client.setTimeout(5000);
  
  if (client.connect("api.telegram.org", 443)) {
    String head = "--Boundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + targetUserChatId + "\r\n--Boundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Boundary--\r\n";
    uint32_t totalLen = head.length() + fb->len + tail.length();

    client.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: multipart/form-data; boundary=Boundary");
    client.print("Content-Length: "); client.println(totalLen);
    client.println();
    client.print(head);

    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n = n + 1024) {
      if (n + 1024 < fbLen) { client.write(fbBuf, 1024); fbBuf += 1024; } 
      else if (fbLen % 1024 > 0) { client.write(fbBuf, fbLen % 1024); }
      yield();
    }
    client.print(tail);
    delay(100);
  }
  
  client.stop(); // Освобождение SSL-сокета для предотвращения утечки RAM
  esp_camera_fb_return(fb); 
}

void setup() {
  // Аппаратное отключение детектора просадки напряжения (Brownout Detector)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  Serial.begin(115200);
  initCamera();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  fbConfigCam.database_url = DATABASE_URL;
  fbConfigCam.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&fbConfigCam, &fbAuthCam);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (Firebase.ready() && (millis() - lastCamPoll > 1000)) {
    lastCamPoll = millis();
    
    // Считывание параметров управления из базы данных
    if (Firebase.getString(fbdoCam, "/control/user_tg_id")) {
      if (fbdoCam.dataType() == "string" && fbdoCam.stringData().length() > 0) {
        targetUserChatId = fbdoCam.stringData();
      }
    }

    if (Firebase.getBool(fbdoCam, "/control/command_snap")) {
      if (fbdoCam.dataType() == "boolean" && fbdoCam.boolData() == true) {
        captureAndSend();
        Firebase.setBool(fbdoCam, "/control/command_snap", false);
      }
    }
  }
  yield();
}
