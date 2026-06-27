# ESP32-S3 存在感知智能提醒终端

本项目是一个基于 ESP32-S3 的智能提醒终端。ESP32-S3 负责网页交互、OLED 显示、提醒状态机、LD2410C 存在感知、蜂鸣器/LED/灯板/风扇控制、INMP441 硬件麦克风采样；电脑端 Python Flask 服务负责语音识别中转和 DeepSeek 自然语言解析。

## 目录结构

```text
ESP32/
├─ Arduino_code/
│  └─ AI.ino                 # Arduino IDE 主程序
├─ transfer/
│  ├─ app.py                 # Flask 中转服务：/parse_text、/voice、/last_voice.wav
│  ├─ requirements.txt       # Python 依赖
│  ├─ run_server.bat         # Windows 一键启动脚本
│  └─ .env.example           # 环境变量示例，不包含真实密钥
├─ .gitignore
├─ README.md
└─ 项目交接说明.md
```

## 硬件引脚

| 功能 | ESP32-S3 引脚 |
|---|---|
| LED | GPIO5 |
| 蜂鸣器 | GPIO4 |
| LD2410C OUT | GPIO15 |
| 确认按键 | GPIO6 |
| OLED SDA | GPIO8 |
| OLED SCL | GPIO9 |
| INMP441 SCK/BCLK | GPIO10 |
| INMP441 WS/LRCLK | GPIO11 |
| INMP441 SD/DIN | GPIO12 |
| 风扇 LR7843 PWM/IN | GPIO13 |
| 灯板 LR7843 PWM/IN | GPIO14 |
| MAX98357A BCLK | GPIO16 |
| MAX98357A LRC | GPIO17 |
| MAX98357A DIN | GPIO18 |

INMP441 默认按 `L/R -> GND` 使用左声道。风扇和灯板通过 LR7843 MOSFET 模块控制，默认高电平导通。

## Python 环境配置

推荐 Python 3.10 或 3.11。首次运行前进入 `transfer` 目录：

```powershell
cd transfer
copy .env.example .env
```

编辑 `.env`，填入自己的 DeepSeek API Key：

```env
DEEPSEEK_API_KEY=your_deepseek_api_key_here
DEEPSEEK_BASE_URL=https://api.deepseek.com
DEEPSEEK_MODEL=deepseek-v4-pro
WHISPER_MODEL=tiny
ASR_DEVICE=cpu
ASR_COMPUTE_TYPE=int8
```

安装依赖并启动：

```powershell
python -m venv .venv
.\.venv\Scripts\activate
python -m pip install -r requirements.txt
python app.py
```

也可以双击或运行：

```powershell
run_server.bat
```

服务启动后访问：

```text
http://127.0.0.1:5000/health
```

ESP32 上传的最近一次硬件麦克风录音可访问：

```text
http://电脑IP:5000/last_voice.wav
```

## Arduino IDE 烧录

1. 打开 `Arduino_code/AI.ino`。
2. 安装 ESP32 Arduino 开发板支持。
3. 安装依赖库：
   - ArduinoJson
   - Adafruit GFX Library
   - Adafruit SSD1306
4. 选择开发板：`ESP32S3 Dev Module`。
5. 在 `AI.ino` 顶部修改 Wi-Fi 名称、密码和 Python 服务地址：

```cpp
const char* WIFI_SSID = "你的WiFi";
const char* WIFI_PASS = "你的密码";
const char* AI_PARSE_URL = "http://电脑IP:5000/parse_text";
const char* AI_VOICE_URL = "http://电脑IP:5000/voice";
```

6. 编译并上传到 ESP32-S3。
7. 串口监视器波特率设为 `115200`，查看 ESP32 获取到的 IP。

## 功能测试

### 1. 手动提醒

打开 ESP32 网页，在 `Add Task` 输入标题和 HH:MM 时间，提交后到点提醒。

### 2. AI 文字输入

在网页 `AI / Web Voice Input` 输入：

```text
五分钟后提醒我喝水
```

预期创建提醒任务。

### 3. 网页语音输入

点击 `Start Web Voice Input`，说出提醒或控制命令，识别成文字后点击 `Submit AI Parse`。

### 4. 硬件麦克风

点击 `Hardware Mic Record Once`，ESP32 录制 INMP441 音频并上传给 Python `/voice`，Python 使用 faster-whisper 识别后再解析。

### 5. 风扇和灯板控制

网页按钮：

- `Fan ON / Fan OFF`
- `Lamp ON / Lamp OFF`
- `All OFF`

AI/语音命令示例：

```text
打开风扇
关闭风扇
打开台灯
关闭台灯
打开灯带
关闭灯带
```

