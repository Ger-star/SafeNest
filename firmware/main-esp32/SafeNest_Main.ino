/*
 * ============================================================================
 *  Проект: SafeNest v1.0
 *  Модуль: Главный управляющий контроллер (ESP32 WROOM-32)
 *  Архитектура: Асинхронное распределение ядер FreeRTOS:
 *               Core 0 — Генерация звука сирены реального времени
 *               Core 1 — Сбор данных датчиков, ЖК-дисплей и сетевые протоколы
 * ============================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Preferences.h>
#include <FirebaseESP32.h>

// Конфигурация сети и сервисов
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
#define DATABASE_URL "YOUR_FIREBASE_DATABASE_URL"
#define DATABASE_SECRET "YOUR_FIREBASE_DATABASE_SECRET"

// Конфигурация периферии
#define DHTPIN 23
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define MQ9_PIN 34
#define BUZZER_PIN 25     
float CO_PPM_THRESHOLD = 100.0f;

LiquidCrystal_I2C lcd(0x27, 16, 2);  
Preferences prefs;

FirebaseData fbdo;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;

// Интервалы выполнения
unsigned long lastTimeSensorsUpdate = 0;
const unsigned long sensorsInterval = 2000; 

unsigned long lastCarouselSwitch = 0;
const unsigned long carouselInterval = 10000;

unsigned long lastLcdUpdate = 0;
const unsigned long lcdUpdateInterval = 500;

unsigned long lastControlPoll = 0;
const unsigned long controlPollInterval = 1500; 

unsigned long lastAlarmBlink = 0;
bool alarmBacklightOn = true;

// Состояние тревоги
int alarmConfirmCounter = 0;
volatile bool realAlarmState = false;
volatile bool isMuted = false;
bool fireTriggered = false;
volatile unsigned long alarmStartTime = 0;
bool telegramAlarmSent = false;
int lastSentAlarmLevel = 0; 
String dynamicUserChatId = "DEFAULT_CHAT_ID"; 

volatile int victoryBeepStep = -1;
volatile unsigned long victoryBeepTime = 0;

// Пользовательские настройки
int currentLang = 1; // 0: KK, 1: RU, 2: EN
volatile int sirenMode = 0;
bool lcdBacklightOn = true;
bool carouselEnabled = true;
int carouselScreen = 0;

// Телеметрические данные
float current_t = 0.0f;
float current_h = 0.0f;
float current_co_ppm = 0.0f;
float heat_index = 0.0f;

float max_t = -99.0f;
float min_t = 99.0f;
float max_co = 0.0f;

char lcdBuffer[17];

// ----------------------------------------------------------------------------
// МАТЕМАТИЧЕСКАЯ ОБРАБОТКА И ПРЕОБРАЗОВАНИЕ ДАННЫХ
// ----------------------------------------------------------------------------
float convertToPPM(int raw_adc) {
  if (raw_adc <= 0) raw_adc = 1;
  float volt = (float)raw_adc * 3.3f / 4095.0f;
  if (volt < 0.01f) volt = 0.01f;
  if (volt >= 3.29f) volt = 3.28f;
  float Rs = ((3.3f - volt) / volt) * 10.0f;
  float ratio = Rs / 11.5f;
  if (ratio <= 0.001f) ratio = 0.001f;
  float ppm = 598.12f * pow(ratio, -2.18f);
  return (isnan(ppm) || ppm < 0.0f) ? 0.0f : ppm;
}

float calculateHeatIndex(float temp, float hum) {
  return temp + 0.33f * (hum / 100.0f * 6.105f * exp(17.27f * temp / (237.7f + temp))) - 4.0f;
}

int getSmoothedAnalog() {
  long sum = 0;
  constexpr int samples = 30;
  for (int i = 0; i < samples; i++) { sum += analogRead(MQ9_PIN); delayMicroseconds(20); }
  return sum / samples;
}

String getUptime() {
  unsigned long sec  = millis() / 1000;
  unsigned long mins = sec / 60;
  unsigned long hr   = mins / 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu ч. %lu мин.", hr, mins % 60);
  return String(buf);
}

// ----------------------------------------------------------------------------
// ПОТОК FREERTOS: УПРАВЛЕНИЕ ЗВУКОМ НА ЯДРЕ 0
// ----------------------------------------------------------------------------
void playStartupMelody() {
  int melody[]    = {1046, 1318, 1568, 2093, 1568, 2637};
  int durations[] = {70,   70,   70,   70,   70,   140};
  for (int i = 0; i < 6; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    delay(durations[i] + 20);
  }
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

void playSirenTest(int mode) {
  if (mode == 0) {
    tone(BUZZER_PIN, 2000, 300);
  } else if (mode == 1) {
    tone(BUZZER_PIN, 2800, 150);
  } else if (mode == 2) {
    tone(BUZZER_PIN, 1500, 100);
  } else if (mode == 3) {
    for (int f = 600; f <= 2200; f += 80) {
      tone(BUZZER_PIN, f);
      delay(10);
    }
    noTone(BUZZER_PIN);
  }
}

void sirenTask(void * pvParameters) {
  bool buzzerActive = false;
  int lastFreq = 0;

  for (;;) {
    if (victoryBeepStep >= 0) {
      if (millis() - victoryBeepTime >= 150) {
        victoryBeepTime = millis();
        if (victoryBeepStep < 3) {
          tone(BUZZER_PIN, 2000, 100);
          victoryBeepStep++;
        } else {
          noTone(BUZZER_PIN);
          digitalWrite(BUZZER_PIN, LOW);
          victoryBeepStep = -1;
        }
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (!realAlarmState || isMuted) {
      if (buzzerActive) {
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);
        buzzerActive = false;
        lastFreq = 0;
      }
      vTaskDelay(20 / portTICK_PERIOD_MS);
      continue;
    }

    unsigned long elapsed = millis() - alarmStartTime;

    if (sirenMode == 3) {
      unsigned long phase = elapsed % 800;
      int freq = (phase < 400) ? map(phase, 0, 400, 800, 2600) : map(phase, 400, 800, 2600, 800);
      tone(BUZZER_PIN, freq);
      buzzerActive = true;
      vTaskDelay(15 / portTICK_PERIOD_MS);
      continue;
    }

    unsigned long onMs = 300, offMs = 300;
    int pitch = 2200;

    if (sirenMode == 1) { onMs = 100; offMs = 100; pitch = 2800; } 
    else if (sirenMode == 2) { onMs = 50; offMs = 250; pitch = 1800; }

    if (elapsed > 60000) { onMs = 150; offMs = 9850; } 

    unsigned long cyclePos = elapsed % (onMs + offMs);

    if (cyclePos < onMs) {
      if (!buzzerActive || lastFreq != pitch) {
        tone(BUZZER_PIN, pitch);
        buzzerActive = true;
        lastFreq = pitch;
      }
    } else {
      if (buzzerActive) {
        noTone(BUZZER_PIN);
        buzzerActive = false;
        lastFreq = 0;
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void startVictoryBeep() { victoryBeepStep = 0; victoryBeepTime = millis(); }

String urlEncode(const String &str) {
  String out = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < str.length(); i++) {
    uint8_t c = (uint8_t)str.charAt(i);
    if (isalnum(c)) out += (char)c;
    else if (c == ' ') out += '+';
    else { out += '%'; out += hex[(c >> 4) & 0xF]; out += hex[c & 0xF]; }
  }
  return out;
}

// ----------------------------------------------------------------------------
// ЛОКАЛИЗАЦИЯ И МНОГОЯЗЫЧНЫЙ МОДУЛЬ ОПОВЕЩЕНИЯ
// ----------------------------------------------------------------------------
void sendTelegramAlarm(const String &msg) {
  if (Firebase.ready()) {
    Firebase.setBool(fbdo, "/control/command_snap", true);
  }
  
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(2500);
  
  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) +
               "/sendMessage?chat_id=" + dynamicUserChatId +
               "&text=" + urlEncode(msg);
               
  if (https.begin(client, url)) {
    https.GET();
    https.end();
  }
}

String buildAlarmMessage(int level, float ppm) {
  String ppmStr = String(ppm, 1);
  
  if (level == 1) { 
    if (currentLang == 0) return "⚠️ ЕСКЕРТУ — Иіс газы (CO)!\nАнықталған деңгей: " + ppmStr + " ppm\nБазалық нормадан асты. Бөлмені желдетіңіз.";
    if (currentLang == 1) return "⚠️ ПРЕДУПРЕЖДЕНИЕ — Угарный газ (CO)!\nОбнаруженный уровень: " + ppmStr + " ppm\nПревышена базовая норма. Проветрите помещение.";
    return "⚠️ WARNING — Carbon Monoxide (CO)!\nDetected level: " + ppmStr + " ppm\nExceeded normal threshold. Ventilate the room.";
  }
  
  if (level == 2) { 
    if (currentLang == 0) return "🚨 ЖОҒАРЫ ҚАУІП — Иіс газы!\nАнықталған деңгей: " + ppmStr + " ppm\nCO деңгейі нормадан 1.5 есе жоғары! Бөлмеден шығыңыз немесе терезені ашыңыз!";
    if (currentLang == 1) return "🚨 ВЫСОКАЯ ОПАСНОСТЬ — Угарный газ!\nОбнаруженный уровень: " + ppmStr + " ppm\nУровень CO в 1.5 раза выше нормы! Покиньте помещение или откройте окна!";
    return "🚨 HIGH DANGER — Carbon Monoxide!\nDetected level: " + ppmStr + " ppm\nCO level is 1.5x above threshold! Leave the area or open windows!";
  }
  
  if (currentLang == 0) {
    return "🚨 ҚАУІП ДЕҢГЕЙІ — ИІС ГАЗЫ!\n\n"
           "Анықталған CO деңгейі: " + ppmStr + " ppm\n"
           "⚠️ ҒИМАРАТТАН ДЕРЕУ ЭВАКУАЦИЯЛАНЫҢЫЗ!\n"
           "🏃 Мүмкіндігінше тезірек шығуға ұмтылыңыз.\n\n"
           "👨‍👩‍👧 Балаларға, қарттарға және айналадағыларға көмектесіңіз.\n\n"
           "🚪 Жолда терезелер немесе есіктер болса, желдету үшін ашыңыз.\n\n"
           "😷 Матадан жасалған немесе әдеттегі маскалар CO-дан қорғамайды.\n\n"
           "🌬 Далаға шыққан соң, ғимараттан алысырақ болыңыз.\n\n"
           "❌ Қауіпсіз деп жариялағанша ішке қайта кірмеңіз.\n"
           "📞 Құтқару қызметі: 112";
  }
  
  if (currentLang == 1) {
    return "🚨 УРОВЕНЬ ОПАСНОСТИ — УГАРНЫЙ ГАЗ!\n\n"
           "Обнаруженный уровень CO: " + ppmStr + " ppm\n"
           "⚠️ НЕМЕДЛЕННО ЭВАКУИРУЙТЕСЬ ИЗ ПОМЕЩЕНИЯ!\n"
           "🏃 Как можно быстрее идите к выходу. Не тратьте время на сбор вещей.\n\n"
           "👨‍👩‍👧 Помогите детям, пожилым людям и окружающим выйти на улицу.\n\n"
           "🚪 Если по пути есть окна или двери, откройте их для проветривания.\n\n"
           "😷 Тканевые или обычные маски НЕ защищают от CO, не используйте их.\n\n"
           "🌬 Оказавшись снаружи, отойдите подальше от здания на свежий воздух.\n\n"
           "❌ Не возвращайтесь внутрь, пока службы не подтвердят безопасность.\n"
           "📞 Служба спасения: 112";
  }

  return "🚨 DANGER LEVEL — CARBON MONOXIDE!\n\n"
         "CO level detected: " + ppmStr + " ppm\n"
         "⚠️ EVACUATE THE ROOM IMMEDIATELY!\n"
         "🏃 Move to the exit as fast as possible.\n\n"
         "👨‍👩‍👧 Help children, elderly, and others around you to get outside.\n\n"
         "🚪 Open windows or doors on your way for ventilation.\n\n"
         "😷 Regular masks do not protect against CO.\n\n"
         "🌬 Once outside, stay away from the building in the fresh air.\n\n"
         "❌ Do not re-enter until emergency services declare it safe.\n"
         "📞 Emergency Services";
}

void drawCoBar(float ppm, float threshold) {
  float ratio = constrain(ppm / threshold, 0.0f, 1.0f);
  int fullCols = (int)(ratio * 16.0f);
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    if (i < fullCols) lcd.write(byte(4));
    else lcd.print(' ');
  }
}

void setBacklight(bool on) {
  static bool lastState = true;
  if (on == lastState) return;
  lastState = on;
  if (on) lcd.backlight(); else lcd.noBacklight();
}

// ----------------------------------------------------------------------------
// ИНИЦИАЛИЗАЦИЯ И НАСТРОЙКА (SETUP)
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  analogSetAttenuation(ADC_11db);
  Wire.begin(21, 22);

  lcd.init(); lcd.backlight(); lcd.clear();
  lcd.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int wifiRetry = 0;
  while (WiFi.status() != WL_CONNECTED && wifiRetry < 20) { delay(500); Serial.print("."); wifiRetry++; }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear(); lcd.print("WiFi Connected!");
  } else {
    lcd.clear(); lcd.print("WiFi Failed!");
  }
  delay(1000);

  prefs.begin("safenest", false);
  CO_PPM_THRESHOLD = prefs.getFloat("cothresh", 100.0f);

  byte lvl5[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00};
  lcd.createChar(4, lvl5);

  if (WiFi.status() == WL_CONNECTED) {
    configTime(5 * 3600, 0, "pool.ntp.org", "time.nist.gov"); 
    time_t now = time(nullptr);
    int ntpRetry = 0;
    while (now < 8 * 3600 * 2 && ntpRetry < 15) { delay(500); now = time(nullptr); ntpRetry++; }
  }

  dht.begin();
  delay(1000); 

  fbConfig.database_url = DATABASE_URL;
  fbConfig.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  // Выделение ресурса Core 0 под задачу сирены FreeRTOS
  xTaskCreatePinnedToCore(
    sirenTask,
    "SirenTask",
    2048,
    NULL,
    1,
    NULL,
    0
  );

  lcd.clear(); lcd.print("SafeNest v1.0");
  playStartupMelody();
  delay(1000); lcd.clear();
}

// ----------------------------------------------------------------------------
// ОБМЕН ДАННЫМИ С ОБЛАЧНЫМИ СЕРВИСАМИ
// ----------------------------------------------------------------------------
void pushTelemetry() {
  if (!Firebase.ready()) return;
  FirebaseJson json;
  json.set("temperature", current_t);
  json.set("humidity", current_h);
  json.set("heat_index", heat_index);
  json.set("co_ppm", current_co_ppm);
  json.set("threshold", CO_PPM_THRESHOLD);
  json.set("max_t", max_t);
  json.set("min_t", min_t);
  json.set("max_co", max_co);
  json.set("is_muted", isMuted);
  json.set("alarm", realAlarmState);
  json.set("fire", fireTriggered);
  json.set("lcd_backlight", lcdBacklightOn);
  json.set("carousel_enabled", carouselEnabled);
  json.set("current_lang", currentLang);
  json.set("siren_mode", sirenMode);
  json.set("uptime", getUptime());
  json.set("ip", WiFi.localIP().toString());
  json.set("last_seen/.sv", "timestamp");
  Firebase.updateNode(fbdo, "/telemetry", json);
}

void pollControlNode() {
  if (!Firebase.ready()) return;
  if (millis() - lastControlPoll < controlPollInterval) return;
  lastControlPoll = millis();

  if (!Firebase.getJSON(fbdo, "/control")) return;

  FirebaseJson &json = fbdo.to<FirebaseJson>();
  FirebaseJsonData result;

  json.get(result, "user_tg_id");
  if (result.success && result.stringValue.length() > 0) {
    dynamicUserChatId = result.stringValue;
  }

  json.get(result, "command_reboot");
  if (result.success && result.boolValue == true) {
    Firebase.setBool(fbdo, "/control/command_reboot", false);
    lcd.clear(); lcd.print("Rebooting..."); delay(500); ESP.restart();
  }

  json.get(result, "threshold");
  if (result.success) {
    float newThresh = (float)result.intValue;
    if (newThresh >= 10.0f && fabs(newThresh - CO_PPM_THRESHOLD) > 0.5f) {
      CO_PPM_THRESHOLD = newThresh;
      prefs.putFloat("cothresh", CO_PPM_THRESHOLD);
    }
  }

  json.get(result, "is_muted"); if (result.success) isMuted = result.boolValue;
  json.get(result, "carousel_enabled"); if (result.success) carouselEnabled = result.boolValue;
  json.get(result, "current_lang"); if (result.success) currentLang = result.intValue;
  
  json.get(result, "siren_mode"); 
  if (result.success) { 
    int newMode = result.intValue;
    if (newMode != sirenMode) {
      sirenMode = newMode;
      if (!realAlarmState) {
        playSirenTest(sirenMode);
      }
    }
  }
  
  json.get(result, "lcd_backlight"); if (result.success) { lcdBacklightOn = result.boolValue; setBacklight(lcdBacklightOn); }
}

// ----------------------------------------------------------------------------
// ОСНОВНОЙ ЦИКЛ ОБРАБОТКИ ДАННЫХ (CORE 1)
// ----------------------------------------------------------------------------
void loop() {
  if (millis() - lastTimeSensorsUpdate >= sensorsInterval || lastTimeSensorsUpdate == 0) {
    lastTimeSensorsUpdate = millis();
    float h_read = dht.readHumidity();
    float t_read = dht.readTemperature();
    if (!isnan(h_read) && !isnan(t_read)) {
      current_h = h_read; current_t = t_read;
      heat_index = calculateHeatIndex(current_t, current_h);
      if (current_t > max_t) max_t = current_t;
      if (current_t < min_t) min_t = current_t;
      if (current_t > 50.0f && current_h < 6.0f && !realAlarmState) {
        realAlarmState = true; fireTriggered = true; alarmStartTime = millis();
      }
    } else { current_h = 0.0f; current_t = 0.0f; }
    int raw_adc = getSmoothedAnalog();
    current_co_ppm = convertToPPM(raw_adc);
    if (current_co_ppm > max_co) max_co = current_co_ppm;
    if (current_co_ppm > CO_PPM_THRESHOLD) { alarmConfirmCounter++; }
    else if (current_co_ppm < (CO_PPM_THRESHOLD * 0.8f)) { if (alarmConfirmCounter > 0) alarmConfirmCounter--; }
    if (alarmConfirmCounter >= 3 && !realAlarmState) { realAlarmState = true; alarmStartTime = millis(); }
    pushTelemetry();
  }

  pollControlNode();

  if (millis() - lastLcdUpdate >= lcdUpdateInterval) {
    lastLcdUpdate = millis();
    if (carouselEnabled && !realAlarmState) {
      setBacklight(lcdBacklightOn);
      if (millis() - lastCarouselSwitch >= carouselInterval) {
        lastCarouselSwitch = millis();
        carouselScreen = (carouselScreen + 1) % 3;
        lcd.clear();
      }
      if (carouselScreen == 0) {
        snprintf(lcdBuffer, sizeof(lcdBuffer), "T:%.1fC H:%.1f%%", current_t, current_h);
        lcd.setCursor(0,0); lcd.print(lcdBuffer);
        snprintf(lcdBuffer, sizeof(lcdBuffer), "FeelsLike:%.1fC", heat_index);
        lcd.setCursor(0,1); lcd.print(lcdBuffer);
      } else if (carouselScreen == 1) {
        snprintf(lcdBuffer, sizeof(lcdBuffer), "CO: %.1f ppm   ", current_co_ppm);
        lcd.setCursor(0,0); lcd.print(lcdBuffer);
        drawCoBar(current_co_ppm, CO_PPM_THRESHOLD);
      } else {
        snprintf(lcdBuffer, sizeof(lcdBuffer), "Up:%s", getUptime().c_str());
        lcd.setCursor(0,0); lcd.print(lcdBuffer);
        lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString() + "   ");
      }
    }
  }

  if (realAlarmState) {
    if (millis() - lastLcdUpdate >= lcdUpdateInterval) {
      lcd.setCursor(0, 0); lcd.print("!!! WARNING !!! ");
      lcd.setCursor(0, 1); lcd.print(isMuted ? "!!! MUTED !!!   " : "!!! ALARM !!!   ");
    }
    if (millis() - lastAlarmBlink >= 400) {
      lastAlarmBlink = millis(); alarmBacklightOn = !alarmBacklightOn; setBacklight(alarmBacklightOn);
    }
    
    int currentAlarmLevel = 1;
    if (fireTriggered || current_co_ppm >= (CO_PPM_THRESHOLD * 2.0f)) {
      currentAlarmLevel = 3;
    } else if (current_co_ppm >= (CO_PPM_THRESHOLD * 1.5f)) {
      currentAlarmLevel = 2;
    }

    if (!telegramAlarmSent || currentAlarmLevel > lastSentAlarmLevel) {
      sendTelegramAlarm(buildAlarmMessage(currentAlarmLevel, current_co_ppm));
      telegramAlarmSent = true;
      lastSentAlarmLevel = currentAlarmLevel;
    }

    if (current_co_ppm < (CO_PPM_THRESHOLD * 0.8f) && !fireTriggered) {
      realAlarmState = false; fireTriggered = false; isMuted = false; telegramAlarmSent = false; lastSentAlarmLevel = 0; alarmConfirmCounter = 0;
      setBacklight(lcdBacklightOn); lcd.clear(); startVictoryBeep();
      
      String clearMsg = "✅ Воздух чист! SafeNest вернулся в режим охраны.";
      if (currentLang == 0) clearMsg = "✅ Ауа тазарды! SafeNest күзет режиміне оралды.";
      else if (currentLang == 2) clearMsg = "✅ Air is clean! SafeNest returned to guard mode.";
      sendTelegramAlarm(clearMsg);
    }
  }

  yield();
}
