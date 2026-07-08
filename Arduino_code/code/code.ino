#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
#include <driver/i2s.h>
#include <math.h>

// Arduino Library Manager: install Adafruit AHTX0, Adafruit BMP280 Library,
// BH1750, Adafruit Unified Sensor, Adafruit SSD1306 and Adafruit GFX.

// ====================== Wi-Fi config ======================
// Change Wi-Fi SSID and password here.
const char* WIFI_SSID = "your wifi";
const char* WIFI_PASS = "your password";

// ====================== AI parse service config ======================
const char* AI_PARSE_URL = "http://your IP/parse_text";
const char* AI_VOICE_URL = "http://your IP/voice";
const unsigned long AI_HTTP_TIMEOUT_MS = 8000;
const unsigned long VOICE_HTTP_TIMEOUT_MS = 60000;

// ====================== Pin config ======================
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

// MP3-TF-16P / DFPlayer Mini serial pins. Do not change current wiring.
const int PIN_MP3_TX = 47;  // ESP32 TX -> MP3 RX
const int PIN_MP3_RX = 48;  // ESP32 RX <- MP3 TX

#define FAN_PIN 13
#define LAMP_PIN 14

// Most LR7843 modules turn on with HIGH. If your module is inverted, change only these.
const bool FAN_ACTIVE_HIGH = true;
const bool LAMP_ACTIVE_HIGH = true;

// Reserved RGB pin. Not used in this version. GPIO48 is used by MP3 RX now.
const int PIN_RGB_RESERVED = -1;

// ====================== Level config ======================
// GPIO15 HIGH means human detected. Change to LOW if the module is inverted.
const int HUMAN_ACTIVE_LEVEL = HIGH;

// LED is active high by default.
const int LED_ON_LEVEL = HIGH;
const int LED_OFF_LEVEL = LOW;

// Buzzer is active high by default.
const int BUZZER_ON_LEVEL = HIGH;
const int BUZZER_OFF_LEVEL = LOW;

// ====================== OLED config ======================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

// ====================== WebServer ======================
WebServer server(80);

// ====================== Beijing time config ======================
const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// ====================== Task state ======================
// Note: avoid the name PENDING because some ESP32 libraries may already define it.
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

// ====================== Global state ======================
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

// ====================== Environment sensors ======================
BH1750 lightMeter;
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

bool bh1750OK = false;
bool aht20OK = false;
bool bmp280OK = false;

float envTempC = NAN;
float envHumidity = NAN;
float envPressureHpa = NAN;
float envLux = NAN;

unsigned long lastEnvReadMs = 0;
const unsigned long ENV_READ_INTERVAL_MS = 2000;

bool autoEnvMode = false;

const float FAN_ON_TEMP_C = 28.0;
const float FAN_OFF_TEMP_C = 26.0;
const float LAMP_ON_LUX = 80.0;
const float LAMP_OFF_LUX = 150.0;

unsigned long lastHumanAbsentMs = 0;
const unsigned long AUTO_OFF_DELAY_MS = 10000;

// ====================== millis timers ======================
unsigned long lastSerialMs = 0;
unsigned long lastOLEDMs = 0;
unsigned long lastLedToggleMs = 0;
unsigned long lastNtpRetryMs = 0;

const unsigned long SERIAL_INTERVAL_MS = 1000;
const unsigned long OLED_INTERVAL_MS = 500;
const unsigned long LED_BLINK_INTERVAL_MS = 500;
const unsigned long VOICE_RETRY_INTERVAL_MS = 3000;
const unsigned long NTP_RETRY_INTERVAL_MS = 10000;

// Buzzer: beep 120 ms every 1500 ms.
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

HardwareSerial mp3Serial(1);
bool mp3OK = false;
int mp3Volume = 20;
int currentMp3Index = -1;
String lastMusicResult = "MP3 not initialized.";

const int MP3_MIN_INDEX = 0;
const int MP3_MAX_INDEX = 40;
// Most DFPlayer-compatible modules play the first sorted file with play(1).
// With files 00.mp3, 01.mp3..., index 0 usually maps to play(1).
// If your module is off by one, change this to 0.
const int MP3_TRACK_OFFSET = 1;

// ====================== Function declarations ======================
void connectWiFi();
void syncTime();
void updateTime();
bool readHuman();
bool isHumanPresent();
void scanI2CBus();
String scanI2CBusText();
void initEnvironmentSensors();
void readEnvironmentSensors();
void updateAutoEnvironmentControl();
String formatEnvValue(float value, int decimals, const char* suffix);
void checkReminder();
void updateOLED();
void handleBuzzerAndLED();
void handleButton();
void handleAutoVoice();
void initI2SMic();
void initI2SSpeaker();
void playSpeakerTone(uint16_t frequency, uint16_t durationMs);
void initMP3Player();
void mp3SendCommand(uint8_t command, uint16_t parameter);
bool playMp3Index(int index);
bool handleMusicAction(String action, int value, String &message);
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
void handleAutoEnvOn();
void handleAutoEnvOff();
void handleI2CScan();

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
bool handleControlAction(String action, int value, String &message);


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

  initEnvironmentSensors();

  connectWiFi();
  setupWebServer();
  syncTime();
  initI2SMic();
  initI2SSpeaker();
  initMP3Player();

  updateTime();

  humanNow = readHuman();
  humanLast = humanNow;

  // Do not create a test task on boot, to keep demo behavior predictable.
  updateOLED();
}


// ====================== loop ======================
void loop() {
  server.handleClient();

  updateTime();

  humanLast = humanNow;
  humanNow = readHuman();

  readEnvironmentSensors();

  handleButton();
  checkReminder();
  updateAutoEnvironmentControl();
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


// ====================== NTP time ======================
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


// ====================== Human presence detection ======================
bool readHuman() {
  int raw = digitalRead(PIN_HUMAN);
  return raw == HUMAN_ACTIVE_LEVEL;
}

bool isHumanPresent() {
  return humanNow;
}

void scanI2CBus() {
  Serial.println("Scanning I2C bus...");
  int count = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
    }
  }

  Serial.print("I2C scan done. Count=");
  Serial.println(count);
}

String scanI2CBusText() {
  String result = "I2C devices:\n";
  int count = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      if (address < 16) result += "0x0";
      else result += "0x";
      result += String(address, HEX);
      result += "\n";
      count++;
    }
  }

  result += "Count=";
  result += String(count);
  result += "\n";
  return result;
}

void initEnvironmentSensors() {
  scanI2CBus();

  Serial.println("Init BH1750...");
  bh1750OK = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  if (!bh1750OK) {
    Serial.println("BH1750 0x23 failed, trying 0x5C...");
    bh1750OK = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire);
  }
  Serial.println(bh1750OK ? "BH1750 OK" : "BH1750 NOT FOUND");

  Serial.println("Init AHT20...");
  aht20OK = aht.begin(&Wire);
  Serial.println(aht20OK ? "AHT20 OK" : "AHT20 NOT FOUND");

  Serial.println("Init BMP280...");
  bmp280OK = bmp.begin(0x76);
  if (!bmp280OK) {
    Serial.println("BMP280 0x76 failed, trying 0x77...");
    bmp280OK = bmp.begin(0x77);
  }
  Serial.println(bmp280OK ? "BMP280 OK" : "BMP280 NOT FOUND");
}

void readEnvironmentSensors() {
  unsigned long nowMs = millis();
  if (lastEnvReadMs != 0 && nowMs - lastEnvReadMs < ENV_READ_INTERVAL_MS) {
    return;
  }
  lastEnvReadMs = nowMs;

  if (bh1750OK) {
    float lux = lightMeter.readLightLevel();
    if (!isnan(lux) && lux >= 0) {
      envLux = lux;
    }
  }

  if (aht20OK) {
    sensors_event_t humidity;
    sensors_event_t temp;
    aht.getEvent(&humidity, &temp);
    if (!isnan(temp.temperature)) {
      envTempC = temp.temperature;
    }
    if (!isnan(humidity.relative_humidity)) {
      envHumidity = humidity.relative_humidity;
    }
  }

  if (bmp280OK) {
    float pressure = bmp.readPressure() / 100.0F;
    if (!isnan(pressure) && pressure > 0) {
      envPressureHpa = pressure;
    }
    if (!aht20OK) {
      float bmpTemp = bmp.readTemperature();
      if (!isnan(bmpTemp)) {
        envTempC = bmpTemp;
      }
    }
  }

  Serial.print("ENV temp=");
  if (isnan(envTempC)) Serial.print("N/A"); else Serial.print(envTempC, 1);

  Serial.print(" hum=");
  if (isnan(envHumidity)) Serial.print("N/A"); else Serial.print(envHumidity, 1);

  Serial.print(" lux=");
  if (isnan(envLux)) Serial.print("N/A"); else Serial.print(envLux, 1);

  Serial.print(" pressure=");
  if (isnan(envPressureHpa)) Serial.println("N/A"); else Serial.println(envPressureHpa, 1);
}

void updateAutoEnvironmentControl() {
  if (!autoEnvMode) {
    return;
  }

  if (currentTask.active && currentTask.state == TASK_ALERTING) {
    return;
  }

  bool human = isHumanPresent();

  if (human) {
    lastHumanAbsentMs = 0;

    if (!isnan(envTempC)) {
      if (envTempC >= FAN_ON_TEMP_C && !fanOn) {
        setFan(true);
        Serial.print("Auto: human=1 temp=");
        Serial.print(envTempC, 1);
        Serial.println(" -> Fan ON");
      } else if (envTempC <= FAN_OFF_TEMP_C && fanOn) {
        setFan(false);
        Serial.print("Auto: human=1 temp=");
        Serial.print(envTempC, 1);
        Serial.println(" -> Fan OFF");
      }
    }

    if (!isnan(envLux)) {
      if (envLux <= LAMP_ON_LUX && !lampOn) {
        setLamp(true);
        Serial.print("Auto: human=1 lux=");
        Serial.print(envLux, 1);
        Serial.println(" -> Lamp ON");
      } else if (envLux >= LAMP_OFF_LUX && lampOn) {
        setLamp(false);
        Serial.print("Auto: human=1 lux=");
        Serial.print(envLux, 1);
        Serial.println(" -> Lamp OFF");
      }
    }
  } else {
    if (lastHumanAbsentMs == 0) {
      lastHumanAbsentMs = millis();
    }

    if (millis() - lastHumanAbsentMs >= AUTO_OFF_DELAY_MS) {
      if (fanOn || lampOn) {
        setFan(false);
        setLamp(false);
        Serial.println("Auto: human=0 timeout -> Fan/Lamp OFF");
      }
    }
  }
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


// ====================== Reminder logic ======================
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


// ====================== LED and buzzer ======================
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


// ====================== Confirm button ======================
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

      // INPUT_PULLUP: pressed means LOW.
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

void initMP3Player() {
  Serial.println("Init MP3-TF-16P serial control...");
  mp3Serial.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
  delay(500);

  mp3OK = true;
  mp3SendCommand(0x06, mp3Volume);  // Set volume, range 0..30.
  lastMusicResult = "MP3 serial ready. volume=" + String(mp3Volume);
  Serial.println(lastMusicResult);

  Serial.print("MP3 pins: TX=");
  Serial.print(PIN_MP3_TX);
  Serial.print(" RX=");
  Serial.println(PIN_MP3_RX);
}

void mp3SendCommand(uint8_t command, uint16_t parameter) {
  uint8_t frame[10] = {
    0x7E, 0xFF, 0x06, command, 0x00,
    (uint8_t)(parameter >> 8),
    (uint8_t)(parameter & 0xFF),
    0x00, 0x00, 0xEF
  };

  uint16_t sum = 0;
  for (int i = 1; i <= 6; i++) {
    sum += frame[i];
  }
  uint16_t checksum = 0 - sum;
  frame[7] = (uint8_t)(checksum >> 8);
  frame[8] = (uint8_t)(checksum & 0xFF);

  mp3Serial.write(frame, sizeof(frame));
  mp3Serial.flush();
}

bool playMp3Index(int index) {
  if (!mp3OK) {
    lastMusicResult = "MP3 not ready.";
    Serial.println(lastMusicResult);
    return false;
  }

  if (index < MP3_MIN_INDEX || index > MP3_MAX_INDEX) {
    lastMusicResult = "MP3 index out of range: " + String(index);
    Serial.println(lastMusicResult);
    return false;
  }

  int trackNumber = index + MP3_TRACK_OFFSET;
  mp3SendCommand(0x03, trackNumber);
  currentMp3Index = index;

  char fileName[16];
  snprintf(fileName, sizeof(fileName), "%02d.mp3", index);
  lastMusicResult = "Playing " + String(fileName) + " track=" + String(trackNumber);
  Serial.println(lastMusicResult);
  return true;
}

bool handleMusicAction(String action, int value, String &message) {
  if (!mp3OK) {
    message = "MP3 not ready. Check wiring/TF card.";
    lastMusicResult = message;
    Serial.println(message);
    return false;
  }

  if (action == "music_play_index") {
    if (!playMp3Index(value)) {
      message = lastMusicResult;
      return false;
    }
    message = lastMusicResult;
    return true;
  }

  if (action == "music_play") {
    mp3SendCommand(0x0D, 0);
    lastMusicResult = "Music play/resume.";
  } else if (action == "music_pause") {
    mp3SendCommand(0x0E, 0);
    lastMusicResult = "Music paused.";
  } else if (action == "music_next") {
    mp3SendCommand(0x01, 0);
    currentMp3Index = -1;
    lastMusicResult = "Music next.";
  } else if (action == "music_prev") {
    mp3SendCommand(0x02, 0);
    currentMp3Index = -1;
    lastMusicResult = "Music previous.";
  } else if (action == "music_volume_up") {
    if (mp3Volume < 30) mp3Volume++;
    mp3SendCommand(0x06, mp3Volume);
    lastMusicResult = "Music volume=" + String(mp3Volume);
  } else if (action == "music_volume_down") {
    if (mp3Volume > 0) mp3Volume--;
    mp3SendCommand(0x06, mp3Volume);
    lastMusicResult = "Music volume=" + String(mp3Volume);
  } else if (action == "music_volume_set") {
    if (value < 0) value = 0;
    if (value > 30) value = 30;
    mp3Volume = value;
    mp3SendCommand(0x06, mp3Volume);
    lastMusicResult = "Music volume=" + String(mp3Volume);
  } else {
    message = "Unknown music action: " + action;
    return false;
  }

  message = lastMusicResult;
  Serial.println(lastMusicResult);
  return true;
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

bool handleControlAction(String action, int value, String &message) {
  action.trim();

  if (action.startsWith("music_")) {
    bool ok = handleMusicAction(action, value, message);
    if (ok) {
      lastAIResult = message;
    }
    return ok;
  }

  if (action == "fan_on") {
    setFan(true);
  } else if (action == "fan_off") {
    setFan(false);
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
    int actionValue = doc["song_index"] | -1;
    if (actionValue < 0) {
      actionValue = doc["volume"] | -1;
    }
    if (asrText.length() > 0) {
      Serial.println("ASR: " + asrText);
    }
    return handleControlAction(action, actionValue, message);
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


// ====================== OLED display ======================
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

  if (!currentTask.active || currentTask.state == TASK_DONE) {
    display.print("Temp: ");
    display.println(formatEnvValue(envTempC, 1, "C"));

    display.print("Hum: ");
    display.println(formatEnvValue(envHumidity, 0, "%"));

    display.print("Lux: ");
    display.println(formatEnvValue(envLux, 0, ""));

    display.print("Auto: ");
    display.println(autoEnvMode ? "ON" : "OFF");

    display.display();
    return;
  }

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
  server.on("/auto_env_on", HTTP_POST, handleAutoEnvOn);
  server.on("/auto_env_off", HTTP_POST, handleAutoEnvOff);
  server.on("/i2c_scan", HTTP_GET, handleI2CScan);
  server.on("/fan_on", HTTP_GET, handleFanOn);
  server.on("/fan_off", HTTP_GET, handleFanOff);
  server.on("/lamp_on", HTTP_GET, handleLampOn);
  server.on("/lamp_off", HTTP_GET, handleLampOff);
  server.on("/all_off", HTTP_GET, handleAllOff);
  server.on("/auto_env_on", HTTP_GET, handleAutoEnvOn);
  server.on("/auto_env_off", HTTP_GET, handleAutoEnvOff);

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
  html += "<p><b>Music:</b> " + htmlEscape(lastMusicResult) + "</p>";
  html += "<p><b>Fan:</b> " + String(fanOn ? "ON" : "OFF") + "</p>";
  html += "<p><b>Lamp:</b> " + String(lampOn ? "ON" : "OFF") + "</p>";
  html += "<p><b>Auto Env Mode:</b> " + String(autoEnvMode ? "ON" : "OFF") + "</p>";

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
  html += "<h3>Environment</h3>";
  html += "<p><b>Auto Mode:</b> " + String(autoEnvMode ? "ON" : "OFF") + "</p>";
  html += "<p><b>Temp:</b> " + htmlEscape(formatEnvValue(envTempC, 1, " C")) + "</p>";
  html += "<p><b>Humidity:</b> " + htmlEscape(formatEnvValue(envHumidity, 1, " %")) + "</p>";
  html += "<p><b>Lux:</b> " + htmlEscape(formatEnvValue(envLux, 1, " lx")) + "</p>";
  html += "<p><b>Pressure:</b> " + htmlEscape(formatEnvValue(envPressureHpa, 1, " hPa")) + "</p>";
  html += "<p><b>BH1750:</b> " + String(bh1750OK ? "OK" : "NOT FOUND") + "</p>";
  html += "<p><b>AHT20:</b> " + String(aht20OK ? "OK" : "NOT FOUND") + "</p>";
  html += "<p><b>BMP280:</b> " + String(bmp280OK ? "OK" : "NOT FOUND") + "</p>";
  html += "<form action='/auto_env_on' method='POST'>";
  html += "<button class='ok' type='submit'>Auto Mode ON</button>";
  html += "</form>";
  html += "<form action='/auto_env_off' method='POST'>";
  html += "<button class='gray' type='submit'>Auto Mode OFF</button>";
  html += "</form>";
  html += "<form action='/i2c_scan' method='GET'>";
  html += "<button class='warn' type='submit'>I2C Scan</button>";
  html += "</form>";
  html += "<p class='muted'>I2C bus: SDA=GPIO8, SCL=GPIO9. Sensor VCC must use 3V3.</p>";
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
  setFan(true);
  lastAIResult = "Fan ON";
  redirectRoot();
}


void handleFanOff() {
  setFan(false);
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

void handleAutoEnvOn() {
  autoEnvMode = true;
  lastHumanAbsentMs = 0;
  lastAIResult = "Auto environment mode ON";
  Serial.println("Auto Mode ON");
  redirectRoot();
}

void handleAutoEnvOff() {
  autoEnvMode = false;
  lastAIResult = "Auto environment mode OFF";
  Serial.println("Auto Mode OFF");
  redirectRoot();
}

void handleI2CScan() {
  String body = scanI2CBusText();
  Serial.print(body);
  server.send(200, "text/plain", body);
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
    int actionValue = respDoc["song_index"] | -1;
    if (actionValue < 0) {
      actionValue = respDoc["volume"] | -1;
    }
    String message;
    if (!handleControlAction(action, actionValue, message)) {
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


// ====================== Task creation ======================
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


// ====================== Utility functions ======================
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


String formatEnvValue(float value, int decimals, const char* suffix) {
  if (isnan(value)) {
    return "N/A";
  }
  return String(value, decimals) + String(suffix);
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

  // If today's target time already passed, schedule it for tomorrow.
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

  Serial.print(" | AutoEnv=");
  Serial.print(autoEnvMode ? "ON" : "OFF");

  Serial.print(" | Temp=");
  if (isnan(envTempC)) Serial.print("N/A"); else Serial.print(envTempC, 1);

  Serial.print(" | Hum=");
  if (isnan(envHumidity)) Serial.print("N/A"); else Serial.print(envHumidity, 1);

  Serial.print(" | Lux=");
  if (isnan(envLux)) Serial.print("N/A"); else Serial.print(envLux, 1);

  Serial.print(" | Pressure=");
  if (isnan(envPressureHpa)) Serial.print("N/A"); else Serial.print(envPressureHpa, 1);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" | IP=");
    Serial.print(WiFi.localIP());
  } else {
    Serial.print(" | WiFi=Disconnected");
  }

  Serial.println();
}











