#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ====================== Wi-Fi config ======================
// Change Wi-Fi SSID and password here.
const char* WIFI_SSID = "ciallo";
const char* WIFI_PASS = "20060311";

// ====================== AI 瑙ｆ瀽鏈嶅姟閰嶇疆 ======================
const char* AI_PARSE_URL = "http://192.168.137.1:5000/parse_text";
const char* AI_VOICE_URL = "http://192.168.137.1:5000/voice";
const unsigned long AI_HTTP_TIMEOUT_MS = 8000;
const unsigned long VOICE_HTTP_TIMEOUT_MS = 60000;

// ====================== 寮曡剼閰嶇疆 ======================
const int PIN_LED = 5;
const int PIN_BUZZER = 4;
const int PIN_HUMAN = 15;
const int PIN_BUTTON = 6;

const int PIN_OLED_SDA = 8;
const int PIN_OLED_SCL = 9;

const int PIN_I2S_BCLK = 10;
const int PIN_I2S_LRCLK = 11;
const int PIN_I2S_DIN = 12;

const int PIN_SPK_BCLK = 16;
const int PIN_SPK_LRCLK = 17;
const int PIN_SPK_DOUT = 18;

#define FAN_PIN 13
#define LAMP_PIN 14

// Most LR7843 modules turn on with HIGH. If your module is inverted, change only these.
const bool FAN_ACTIVE_HIGH = true;
const bool LAMP_ACTIVE_HIGH = true;

// Reserved RGB pin. Not used in this version.
const int PIN_RGB_RESERVED = 48;

// ====================== Level config ======================
// GPIO15 HIGH means human detected. Change to LOW if the module is inverted.
const int HUMAN_ACTIVE_LEVEL = HIGH;

// LED is active high by default.
const int LED_ON_LEVEL = HIGH;
const int LED_OFF_LEVEL = LOW;

// Buzzer is active high by default.
const int BUZZER_ON_LEVEL = HIGH;
const int BUZZER_OFF_LEVEL = LOW;

// ====================== OLED 閰嶇疆 ======================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

// ====================== WebServer ======================
WebServer server(80);

// ====================== 鍖椾含鏃堕棿閰嶇疆 ======================
const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// ====================== 浠诲姟鐘舵€?======================
// 娉ㄦ剰锛氫笉鑳界洿鎺ョ敤 PENDING锛屽洜涓?ESP32 搴曞眰搴撻噷宸茬粡鏈?PENDING
enum TaskState {
  TASK_WAITING,
  TASK_PENDING,
  TASK_ALERTING,
  TASK_DONE
};

struct ReminderTask {
  String title;
  time_t remindEpoch;
  bool active;
  TaskState state;
};

ReminderTask currentTask;

// ====================== 鍏ㄥ眬鐘舵€?======================
time_t currentEpoch = 0;
String currentHHMMSS = "--:--:--";

bool humanNow = false;
bool humanLast = false;

String lastAIInput = "";
String lastAIResult = "AI parse function reserved.";
String lastVoiceResult = "Voice input ready.";
bool micOK = false;
bool voiceBusy = false;
bool speakerOK = false;
String lastSpeakerResult = "Speaker not tested.";
bool fanOn = false;
bool lampOn = false;

// ====================== millis 瀹氭椂 ======================
unsigned long lastSerialMs = 0;
unsigned long lastOLEDMs = 0;
unsigned long lastLedToggleMs = 0;
unsigned long lastNtpRetryMs = 0;

const unsigned long SERIAL_INTERVAL_MS = 1000;
const unsigned long OLED_INTERVAL_MS = 500;
const unsigned long LED_BLINK_INTERVAL_MS = 500;
const unsigned long VOICE_RETRY_INTERVAL_MS = 3000;
const unsigned long NTP_RETRY_INTERVAL_MS = 10000;

// 铚傞福鍣細姣?1500ms 鍝?120ms
const unsigned long BEEP_CYCLE_MS = 1500;
const unsigned long BEEP_ON_MS = 120;

bool ledBlinkState = false;
unsigned long lastVoiceMs = 0;
uint16_t lastVoicePeak = 0;
uint16_t lastVoiceRms = 0;

const i2s_port_t I2S_PORT = I2S_NUM_0;
const int SAMPLE_RATE = 16000;
const int RECORD_SECONDS = 5;

#define MIC_CHANNEL_LEFT 1
#define MIC_CHANNEL_RIGHT 2

const int MIC_CHANNEL_SELECT = MIC_CHANNEL_LEFT;  // L/R to GND uses LEFT.
const int MIC_SAMPLE_SHIFT = 14;                  // Try 14/16/18 when debugging volume/noise.
const bool MIC_DEBUG_PRINT_STATS = true;

const size_t PCM_BYTES = SAMPLE_RATE * RECORD_SECONDS * 2;
const size_t WAV_BYTES = PCM_BYTES + 44;
const unsigned long RECORD_HARD_TIMEOUT_MS = (RECORD_SECONDS + 1) * 1000UL;
const uint16_t VOICE_RMS_THRESHOLD = 80;

const i2s_port_t SPK_I2S_PORT = I2S_NUM_1;
const int SPK_SAMPLE_RATE = 16000;
const int SPK_TONE_AMPLITUDE = 5000;

// ====================== 鍑芥暟澹版槑 ======================
void connectWiFi();
void syncTime();
void updateTime();
bool readHuman();
void checkReminder();
void updateOLED();
void handleBuzzerAndLED();
void handleButton();
void handleAutoVoice();
void initI2SMic();
void initI2SSpeaker();
void playSpeakerTone(uint16_t frequency, uint16_t durationMs);
void setFan(bool on);
void setLamp(bool on);
void setAllDevices(bool on);

void setupWebServer();
void handleRoot();
void handleAddTask();
void handleDoneTask();
void handleClearTask();
void handleTestTask();
void handleAIParse();
void handleVoiceOnce();
void handleSpeakerTest();
void handleFanOn();
void handleFanOff();
void handleLampOn();
void handleLampOff();
void handleAllOff();

void createTestTask();
void createTask(String title, time_t remindEpoch);
void startAlerting();
void markTaskDone();
void clearTask();

const char* stateToString(TaskState state);
const char* stateToShortString(TaskState state);

String formatEpochDateTime(time_t epoch);
String formatEpochHHMM(time_t epoch);
String htmlEscape(String s);
bool parseHHMM(String hhmm, int &hour, int &minute);
bool isNumberString(String s);
time_t getNextEpochByHHMM(int hour, int minute);
void redirectRoot();
void printDebug();
void writeWavHeader(uint8_t* wav, uint32_t pcmLen);
uint8_t* recordWav();
bool createTaskFromAIJson(String responseBody, String &message);
bool uploadVoiceAndCreateTask(uint8_t* wav, String &message);
bool handleControlAction(String action, String &message);


// ====================== setup ======================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=================================");
  Serial.println("ESP32-S3 Smart Reminder Terminal");
  Serial.println("=================================");

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_HUMAN, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LAMP_PIN, OUTPUT);

  digitalWrite(PIN_LED, LED_OFF_LEVEL);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  setFan(false);
  setLamp(false);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOK) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.dim(false);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Reminder");
    display.println("OLED init OK");
    display.display();
  } else {
    Serial.println("OLED init failed. Check wiring and I2C address.");
  }

  connectWiFi();
  setupWebServer();
  syncTime();
  initI2SMic();
  initI2SSpeaker();

  updateTime();

  humanNow = readHuman();
  humanLast = humanNow;

  // 寮€鏈轰笉榛樿鍒涘缓娴嬭瘯浠诲姟锛岄伩鍏嶅奖鍝嶆寮忔紨绀恒€?
  updateOLED();
}


// ====================== loop ======================
void loop() {
  server.handleClient();

  updateTime();

  humanLast = humanNow;
  humanNow = readHuman();

  handleButton();
  checkReminder();
  handleBuzzerAndLED();

  unsigned long nowMs = millis();

  if (nowMs - lastOLEDMs >= OLED_INTERVAL_MS) {
    lastOLEDMs = nowMs;
    updateOLED();
  }

  if (currentEpoch < 100000 &&
      WiFi.status() == WL_CONNECTED &&
      nowMs - lastNtpRetryMs >= NTP_RETRY_INTERVAL_MS) {
    lastNtpRetryMs = nowMs;
    syncTime();
    updateTime();
  }

  if (nowMs - lastSerialMs >= SERIAL_INTERVAL_MS) {
    lastSerialMs = nowMs;
    printDebug();
  }
}


// ====================== Wi-Fi ======================
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  if (oledOK) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.dim(false);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Reminder");
    display.println("WiFi connecting");
    display.println(WIFI_SSID);
    display.display();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startMs = millis();
  int dotCount = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000) {
    delay(500);
    Serial.print(".");
    if (oledOK) {
      dotCount++;
      display.fillRect(0, 32, SCREEN_WIDTH, 16, SSD1306_BLACK);
      display.setCursor(0, 32);
      display.print("Wait ");
      for (int i = 0; i < dotCount % 10; i++) {
        display.print(".");
      }
      display.display();
    }
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    if (oledOK) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("WiFi connected");
      display.println(WiFi.localIP());
      display.display();
    }
  } else {
    Serial.println("Wi-Fi connect failed.");
    Serial.println("Please check Wi-Fi name, password, 2.4GHz network, and antenna.");
    if (oledOK) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("WiFi failed");
      display.println("Check 2.4G/pass");
      display.display();
    }
  }
}


// ====================== NTP 鏃堕棿 ======================
void syncTime() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skip NTP sync because Wi-Fi is not connected.");
    return;
  }

  Serial.println("Syncing time by NTP...");

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    "pool.ntp.org",
    "ntp.aliyun.com",
    "cn.pool.ntp.org"
  );

  struct tm timeinfo;

  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&timeinfo, 500)) {
      Serial.println("NTP time synced.");
      return;
    }
    Serial.print(".");
  }

  Serial.println();
  Serial.println("NTP sync failed. Check Wi-Fi or NTP server.");
}


void updateTime() {
  currentEpoch = time(nullptr);

  if (currentEpoch < 100000) {
    currentHHMMSS = "--:--:--";
    return;
  }

  struct tm timeinfo;
  localtime_r(&currentEpoch, &timeinfo);

  char buf[16];
  snprintf(
    buf,
    sizeof(buf),
    "%02d:%02d:%02d",
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  currentHHMMSS = String(buf);
}


// ====================== 浜轰綋妫€娴?======================
bool readHuman() {
  int raw = digitalRead(PIN_HUMAN);
  return raw == HUMAN_ACTIVE_LEVEL;
}

void setFan(bool on) {
  fanOn = on;
  if (FAN_ACTIVE_HIGH) {
    digitalWrite(FAN_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(FAN_PIN, on ? LOW : HIGH);
  }
  Serial.print("Fan: ");
  Serial.println(on ? "ON" : "OFF");
  Serial.print("Fan pin GPIO");
  Serial.print(FAN_PIN);
  Serial.print(" level=");
  Serial.println(digitalRead(FAN_PIN));
}


void setLamp(bool on) {
  lampOn = on;
  if (LAMP_ACTIVE_HIGH) {
    digitalWrite(LAMP_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(LAMP_PIN, on ? LOW : HIGH);
  }
  Serial.print("Lamp: ");
  Serial.println(on ? "ON" : "OFF");
  Serial.print("Lamp pin GPIO");
  Serial.print(LAMP_PIN);
  Serial.print(" level=");
  Serial.println(digitalRead(LAMP_PIN));
}


void setAllDevices(bool on) {
  setFan(on);
  setLamp(on);
}


// ====================== 鎻愰啋閫昏緫 ======================
void checkReminder() {
  if (!currentTask.active) {
    return;
  }

  if (currentTask.state == TASK_DONE) {
    return;
  }

  if (currentTask.state == TASK_ALERTING) {
    return;
  }

  if (currentTask.state == TASK_WAITING) {
    if (currentEpoch >= currentTask.remindEpoch) {
      if (humanNow) {
        Serial.println("Reminder time reached and Human=YES. Start ALERTING.");
        startAlerting();
      } else {
        Serial.println("Reminder time reached but Human=NO. Enter PENDING.");
        currentTask.state = TASK_PENDING;
      }
    }
    return;
  }

  if (currentTask.state == TASK_PENDING) {
    if (humanNow && !humanLast) {
      Serial.println("Human returned. PENDING -> ALERTING.");
      startAlerting();
    }
  }
}


void startAlerting() {
  if (!currentTask.active) {
    return;
  }

  currentTask.state = TASK_ALERTING;
  ledBlinkState = false;
  lastLedToggleMs = millis();

  digitalWrite(PIN_LED, LED_OFF_LEVEL);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  setLamp(true);

  if (speakerOK) {
    playSpeakerTone(880, 140);
    delay(40);
    playSpeakerTone(1320, 180);
    lastSpeakerResult = "Reminder tone played.";
  }
}


void markTaskDone() {
  if (!currentTask.active) {
    return;
  }

  currentTask.state = TASK_DONE;

  digitalWrite(PIN_LED, LED_OFF_LEVEL);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  setLamp(false);
  setFan(false);

  Serial.println("Task marked as DONE.");
}


void clearTask() {
  currentTask.title = "";
  currentTask.remindEpoch = 0;
  currentTask.active = false;
  currentTask.state = TASK_DONE;

  digitalWrite(PIN_LED, LED_OFF_LEVEL);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  setLamp(false);
  setFan(false);

  Serial.println("Task cleared.");
}


// ====================== LED 鍜岃渹楦ｅ櫒 ======================
void handleBuzzerAndLED() {
  if (!currentTask.active || currentTask.state != TASK_ALERTING) {
    digitalWrite(PIN_LED, LED_OFF_LEVEL);
    digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
    return;
  }

  unsigned long nowMs = millis();

  // Blink LED while alerting.
  if (nowMs - lastLedToggleMs >= LED_BLINK_INTERVAL_MS) {
    lastLedToggleMs = nowMs;
    ledBlinkState = !ledBlinkState;
    digitalWrite(PIN_LED, ledBlinkState ? LED_ON_LEVEL : LED_OFF_LEVEL);
  }

  // Short intermittent buzzer beep.
  unsigned long phase = nowMs % BEEP_CYCLE_MS;

  if (phase < BEEP_ON_MS) {
    digitalWrite(PIN_BUZZER, BUZZER_ON_LEVEL);
  } else {
    digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  }
}


// ====================== 鎸夐敭纭 ======================
void handleButton() {
  static int lastRaw = HIGH;
  static int stableState = HIGH;
  static unsigned long lastChangeMs = 0;

  int raw = digitalRead(PIN_BUTTON);

  if (raw != lastRaw) {
    lastChangeMs = millis();
    lastRaw = raw;
  }

  if (millis() - lastChangeMs > 30) {
    if (raw != stableState) {
      stableState = raw;

      // INPUT_PULLUP锛氭寜涓嬩负 LOW
      if (stableState == LOW) {
        if (currentTask.active && currentTask.state == TASK_ALERTING) {
          Serial.println("Button pressed. Confirm DONE.");
          markTaskDone();
        } else {
          Serial.println("Button pressed, but task is not ALERTING.");
        }
      }
    }
  }
}

void initI2SMic() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    // INMP441 L/R to GND uses LEFT; L/R to 3V3 uses RIGHT.
    .channel_format = (MIC_CHANNEL_SELECT == MIC_CHANNEL_RIGHT)
        ? I2S_CHANNEL_FMT_ONLY_RIGHT
        : I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = PIN_I2S_BCLK,
    .ws_io_num = PIN_I2S_LRCLK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_DIN
  };

  if (i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL) != ESP_OK) {
    Serial.println("INMP441 I2S driver install failed.");
    lastVoiceResult = "Mic driver failed.";
    return;
  }

  if (i2s_set_pin(I2S_PORT, &pinConfig) != ESP_OK) {
    Serial.println("INMP441 I2S pin config failed.");
    lastVoiceResult = "Mic pin config failed.";
    return;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  micOK = true;
  Serial.println("INMP441 manual voice ready.");
  Serial.print("INMP441 pins: BCLK=");
  Serial.print(PIN_I2S_BCLK);
  Serial.print(" LRCLK=");
  Serial.print(PIN_I2S_LRCLK);
  Serial.print(" DIN=");
  Serial.println(PIN_I2S_DIN);

  Serial.print("INMP441 channel: ");
  Serial.println(MIC_CHANNEL_SELECT == MIC_CHANNEL_RIGHT ? "RIGHT" : "LEFT");

  Serial.print("INMP441 sample shift: >>");
  Serial.println(MIC_SAMPLE_SHIFT);
}

void initI2SSpeaker() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SPK_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = PIN_SPK_BCLK,
    .ws_io_num = PIN_SPK_LRCLK,
    .data_out_num = PIN_SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(SPK_I2S_PORT, &i2sConfig, 0, NULL) != ESP_OK) {
    Serial.println("MAX98357A I2S driver install failed.");
    lastSpeakerResult = "Speaker driver failed.";
    return;
  }

  if (i2s_set_pin(SPK_I2S_PORT, &pinConfig) != ESP_OK) {
    Serial.println("MAX98357A I2S pin config failed.");
    lastSpeakerResult = "Speaker pin config failed.";
    i2s_driver_uninstall(SPK_I2S_PORT);
    return;
  }

  i2s_zero_dma_buffer(SPK_I2S_PORT);
  speakerOK = true;
  lastSpeakerResult = "MAX98357A ready.";

  Serial.print("MAX98357A pins: BCLK=");
  Serial.print(PIN_SPK_BCLK);
  Serial.print(" LRC=");
  Serial.print(PIN_SPK_LRCLK);
  Serial.print(" DIN=");
  Serial.println(PIN_SPK_DOUT);
}

void playSpeakerTone(uint16_t frequency, uint16_t durationMs) {
  if (!speakerOK || frequency == 0 || durationMs == 0) {
    return;
  }

  const size_t FRAMES_PER_CHUNK = 128;
  int16_t stereo[FRAMES_PER_CHUNK * 2];
  uint32_t totalFrames = ((uint32_t)SPK_SAMPLE_RATE * durationMs) / 1000;
  uint32_t generatedFrames = 0;
  float phase = 0.0f;
  float phaseStep = 2.0f * PI * frequency / SPK_SAMPLE_RATE;

  while (generatedFrames < totalFrames) {
    size_t frames = totalFrames - generatedFrames;
    if (frames > FRAMES_PER_CHUNK) {
      frames = FRAMES_PER_CHUNK;
    }

    for (size_t i = 0; i < frames; i++) {
      int16_t sample = (int16_t)(sinf(phase) * SPK_TONE_AMPLITUDE);
      stereo[i * 2] = sample;
      stereo[i * 2 + 1] = sample;
      phase += phaseStep;
      if (phase >= 2.0f * PI) {
        phase -= 2.0f * PI;
      }
    }

    size_t bytesWritten = 0;
    esp_err_t result = i2s_write(
        SPK_I2S_PORT,
        stereo,
        frames * 2 * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY);
    if (result != ESP_OK) {
      lastSpeakerResult = "Speaker write failed.";
      Serial.println(lastSpeakerResult);
      return;
    }

    generatedFrames += frames;
  }

  memset(stereo, 0, sizeof(stereo));
  size_t bytesWritten = 0;
  i2s_write(SPK_I2S_PORT, stereo, sizeof(stereo), &bytesWritten, portMAX_DELAY);
}

void writeWavHeader(uint8_t* wav, uint32_t pcmLen) {
  uint32_t riffSize = pcmLen + 36;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint16_t blockAlign = 2;
  uint16_t bitsPerSample = 16;
  uint16_t audioFormat = 1;
  uint16_t channels = 1;
  uint32_t fmtSize = 16;

  memcpy(wav + 0, "RIFF", 4);
  memcpy(wav + 4, &riffSize, 4);
  memcpy(wav + 8, "WAVE", 4);
  memcpy(wav + 12, "fmt ", 4);
  memcpy(wav + 16, &fmtSize, 4);
  memcpy(wav + 20, &audioFormat, 2);
  memcpy(wav + 22, &channels, 2);
  memcpy(wav + 24, &sampleRate, 4);
  memcpy(wav + 28, &byteRate, 4);
  memcpy(wav + 32, &blockAlign, 2);
  memcpy(wav + 34, &bitsPerSample, 2);
  memcpy(wav + 36, "data", 4);
  memcpy(wav + 40, &pcmLen, 4);
}

uint8_t* recordWav() {
  uint8_t* wav = (uint8_t*)ps_malloc(WAV_BYTES);
  if (!wav) {
    wav = (uint8_t*)malloc(WAV_BYTES);
  }
  if (!wav) {
    lastVoiceResult = "Voice memory failed.";
    return NULL;
  }

  writeWavHeader(wav, PCM_BYTES);
  uint8_t* pcm = wav + 44;
  size_t written = 0;
  int32_t raw[256];
  uint16_t peak = 0;
  uint64_t sumSquares = 0;
  uint32_t sampleCount = 0;
  int16_t minSample = 32767;
  int16_t maxSample = -32768;
  uint32_t zeroCount = 0;
  uint32_t negOneCount = 0;
  uint32_t clipCount = 0;
  uint32_t positiveCount = 0;
  uint32_t negativeCount = 0;
  unsigned long startedAt = millis();

  Serial.println("Voice recording...");
  while (written < PCM_BYTES && millis() - startedAt < RECORD_HARD_TIMEOUT_MS) {
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, pdMS_TO_TICKS(100));
    if (result != ESP_OK || bytesRead == 0) {
      continue;
    }

    int count = bytesRead / sizeof(int32_t);
    for (int i = 0; i < count && written + 2 <= PCM_BYTES; i++) {
      int32_t sample = raw[i] >> MIC_SAMPLE_SHIFT;
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      int16_t sample16 = (int16_t)sample;
      if (sample16 < minSample) minSample = sample16;
      if (sample16 > maxSample) maxSample = sample16;
      if (sample16 == 0) zeroCount++;
      if (sample16 == -1) negOneCount++;
      if (sample16 == 32767 || sample16 == -32768) clipCount++;
      if (sample16 > 0) positiveCount++;
      if (sample16 < 0) negativeCount++;
      int32_t sampleValue = sample16;
      uint32_t absolute = abs(sampleValue);
      if (absolute > 32767) {
        absolute = 32767;
      }
      if (absolute > peak) {
        peak = absolute;
      }
      sumSquares += (int64_t)sampleValue * sampleValue;
      sampleCount++;
      pcm[written++] = sample16 & 0xFF;
      pcm[written++] = (sample16 >> 8) & 0xFF;
    }
  }
  while (written < PCM_BYTES) {
    pcm[written++] = 0;
  }
  lastVoicePeak = peak;
  lastVoiceRms = sampleCount > 0 ? (uint16_t)sqrt((double)sumSquares / sampleCount) : 0;

  if (MIC_DEBUG_PRINT_STATS) {
    Serial.println("===== MIC DEBUG STATS =====");
    Serial.print("samples=");
    Serial.println(sampleCount);
    Serial.print("min=");
    Serial.print(minSample);
    Serial.print(" max=");
    Serial.println(maxSample);
    Serial.print("peak=");
    Serial.print(lastVoicePeak);
    Serial.print(" rms=");
    Serial.println(lastVoiceRms);
    Serial.print("zero=");
    Serial.print(zeroCount);
    Serial.print(" negOne=");
    Serial.println(negOneCount);
    Serial.print("clip=");
    Serial.println(clipCount);
    Serial.print("positive=");
    Serial.print(positiveCount);
    Serial.print(" negative=");
    Serial.println(negativeCount);
    Serial.println("===========================");
  }

  return wav;
}

bool handleControlAction(String action, String &message) {
  action.trim();

  if (action == "fan_on") {
    fanOn = true;
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, HIGH);
    Serial.print("Fan: ON direct GPIO");
    Serial.print(FAN_PIN);
    Serial.print(" level=");
    Serial.println(digitalRead(FAN_PIN));
  } else if (action == "fan_off") {
    fanOn = false;
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
    Serial.print("Fan: OFF direct GPIO");
    Serial.print(FAN_PIN);
    Serial.print(" level=");
    Serial.println(digitalRead(FAN_PIN));
  } else if (action == "lamp_on") {
    setLamp(true);
  } else if (action == "lamp_off") {
    setLamp(false);
  } else if (action == "all_on") {
    setAllDevices(true);
  } else if (action == "all_off") {
    setAllDevices(false);
  } else {
    message = "Unknown control action: " + action;
    return false;
  }

  message = "Control OK: " + action;
  lastAIResult = message;
  Serial.println(message);
  return true;
}

bool createTaskFromAIJson(String responseBody, String &message) {
  StaticJsonDocument<1536> doc;
  DeserializationError err = deserializeJson(doc, responseBody);
  if (err) {
    message = "Voice JSON parse failed.";
    return false;
  }

  bool success = doc["success"] | false;
  if (!success) {
    message = doc["error"] | "No reminder in voice.";
    return false;
  }

  String asrText = doc["recognized_text"] | "";
  if (asrText.length() == 0) {
    asrText = doc["asr_text"] | "";
  }

  String resultType = doc["type"] | "";
  if (resultType == "control") {
    String action = doc["action"] | "";
    if (asrText.length() > 0) {
      Serial.println("ASR: " + asrText);
    }
    return handleControlAction(action, message);
  }

  String title = doc["title"] | "Voice Task";
  long delaySeconds = doc["delay_seconds"] | -1;
  String remindTime = doc["remind_time"] | "";

  time_t targetEpoch = 0;
  if (delaySeconds > 0) {
    targetEpoch = currentEpoch + delaySeconds;
  } else if (remindTime.length() > 0) {
    int hour = 0;
    int minute = 0;
    if (!parseHHMM(remindTime, hour, minute)) {
      message = "Invalid voice remind_time.";
      return false;
    }
    targetEpoch = getNextEpochByHHMM(hour, minute);
  } else {
    message = "Voice result has no valid time.";
    return false;
  }

  createTask(title, targetEpoch);
  message = "Parsed: " + title + " @ " + formatEpochDateTime(targetEpoch);
  if (asrText.length() > 0) {
    Serial.println("ASR: " + asrText);
  }
  return true;
}

bool uploadVoiceAndCreateTask(uint8_t* wav, String &message) {
  HTTPClient http;
  WiFiClient client;

  http.begin(client, AI_VOICE_URL);
  http.setTimeout(VOICE_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "audio/wav");

  int httpCode = http.POST(wav, WAV_BYTES);
  String responseBody = http.getString();
  http.end();

  Serial.print("Voice HTTP code: ");
  Serial.println(httpCode);
  Serial.println(responseBody);

  if (httpCode != 200) {
    StaticJsonDocument<512> doc;
    String serverError = "";

    if (deserializeJson(doc, responseBody) == DeserializationError::Ok) {
      serverError = doc["error"] | "";
    }

    message = "Voice HTTP " + String(httpCode);
    if (serverError.length() > 0) {
      message += ": " + serverError;
    }
    return false;
  }

  return createTaskFromAIJson(responseBody, message);
}

void handleAutoVoice() {
  // Disabled for demo stability. Hardware mic recording is triggered only by /voice_once.
}


// ====================== OLED 鏄剧ず ======================
void updateOLED() {
  if (!oledOK) {
    return;
  }

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.dim(false);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  String taskTitle = currentTask.active ? currentTask.title : "No task";

  if (taskTitle.length() > 15) {
    taskTitle = taskTitle.substring(0, 15);
  }

  display.println("Smart Reminder");

  display.print("Time: ");
  display.println(currentHHMMSS);

  display.print("Human: ");
  display.println(humanNow ? "YES" : "NO");

  display.print("Voice: ");
  display.println(voiceBusy ? "LISTEN" : lastVoiceResult.substring(0, 14));

  display.print("Task: ");
  display.println(taskTitle);

  display.print("State: ");
  if (currentTask.active) {
    display.println(stateToShortString(currentTask.state));
  } else {
    display.println("NONE");
  }

  if (currentTask.active) {
    display.print("At: ");
    display.println(formatEpochHHMM(currentTask.remindEpoch));
  }

  display.display();
}


// ====================== WebServer ======================
void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/add", HTTP_POST, handleAddTask);
  server.on("/done", HTTP_POST, handleDoneTask);
  server.on("/clear", HTTP_POST, handleClearTask);
  server.on("/test", HTTP_POST, handleTestTask);
  server.on("/ai", HTTP_POST, handleAIParse);
  server.on("/ai", HTTP_GET, handleAIParse);
  server.on("/voice_once", HTTP_POST, handleVoiceOnce);
  server.on("/speaker_test", HTTP_POST, handleSpeakerTest);
  server.on("/fan_on", HTTP_POST, handleFanOn);
  server.on("/fan_off", HTTP_POST, handleFanOff);
  server.on("/lamp_on", HTTP_POST, handleLampOn);
  server.on("/lamp_off", HTTP_POST, handleLampOff);
  server.on("/all_off", HTTP_POST, handleAllOff);
  server.on("/fan_on", HTTP_GET, handleFanOn);
  server.on("/fan_off", HTTP_GET, handleFanOff);
  server.on("/lamp_on", HTTP_GET, handleLampOn);
  server.on("/lamp_off", HTTP_GET, handleLampOff);
  server.on("/all_off", HTTP_GET, handleAllOff);

  server.onNotFound([]() {
    server.send(404, "text/plain", "404 Not Found");
  });

  server.begin();

  Serial.println("WebServer started.");
}


void handleRoot() {
  updateTime();

  String taskTitle = currentTask.active ? currentTask.title : "No task";
  String taskState = currentTask.active ? stateToString(currentTask.state) : "NO TASK";
  String remindTime = currentTask.active ? formatEpochDateTime(currentTask.remindEpoch) : "-";

  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32-S3 Smart Reminder</title>";

  html += "<style>";
  html += "body{font-family:Arial,Helvetica,sans-serif;margin:20px;background:#f5f5f5;}";
  html += ".card{background:white;padding:16px;margin-bottom:16px;border-radius:10px;box-shadow:0 2px 8px #ddd;}";
  html += "input,textarea,button{font-size:16px;padding:8px;margin:5px 0;width:100%;box-sizing:border-box;}";
  html += "textarea{min-height:86px;resize:vertical;}";
  html += "button{background:#1976d2;color:white;border:none;border-radius:6px;}";
  html += ".danger{background:#d32f2f;}";
  html += ".ok{background:#388e3c;}";
  html += ".gray{background:#616161;}";
  html += ".warn{background:#f57c00;}";
  html += ".muted{color:#666;font-size:14px;}";
  html += "</style>";

  html += "</head><body>";

  html += "<h2>ESP32-S3 Smart Reminder</h2>";

  html += "<div class='card'>";
  html += "<p><b>Current Time:</b> " + htmlEscape(currentHHMMSS) + "</p>";
  html += "<p><b>Human:</b> " + String(humanNow ? "YES" : "NO") + "</p>";
  html += "<p><b>Task:</b> " + htmlEscape(taskTitle) + "</p>";
  html += "<p><b>State:</b> " + htmlEscape(taskState) + "</p>";
  html += "<p><b>Remind Time:</b> " + htmlEscape(remindTime) + "</p>";
  html += "<p><b>Last AI Result:</b> " + htmlEscape(lastAIResult) + "</p>";
  html += "<p><b>Last Voice Result:</b> " + htmlEscape(lastVoiceResult) + "</p>";
  html += "<p><b>Voice Peak:</b> " + String(lastVoicePeak) + "</p>";
  html += "<p><b>Voice RMS:</b> " + String(lastVoiceRms) + "</p>";
  html += "<p><b>Speaker:</b> " + htmlEscape(lastSpeakerResult) + "</p>";
  html += "<p><b>Fan:</b> " + String(fanOn ? "ON" : "OFF") + "</p>";
  html += "<p><b>Lamp:</b> " + String(lampOn ? "ON" : "OFF") + "</p>";

  if (WiFi.status() == WL_CONNECTED) {
    html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  } else {
    html += "<p><b>WiFi:</b> Disconnected</p>";
  }

  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Add Task</h3>";
  html += "<form action='/add' method='POST'>";
  html += "<label>Title:</label><br>";
  html += "<input name='title' placeholder='Submit Homework' required>";
  html += "<label>Remind Time HH:MM:</label><br>";
  html += "<input name='remind_time' type='time' required>";
  html += "<button type='submit'>Add Task</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>AI / Web Voice Input</h3>";
  html += "<form id='aiForm' action='/ai' method='POST'>";
  html += "<textarea id='aiText' name='text' placeholder='Example: remind me to drink water in five minutes'>" + htmlEscape(lastAIInput) + "</textarea>";
  html += "<button class='warn' type='button' id='voiceBtn'>Start Web Voice Input</button>";
  html += "<button class='gray' type='button' id='clearAiBtn'>Clear</button>";
  html += "<button type='submit'>Submit AI Parse</button>";
  html += "</form>";
  html += "<p><b>Last Input:</b> " + htmlEscape(lastAIInput) + "</p>";
  html += "<p><b>Result:</b> " + htmlEscape(lastAIResult) + "</p>";
  html += "<p id='speechStatus' class='muted'></p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Hardware Mic Debug</h3>";
  html += "<form action='/voice_once' method='POST'>";
  html += "<button class='gray' type='submit'>Hardware Mic Record Once</button>";
  html += "</form>";
  html += "<p class='muted'>INMP441 debug entry. It does not record automatically.</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>MAX98357A Speaker</h3>";
  html += "<form action='/speaker_test' method='POST'>";
  html += "<button class='gray' type='submit'>Test Speaker Tone</button>";
  html += "</form>";
  html += "<p class='muted'>BCLK=GPIO16, LRC=GPIO17, DIN=GPIO18.</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Fan / Lamp Control</h3>";
  html += "<p><b>Fan:</b> " + String(fanOn ? "ON" : "OFF") + "</p>";
  html += "<p><b>Lamp:</b> " + String(lampOn ? "ON" : "OFF") + "</p>";
  html += "<form action='/fan_on' method='POST'>";
  html += "<button class='ok' type='submit'>Fan ON</button>";
  html += "</form>";
  html += "<form action='/fan_off' method='POST'>";
  html += "<button class='gray' type='submit'>Fan OFF</button>";
  html += "</form>";
  html += "<form action='/lamp_on' method='POST'>";
  html += "<button class='ok' type='submit'>Lamp ON</button>";
  html += "</form>";
  html += "<form action='/lamp_off' method='POST'>";
  html += "<button class='gray' type='submit'>Lamp OFF</button>";
  html += "</form>";
  html += "<form action='/all_off' method='POST'>";
  html += "<button class='danger' type='submit'>All OFF</button>";
  html += "</form>";
  html += "<p class='muted'>Fan PWM/IN=GPIO13, Lamp PWM/IN=GPIO14. No RGB control.</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Control</h3>";

  html += "<form action='/done' method='POST'>";
  html += "<button class='ok' type='submit'>Confirm Done</button>";
  html += "</form>";

  html += "<form action='/clear' method='POST'>";
  html += "<button class='danger' type='submit'>Clear Task</button>";
  html += "</form>";

  html += "<form action='/test' method='POST'>";
  html += "<button class='gray' type='submit'>Create 1 Minute Test Task</button>";
  html += "</form>";

  html += "</div>";

  html += "<p>Refresh this page to update status.</p>";

  html += "<script>";
  html += "const aiText=document.getElementById('aiText');";
  html += "const statusEl=document.getElementById('speechStatus');";
  html += "document.getElementById('clearAiBtn').onclick=function(){aiText.value='';statusEl.textContent='';};";
  html += "const SpeechRecognition=window.SpeechRecognition||window.webkitSpeechRecognition;";
  html += "if(!SpeechRecognition){document.getElementById('voiceBtn').disabled=true;statusEl.textContent='Web Speech unsupported. Use keyboard voice input.';}";
  html += "else{const r=new SpeechRecognition();r.lang='zh-CN';r.continuous=false;r.interimResults=true;";
  html += "r.onstart=function(){statusEl.textContent='Listening...';};";
  html += "r.onerror=function(e){statusEl.textContent='Speech failed: '+e.error;};";
  html += "r.onend=function(){statusEl.textContent=aiText.value.trim()?'Voice text ready.':'Voice ended.';};";
  html += "r.onresult=function(e){let s='';for(let i=0;i<e.results.length;i++){s+=e.results[i][0].transcript;}aiText.value=s.trim();};";
  html += "document.getElementById('voiceBtn').onclick=function(){try{aiText.value='';r.start();}catch(e){statusEl.textContent='Speech already running.';}};}";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}


void handleAddTask() {
  updateTime();

  if (!server.hasArg("title") || !server.hasArg("remind_time")) {
    server.send(400, "text/plain", "Missing title or remind_time.");
    return;
  }

  if (currentEpoch < 100000) {
    server.send(500, "text/plain", "Time is not ready. Check Wi-Fi and NTP.");
    return;
  }

  String title = server.arg("title");
  String hhmm = server.arg("remind_time");

  title.trim();
  hhmm.trim();

  if (title.length() == 0) {
    title = "Untitled";
  }

  int hour = 0;
  int minute = 0;

  if (!parseHHMM(hhmm, hour, minute)) {
    server.send(400, "text/plain", "Invalid time format. Use HH:MM.");
    return;
  }

  time_t targetEpoch = getNextEpochByHHMM(hour, minute);

  createTask(title, targetEpoch);

  Serial.print("Web added task: ");
  Serial.print(title);
  Serial.print(", remind at ");
  Serial.println(formatEpochDateTime(targetEpoch));

  redirectRoot();
}


void handleDoneTask() {
  if (currentTask.active) {
    markTaskDone();
  }

  redirectRoot();
}


void handleClearTask() {
  clearTask();
  redirectRoot();
}


void handleTestTask() {
  createTestTask();
  redirectRoot();
}

void handleSpeakerTest() {
  if (!speakerOK) {
    lastSpeakerResult = "Speaker is not ready.";
    redirectRoot();
    return;
  }

  playSpeakerTone(660, 220);
  delay(50);
  playSpeakerTone(990, 280);
  lastSpeakerResult = "Speaker test tone played.";
  Serial.println(lastSpeakerResult);
  redirectRoot();
}

void handleFanOn() {
  fanOn = true;
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, HIGH);
  Serial.print("Fan: ON direct GPIO");
  Serial.print(FAN_PIN);
  Serial.print(" level=");
  Serial.println(digitalRead(FAN_PIN));
  lastAIResult = "Fan ON";
  redirectRoot();
}


void handleFanOff() {
  fanOn = false;
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  Serial.print("Fan: OFF direct GPIO");
  Serial.print(FAN_PIN);
  Serial.print(" level=");
  Serial.println(digitalRead(FAN_PIN));
  lastAIResult = "Fan OFF";
  redirectRoot();
}


void handleLampOn() {
  setLamp(true);
  lastAIResult = "Lamp ON";
  redirectRoot();
}


void handleLampOff() {
  setLamp(false);
  lastAIResult = "Lamp OFF";
  redirectRoot();
}


void handleAllOff() {
  setAllDevices(false);
  lastAIResult = "All devices OFF";
  redirectRoot();
}


void handleAIParse() {
  updateTime();

  String argName = "";
  if (server.hasArg("text")) {
    argName = "text";
  } else if (server.hasArg("nl_text")) {
    argName = "nl_text";
  }

  if (argName.length() == 0) {
    lastAIInput = "";
    lastAIResult = "No AI input.";
    redirectRoot();
    return;
  }

  lastAIInput = server.arg(argName);
  lastAIInput.trim();

  if (lastAIInput.length() == 0) {
    lastAIResult = "Empty AI input.";
    redirectRoot();
    return;
  }

  if (currentEpoch < 100000) {
    lastAIResult = "Time not ready. Please wait for NTP.";
    Serial.println("AI parse failed: time not ready.");
    redirectRoot();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastAIResult = "WiFi not connected.";
    Serial.println("AI parse failed: WiFi not connected.");
    redirectRoot();
    return;
  }

  Serial.println("Sending text to AI parse server...");
  Serial.print("AI input: ");
  Serial.println(lastAIInput);

  HTTPClient http;
  WiFiClient client;

  http.begin(client, AI_PARSE_URL);
  http.setTimeout(AI_HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> reqDoc;
  reqDoc["text"] = lastAIInput;

  String requestBody;
  serializeJson(reqDoc, requestBody);

  int httpCode = http.POST(requestBody);
  String responseBody = http.getString();

  http.end();

  Serial.print("AI server HTTP code: ");
  Serial.println(httpCode);
  Serial.print("AI server response: ");
  Serial.println(responseBody);

  if (httpCode != 200) {
    lastAIResult = "AI server error, HTTP code: " + String(httpCode);
    redirectRoot();
    return;
  }

  StaticJsonDocument<1024> respDoc;
  DeserializationError err = deserializeJson(respDoc, responseBody);

  if (err) {
    lastAIResult = "JSON parse failed.";
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    redirectRoot();
    return;
  }

  bool success = respDoc["success"] | false;

  if (!success) {
    const char* errorMsg = respDoc["error"] | "AI parse failed.";
    lastAIResult = String(errorMsg);
    redirectRoot();
    return;
  }

  String resultType = respDoc["type"] | "";
  if (resultType == "control") {
    String action = respDoc["action"] | "";
    String message;
    if (!handleControlAction(action, message)) {
      lastAIResult = message;
    }
    redirectRoot();
    return;
  }

  String title = respDoc["title"] | "AI Task";
  long delaySeconds = respDoc["delay_seconds"] | -1;
  String remindTime = respDoc["remind_time"] | "";

  time_t targetEpoch = 0;

  if (delaySeconds > 0) {
    targetEpoch = currentEpoch + delaySeconds;
  } else if (remindTime.length() > 0) {
    int hour = 0;
    int minute = 0;

    if (!parseHHMM(remindTime, hour, minute)) {
      lastAIResult = "Invalid remind_time from AI.";
      redirectRoot();
      return;
    }

    targetEpoch = getNextEpochByHHMM(hour, minute);
  } else {
    lastAIResult = "AI result has no valid time.";
    redirectRoot();
    return;
  }

  createTask(title, targetEpoch);

  lastAIResult = "Parsed: " + title + " @ " + formatEpochDateTime(targetEpoch);

  Serial.print("AI task created: ");
  Serial.print(title);
  Serial.print(" at ");
  Serial.println(formatEpochDateTime(targetEpoch));

  redirectRoot();
}


void handleVoiceOnce() {
  updateTime();

  if (voiceBusy) {
    lastVoiceResult = "Voice is busy. Please wait.";
    redirectRoot();
    return;
  }

  if (!micOK) {
    lastVoiceResult = "Mic not ready. Check INMP441 wiring.";
    redirectRoot();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastVoiceResult = "WiFi not connected.";
    redirectRoot();
    return;
  }

  if (currentEpoch < 100000) {
    lastVoiceResult = "Time not ready. Please wait for NTP.";
    redirectRoot();
    return;
  }

  voiceBusy = true;
  lastVoiceMs = millis();
  lastVoiceResult = "Recording hardware mic...";

  uint8_t* wav = recordWav();
  if (!wav) {
    Serial.println(lastVoiceResult);
    voiceBusy = false;
    redirectRoot();
    return;
  }

  Serial.print("Voice once peak=");
  Serial.print(lastVoicePeak);
  Serial.print(" rms=");
  Serial.println(lastVoiceRms);

  String micDebugResult = "Recorded. peak=" + String(lastVoicePeak) +
      " rms=" + String(lastVoiceRms) +
      " channel=" + String(MIC_CHANNEL_SELECT == MIC_CHANNEL_RIGHT ? "RIGHT" : "LEFT") +
      " shift=" + String(MIC_SAMPLE_SHIFT);
  lastVoiceResult = micDebugResult;

  if (lastVoiceRms < VOICE_RMS_THRESHOLD) {
    lastVoiceResult = "Voice too quiet or not detected. " + micDebugResult;
    Serial.println(lastVoiceResult + " Uploading WAV for audio diagnostics.");
  }

  String message;
  bool ok = uploadVoiceAndCreateTask(wav, message);
  free(wav);

  lastVoiceResult = ok ? message : "Voice ignored: " + message + " | " + micDebugResult;
  Serial.println(lastVoiceResult);
  voiceBusy = false;

  redirectRoot();
}


// ====================== 浠诲姟鍒涘缓 ======================
void createTestTask() {
  updateTime();

  if (currentEpoch < 100000) {
    Serial.println("Warning: time is not ready. Test task time may be wrong.");
  }

  time_t nowEpoch = time(nullptr);
  createTask("Submit Homework", nowEpoch + 60);

  Serial.println("Test task created: Submit Homework, current time + 1 minute.");
}


void createTask(String title, time_t remindEpoch) {
  currentTask.title = title;
  currentTask.remindEpoch = remindEpoch;
  currentTask.active = true;
  currentTask.state = TASK_WAITING;

  digitalWrite(PIN_LED, LED_OFF_LEVEL);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);

  Serial.print("Task created: ");
  Serial.print(currentTask.title);
  Serial.print(", remind time: ");
  Serial.println(formatEpochDateTime(currentTask.remindEpoch));
}


// ====================== 宸ュ叿鍑芥暟 ======================
const char* stateToString(TaskState state) {
  switch (state) {
    case TASK_WAITING:
      return "WAITING";
    case TASK_PENDING:
      return "PENDING";
    case TASK_ALERTING:
      return "ALERTING";
    case TASK_DONE:
      return "DONE";
    default:
      return "UNKNOWN";
  }
}


const char* stateToShortString(TaskState state) {
  switch (state) {
    case TASK_WAITING:
      return "WAIT";
    case TASK_PENDING:
      return "PEND";
    case TASK_ALERTING:
      return "ALERT";
    case TASK_DONE:
      return "DONE";
    default:
      return "UNKN";
  }
}


String formatEpochDateTime(time_t epoch) {
  if (epoch < 100000) {
    return "NTP not ready";
  }

  struct tm timeinfo;
  localtime_r(&epoch, &timeinfo);

  char buf[32];
  snprintf(
    buf,
    sizeof(buf),
    "%04d-%02d-%02d %02d:%02d:%02d",
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  return String(buf);
}


String formatEpochHHMM(time_t epoch) {
  if (epoch < 100000) {
    return "--:--";
  }

  struct tm timeinfo;
  localtime_r(&epoch, &timeinfo);

  char buf[8];
  snprintf(
    buf,
    sizeof(buf),
    "%02d:%02d",
    timeinfo.tm_hour,
    timeinfo.tm_min
  );

  return String(buf);
}


String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}


bool isNumberString(String s) {
  if (s.length() == 0) {
    return false;
  }

  for (int i = 0; i < s.length(); i++) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }

  return true;
}


bool parseHHMM(String hhmm, int &hour, int &minute) {
  hhmm.trim();

  int colonIndex = hhmm.indexOf(':');
  if (colonIndex <= 0) {
    return false;
  }

  String hourStr = hhmm.substring(0, colonIndex);
  String minuteStr = hhmm.substring(colonIndex + 1);

  if (!isNumberString(hourStr) || !isNumberString(minuteStr)) {
    return false;
  }

  hour = hourStr.toInt();
  minute = minuteStr.toInt();

  if (hour < 0 || hour > 23) {
    return false;
  }

  if (minute < 0 || minute > 59) {
    return false;
  }

  return true;
}


time_t getNextEpochByHHMM(int hour, int minute) {
  time_t nowEpoch = currentEpoch;

  struct tm timeinfo;
  localtime_r(&nowEpoch, &timeinfo);

  long secondsToday =
    timeinfo.tm_hour * 3600L +
    timeinfo.tm_min * 60L +
    timeinfo.tm_sec;

  time_t localTodayMidnight = nowEpoch - secondsToday;

  time_t targetEpoch =
    localTodayMidnight +
    hour * 3600L +
    minute * 60L;

  // 濡傛灉浠婂ぉ杩欎釜鏃堕棿宸茬粡杩囦簡锛屽氨鑷姩璁句负鏄庡ぉ
  if (targetEpoch <= nowEpoch) {
    targetEpoch += 24L * 3600L;
  }

  return targetEpoch;
}


void redirectRoot() {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}


void printDebug() {
  String taskTitle = currentTask.active ? currentTask.title : "No task";
  String taskState = currentTask.active ? stateToString(currentTask.state) : "NO TASK";
  String remindTime = currentTask.active ? formatEpochDateTime(currentTask.remindEpoch) : "-";

  Serial.print("Time=");
  Serial.print(currentHHMMSS);

  Serial.print(" | Human=");
  Serial.print(humanNow ? "YES" : "NO");

  Serial.print(" | Task=");
  Serial.print(taskTitle);

  Serial.print(" | State=");
  Serial.print(taskState);

  Serial.print(" | Remind=");
  Serial.print(remindTime);

  Serial.print(" | VoicePeak=");
  Serial.print(lastVoicePeak);

  Serial.print(" | VoiceRMS=");
  Serial.print(lastVoiceRms);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" | IP=");
    Serial.print(WiFi.localIP());
  } else {
    Serial.print(" | WiFi=Disconnected");
  }

  Serial.println();
}
