# ESP32-S3 存在感知智能提醒终端

这是一个基于 ESP32-S3 的存在感知智能提醒终端。设备端使用 Arduino IDE 开发，负责网页控制、OLED 显示、提醒状态机、人体存在检测、环境采集、蜂鸣器/LED 提醒、风扇/灯带控制、硬件麦克风录音和 MP3 播放控制；电脑端 Python Flask 服务负责语音识别和自然语言解析。

## 主要功能

- 手动添加 HH:MM 定时提醒。
- 网页 AI 文字输入和网页语音输入。
- INMP441 硬件麦克风录音上传到 Python `/voice`。
- Python 使用 faster-whisper 做语音识别。
- Python `/parse_text` 支持 DeepSeek 解析，也支持本地规则解析控制命令。
- 单任务提醒状态机：`WAITING` / `PENDING` / `ALERTING` / `DONE`。
- LD2410C 人体存在感知。
- OLED 显示时间、任务、状态和环境信息。
- 蜂鸣器、LED、MAX98357A 提示音提醒。
- 风扇和普通灯带/COB 灯板网页手动控制。
- 风扇和灯带语音控制：打开/关闭风扇，打开/关闭灯带。
- BH1750、AHT20、BMP280 环境数据显示。
- Auto Env Mode：有人且温度高自动开风扇，有人且光照低自动开灯带，无人 10 秒自动关闭。
- MP3-TF-16P 音乐控制，支持播放、暂停、上一首、下一首、音量调节和按编号播放。

## 目录结构

```text
ESP32_可运行调试版_含风扇台灯语音控制/
├─ Arduino_code/
│  └─ code/
│     └─ code.ino        # Arduino IDE 主程序
├─ transfer/
│  ├─ app.py             # Flask 服务：/parse_text、/voice、/health
│  ├─ requirements.txt   # Python 依赖
│  ├─ run_server.bat     # Windows 启动脚本
│  └─ .env.example       # 环境变量示例
├─ README.md
├─ 项目交接说明.md
└─ 环境传感器调试说明.md
```

## 硬件引脚

| 模块 | ESP32-S3 引脚 |
|---|---|
| OLED SDA | GPIO8 |
| OLED SCL | GPIO9 |
| 蜂鸣器 | GPIO4 |
| LED | GPIO5 |
| 确认按键 | GPIO6 |
| INMP441 SCK | GPIO10 |
| INMP441 WS | GPIO11 |
| INMP441 SD | GPIO12 |
| 风扇 LR7843 PWM | GPIO13 |
| 灯带/COB 灯板 LR7843 PWM | GPIO14 |
| LD2410C OUT | GPIO15 |
| MAX98357A BCLK | GPIO16 |
| MAX98357A LRC/LRCLK | GPIO17 |
| MAX98357A DIN | GPIO18 |
| MP3-TF-16P RX | ESP32 GPIO47 TX |
| MP3-TF-16P TX | ESP32 GPIO48 RX |

BH1750、AHT20、BMP280 与 OLED 共用 I2C：`SDA -> GPIO8`，`SCL -> GPIO9`，电源接 `3V3` 和 `GND`。

## 运行方式

### 1. 启动 Python 服务

进入 `transfer` 目录，按需复制 `.env.example` 为 `.env` 并填写 DeepSeek 配置，然后运行：

```powershell
python app.py
```

也可以直接运行：

```powershell
run_server.bat
```

常用接口：

- `GET /health`：服务状态检查。
- `POST /parse_text`：文字解析提醒或控制命令。
- `POST /voice`：接收 ESP32 上传的 WAV 并识别解析。
- `GET /last_voice.wav`：查看最近一次硬件麦克风录音。

### 2. 烧录 Arduino 程序

使用 Arduino IDE 打开：

```text
Arduino_code/code/code.ino
```

确认代码中的 Wi-Fi 和 Python 服务地址正确，例如：

```cpp
const char* AI_PARSE_URL = "http://电脑IP:5000/parse_text";
const char* AI_VOICE_URL = "http://电脑IP:5000/voice";
```

选择 ESP32-S3 开发板后编译上传，串口监视器波特率使用 `115200`。

## MP3 说明

MP3-TF-16P 当前使用 GPIO47/48 直接串口协议控制，不依赖 `DFRobotDFPlayerMini` 库。TF 卡歌曲建议放在根目录，命名为：

```text
00.mp3
01.mp3
02.mp3
...
40.mp3
```


