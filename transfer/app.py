from flask import Flask, request, jsonify, send_file
from datetime import datetime, timedelta
from dotenv import load_dotenv
import os
import re
import json
import requests
import tempfile
import threading
import io
import math
import wave
from pathlib import Path

load_dotenv(Path(__file__).with_name(".env"), override=True)

app = Flask(__name__)
LAST_VOICE_PATH = Path(__file__).with_name("last_voice.wav")

# 尽量让 Flask 返回中文，而不是 \u4ea4\u4f5c\u4e1a
try:
    app.json.ensure_ascii = False
except Exception:
    pass


# =========================
# DeepSeek 配置
# =========================
DEEPSEEK_API_KEY = os.getenv("DEEPSEEK_API_KEY") or os.getenv("\ufeffDEEPSEEK_API_KEY", "")
DEEPSEEK_BASE_URL = os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com")
DEEPSEEK_MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-v4-pro")

_whisper_model = None
_whisper_lock = threading.Lock()
_whisper_transcribe_lock = threading.Lock()


CN_NUM = {
    "零": 0, "〇": 0,
    "一": 1, "二": 2, "两": 2, "三": 3, "四": 4,
    "五": 5, "六": 6, "七": 7, "八": 8, "九": 9,
    "十": 10
}


def chinese_number_to_int(s: str):
    s = s.strip()

    if s.isdigit():
        return int(s)

    if s in CN_NUM:
        return CN_NUM[s]

    # 十一、十二、二十、二十三
    if "十" in s:
        parts = s.split("十")

        if parts[0] == "":
            tens = 1
        else:
            tens = CN_NUM.get(parts[0], 0)

        if len(parts) > 1 and parts[1] != "":
            ones = CN_NUM.get(parts[1], 0)
        else:
            ones = 0

        return tens * 10 + ones

    return None


def clean_title(text: str):
    """
    从自然语言里提取任务标题。
    例如：
    五分钟后提醒我喝水 -> 喝水
    晚上八点提醒我开会 -> 开会
    明天晚上八点提醒我交作业 -> 交作业
    """
    t = text.strip()

    # 优先取“提醒我 / 叫我 / 让我”后面的内容
    for key in ["提醒我", "叫我", "让我"]:
        if key in t:
            t = t.split(key, 1)[1]
            break

    # 去掉常见时间词
    t = re.sub(r"(今天|明天|后天)", "", t)
    t = re.sub(r"(早上|上午|中午|下午|晚上|傍晚|凌晨)", "", t)
    t = re.sub(r"(\d+|[零〇一二两三四五六七八九十]+)\s*分钟后", "", t)
    t = re.sub(r"(\d+|[零〇一二两三四五六七八九十]+)\s*小时后", "", t)
    t = re.sub(r"\d{1,2}:\d{2}", "", t)
    t = re.sub(r"([零〇一二两三四五六七八九十\d]{1,3})\s*点\s*半?", "", t)
    t = re.sub(r"([零〇一二两三四五六七八九十\d]{1,3})\s*点\s*([零〇一二两三四五六七八九十\d]{1,3})?\s*分?", "", t)

    # 去掉提示词和标点
    for word in ["提醒", "请", "帮我", "记得"]:
        t = t.replace(word, "")

    t = t.strip(" ，,。.!！")

    if not t:
        t = "任务提醒"

    return t[:20]


def convert_to_24h(hour: int, period: str):
    """
    把 下午三点 / 晚上八点 转成 24 小时制。
    """
    if period in ["下午", "晚上", "傍晚"]:
        if hour < 12:
            hour += 12

    elif period == "中午":
        if hour < 11:
            hour += 12

    elif period in ["凌晨", "早上", "上午"]:
        if hour == 12:
            hour = 0

    return hour


def parse_date_offset(text: str):
    """
    返回日期偏移：
    今天 -> 0
    明天 -> 1
    后天 -> 2
    没写 -> None
    """
    if "后天" in text:
        return 2
    if "明天" in text:
        return 1
    if "今天" in text:
        return 0
    return None


def build_result(title: str, delay_seconds: int, source="local_rule"):
    if delay_seconds <= 0:
        delay_seconds = 1

    return {
        "success": True,
        "type": "reminder",
        "source": source,
        "title": title,
        "delay_seconds": int(delay_seconds),
        "remind_time": ""
    }


def build_result_by_target_time(title: str, target_time: datetime, source="local_rule"):
    now = datetime.now()
    delay_seconds = int((target_time - now).total_seconds())

    if delay_seconds <= 0:
        delay_seconds = 1

    return {
        "success": True,
        "type": "reminder",
        "source": source,
        "title": title,
        "delay_seconds": delay_seconds,
        "remind_time": "",
        "target_time": target_time.strftime("%Y-%m-%d %H:%M:%S")
    }


def should_use_llm_first(text: str):
    """
    这些表达比较复杂，规则容易误判，直接交给 DeepSeek：
    1. 周五 / 星期五
    2. 提前一小时
    3. 十点前 / 20:30前
    4. 下周 / 下个月
    """
    complex_patterns = [
        r"周[一二三四五六日天]",
        r"星期[一二三四五六日天]",
        r"提前",
        r"(点|:\d{2})\s*前",
        r"之前",
        r"下周",
        r"下个月"
    ]

    for p in complex_patterns:
        if re.search(p, text):
            return True

    return False


def local_rule_parse(text: str):
    """
    本地规则解析。
    适合简单、稳定的表达：
    五分钟后提醒我喝水
    两小时后提醒我开会
    20:30提醒我提交报告
    晚上八点提醒我开会
    明天晚上八点提醒我交作业
    """
    text = text.strip()
    now = datetime.now()

    if not text:
        return {
            "success": False,
            "source": "local_rule",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": "empty text"
        }

    # 比赛演示高频句式：我两分钟后有个会议，提前一分钟提醒我
    m = re.search(
        r"(\d+|[零〇一二两三四五六七八九十]+)\s*分钟后.*?提前\s*(\d+|[零〇一二两三四五六七八九十]+)\s*分钟",
        text
    )
    if m:
        event_minutes = chinese_number_to_int(m.group(1))
        advance_minutes = chinese_number_to_int(m.group(2))
        if event_minutes is not None and advance_minutes is not None:
            title_part = re.sub(r"^.*?分钟后", "", text)
            title_part = title_part.split("提前", 1)[0]
            title_part = re.sub(r"^(我)?有(一)?(个|场)?", "", title_part)
            title_part = title_part.strip(" ，,。.!！")
            title = title_part or clean_title(text)
            return build_result(title, (event_minutes - advance_minutes) * 60)

    # 复杂表达不要用规则硬解析，交给大模型兜底
    if should_use_llm_first(text):
        return {
            "success": False,
            "source": "local_rule",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": "need llm"
        }

    title = clean_title(text)

    # ======================
    # 1. 几分钟后
    # 例如：五分钟后提醒我喝水
    # ======================
    m = re.search(r"(\d+|[零〇一二两三四五六七八九十]+)\s*分钟后", text)
    if m:
        n = chinese_number_to_int(m.group(1))
        if n is not None:
            return build_result(title, n * 60)

    # ======================
    # 2. 几小时后
    # 例如：两小时后提醒我开会
    # ======================
    m = re.search(r"(\d+|[零〇一二两三四五六七八九十]+)\s*小时后", text)
    if m:
        n = chinese_number_to_int(m.group(1))
        if n is not None:
            return build_result(title, n * 3600)

    # ======================
    # 3. HH:MM
    # 例如：20:30提醒我提交报告
    # 明天20:30提醒我交作业
    # ======================
    m = re.search(r"(\d{1,2}):(\d{2})", text)
    if m:
        hour = int(m.group(1))
        minute = int(m.group(2))

        if 0 <= hour <= 23 and 0 <= minute <= 59:
            offset = parse_date_offset(text)

            if offset is None:
                target = now.replace(hour=hour, minute=minute, second=0, microsecond=0)

                # 没说今天/明天，如果这个时间已经过了，就自动设为明天
                if target <= now:
                    target += timedelta(days=1)
            else:
                target_date = now.date() + timedelta(days=offset)
                target = datetime(
                    target_date.year,
                    target_date.month,
                    target_date.day,
                    hour,
                    minute,
                    0
                )

            return build_result_by_target_time(title, target)

    # ======================
    # 4. 中文几点
    # 例如：
    # 晚上八点提醒我开会
    # 下午三点半提醒我提交报告
    # 明天晚上八点提醒我交作业
    # 后天上午九点提醒我拿快递
    # ======================
    m = re.search(
        r"(早上|上午|中午|下午|晚上|傍晚|凌晨)?\s*([零〇一二两三四五六七八九十\d]{1,3})\s*点\s*(半|[零〇一二两三四五六七八九十\d]{1,3}\s*分?)?",
        text
    )

    if m:
        period = m.group(1) or ""
        hour_raw = m.group(2)
        minute_raw = m.group(3) or ""

        hour = chinese_number_to_int(hour_raw)
        if hour is None:
            return {
                "success": False,
                "source": "local_rule",
                "title": "",
                "delay_seconds": -1,
                "remind_time": "",
                "error": "hour parse failed"
            }

        minute = 0

        if "半" in minute_raw:
            minute = 30
        else:
            minute_raw = minute_raw.replace("分", "").strip()
            if minute_raw:
                minute_value = chinese_number_to_int(minute_raw)
                if minute_value is not None:
                    minute = minute_value

        hour = convert_to_24h(hour, period)

        if not (0 <= hour <= 23 and 0 <= minute <= 59):
            return {
                "success": False,
                "source": "local_rule",
                "title": "",
                "delay_seconds": -1,
                "remind_time": "",
                "error": "invalid time"
            }

        offset = parse_date_offset(text)

        if offset is None:
            target = now.replace(hour=hour, minute=minute, second=0, microsecond=0)

            # 没说今天/明天，如果这个时间已经过了，就自动设为明天
            if target <= now:
                target += timedelta(days=1)
        else:
            target_date = now.date() + timedelta(days=offset)
            target = datetime(
                target_date.year,
                target_date.month,
                target_date.day,
                hour,
                minute,
                0
            )

        return build_result_by_target_time(title, target)

    return {
        "success": False,
        "source": "local_rule",
        "title": "",
        "delay_seconds": -1,
        "remind_time": "",
        "error": "本地规则无法解析"
    }


def extract_json_from_text(content: str):
    """
    防止模型偶尔输出 ```json ... ```，这里做一次清理。
    """
    content = content.strip()
    content = content.replace("```json", "").replace("```", "").strip()

    start = content.find("{")
    end = content.rfind("}")

    if start != -1 and end != -1 and end > start:
        content = content[start:end + 1]

    return json.loads(content)


def normalize_llm_result(raw_result: dict):
    """
    把 DeepSeek 返回结果统一转换成 ESP32 能处理的格式。
    ESP32 只需要：
    success
    title
    delay_seconds
    remind_time
    """
    if not raw_result.get("success", False):
        return {
            "success": False,
            "type": "error",
            "source": "deepseek",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": raw_result.get("error", "DeepSeek parse failed")
        }

    title = raw_result.get("title", "AI Task")
    delay_seconds = raw_result.get("delay_seconds", -1)
    remind_time = raw_result.get("remind_time", "")
    target_datetime = raw_result.get("target_datetime", "")

    # 如果 DeepSeek 直接给了 delay_seconds
    try:
        delay_seconds = int(delay_seconds)
    except Exception:
        delay_seconds = -1

    if delay_seconds > 0:
        return {
            "success": True,
            "type": "reminder",
            "source": "deepseek",
            "title": title,
            "delay_seconds": delay_seconds,
            "remind_time": ""
        }

    # 如果 DeepSeek 给了完整目标时间，Python 本地换算 delay_seconds
    if target_datetime:
        try:
            target_time = datetime.strptime(target_datetime, "%Y-%m-%d %H:%M:%S")
            return build_result_by_target_time(title, target_time, source="deepseek")
        except Exception as e:
            print("target_datetime parse failed:", e)

    # 如果只有 HH:MM，则给 ESP32 处理
    if remind_time:
        return {
            "success": True,
            "type": "reminder",
            "source": "deepseek",
            "title": title,
            "delay_seconds": -1,
            "remind_time": remind_time
        }

    return {
        "success": False,
        "type": "error",
        "source": "deepseek",
        "title": "",
        "delay_seconds": -1,
        "remind_time": "",
        "error": "DeepSeek result has no valid time"
    }


def deepseek_parse(text: str):
    """
    DeepSeek 大模型解析。
    只在本地规则失败时调用。
    """
    if not DEEPSEEK_API_KEY or DEEPSEEK_API_KEY.startswith("这里填"):
        print("DeepSeek API Key not configured.")
        return None

    now = datetime.now()
    now_str = now.strftime("%Y-%m-%d %H:%M:%S")
    weekday_names = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
    weekday_str = weekday_names[now.weekday()]

    system_prompt = """
你是一个提醒任务解析器。
你的任务是把用户输入的自然语言提醒，解析成严格 JSON。
不要输出解释，不要输出 Markdown，只输出 JSON。

你必须输出 JSON，格式如下：
{
  "success": true,
  "title": "任务标题",
  "delay_seconds": -1,
  "remind_time": "",
  "target_datetime": "YYYY-MM-DD HH:MM:SS"
}

字段要求：
1. title：任务标题，不要包含“提醒我”“提前一小时”“明天晚上八点”等时间词。
2. target_datetime：如果能确定完整日期和时间，必须输出 YYYY-MM-DD HH:MM:SS。
3. delay_seconds：如果是“几分钟后”“几小时后”，可以输出正整数秒数；否则输出 -1。
4. remind_time：如果只能确定 HH:MM，输出 HH:MM；否则输出空字符串。
5. 如果用户说“提前一小时提醒我”，必须把提醒时间提前计算好。
6. 如果用户说“周五晚上十点前交作业，提前一小时提醒我”，意思是截止时间是周五 22:00，提醒时间是周五 21:00。
7. 如果无法解析，输出：
{
  "success": false,
  "title": "",
  "delay_seconds": -1,
  "remind_time": "",
  "target_datetime": "",
  "error": "无法解析"
}
"""

    user_prompt = f"""
当前时间：{now_str}
今天是：{weekday_str}

用户输入：{text}

请输出严格 JSON。
"""

    url = DEEPSEEK_BASE_URL.rstrip("/") + "/chat/completions"

    headers = {
        "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        "Content-Type": "application/json"
    }

    payload = {
        "model": DEEPSEEK_MODEL,
        "messages": [
            {"role": "system", "content": system_prompt.strip()},
            {"role": "user", "content": user_prompt.strip()}
        ],
        "temperature": 0.1,
        "max_tokens": 2048,
        "response_format": {
            "type": "json_object"
        }
    }

    try:
        response = requests.post(
            url,
            headers=headers,
            json=payload,
            timeout=25
        )

        print("DeepSeek HTTP status:", response.status_code)

        if response.status_code != 200:
            print("DeepSeek error body:", response.text)
            return None

        data = response.json()
        content = data["choices"][0]["message"]["content"].strip()

        print("DeepSeek raw content:", content)

        raw_result = extract_json_from_text(content)
        result = normalize_llm_result(raw_result)

        return result

    except Exception as e:
        print("DeepSeek parse failed:", e)
        return None


def parse_control_command(text: str):
    t = text.strip().replace(" ", "")

    fan_words = [
        "\u98ce\u6247",      # 风扇
        "\u5c0f\u98ce\u6247",  # 小风扇
        "\u6563\u70ed\u98ce\u6247",  # 散热风扇
        "\u98ce\u58f0",      # 风声, common ASR confusion
        "\u98ce\u5c71",      # 风山
        "\u98ce\u5220",      # 风删
        "\u98ce\u6247\u513f",  # 风扇儿
        "\u98ce\u6247\u673a",  # 风扇机
        "fan",
    ]
    lamp_words = ["灯", "灯带", "台灯", "灯板", "照明", "光源"]
    on_words = ["打开", "开启", "启动", "开一下", "开开", "开"]
    off_words = [
        "关闭", "关掉", "关上", "关一下", "关了", "关掉吧", "关了吧",
        "停止", "停掉", "熄灯", "灭灯", "关", "關閉", "關掉", "關"
    ]

    def has_any(words):
        return any(w in t for w in words)

    is_on = has_any(on_words)
    is_off = has_any(off_words)
    has_fan = has_any(fan_words)
    has_lamp = has_any(lamp_words)
    has_all = "全部" in t or "所有" in t or "都" in t

    # Close commands are checked first because ASR may produce short phrases like "关灯".
    if has_all and is_off and (has_fan or has_lamp):
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "all_off",
            "title": "全部关闭",
            "delay_seconds": -1,
            "remind_time": ""
        }

    if has_all and is_on and (has_fan or has_lamp):
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "all_on",
            "title": "全部打开",
            "delay_seconds": -1,
            "remind_time": ""
        }

    if has_fan and is_off:
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "fan_off",
            "title": "关闭风扇",
            "delay_seconds": -1,
            "remind_time": ""
        }

    if has_lamp and is_off:
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "lamp_off",
            "title": "关闭灯",
            "delay_seconds": -1,
            "remind_time": ""
        }

    if has_fan and is_on:
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "fan_on",
            "title": "打开风扇",
            "delay_seconds": -1,
            "remind_time": ""
        }

    if has_lamp and is_on:
        return {
            "success": True,
            "type": "control",
            "source": "local_control_rule",
            "action": "lamp_on",
            "title": "打开灯",
            "delay_seconds": -1,
            "remind_time": ""
        }

    return None


def parse_reminder_text(text: str):
    control_result = parse_control_command(text)
    if control_result is not None:
        return control_result

    result = local_rule_parse(text)
    if result.get("success", False):
        return result

    print("Local rule failed:", result.get("error", "unknown"))
    print("Trying DeepSeek...")

    llm_result = deepseek_parse(text)
    if llm_result is not None and llm_result.get("success", False):
        return llm_result

    return {
        "success": False,
        "type": "error",
        "source": "deepseek" if llm_result is not None else "local_rule",
        "title": "",
        "delay_seconds": -1,
        "remind_time": "",
        "error": "规则和大模型都无法解析"
    }


def get_whisper_model():
    global _whisper_model
    if _whisper_model is not None:
        return _whisper_model
    with _whisper_lock:
        if _whisper_model is None:
            from faster_whisper import WhisperModel

            model_name = os.getenv("WHISPER_MODEL", "tiny")
            device = os.getenv("ASR_DEVICE", "cpu")
            compute_type = os.getenv("ASR_COMPUTE_TYPE", "int8")

            print("Loading Whisper model:", model_name)
            print("ASR device:", device)
            print("ASR compute_type:", compute_type)

            _whisper_model = WhisperModel(
                model_name,
                device=device,
                compute_type=compute_type,
            )
    return _whisper_model


def transcribe_wav(path: Path) -> str:
    print("Transcribing wav:", path)
    print("WAV size:", path.stat().st_size)
    model = get_whisper_model()
    with _whisper_transcribe_lock:
        segments, _ = model.transcribe(
            str(path),
            language="zh",
            vad_filter=False,
            initial_prompt="关键词：分钟后、小时后、提醒我、开会、喝水。请准确输出简体中文。",
        )
        text = "".join(segment.text for segment in segments).strip()
    print("Whisper text:", text)
    return text


def inspect_wav_audio(audio: bytes):
    """Return simple PCM16 diagnostics used to reject broken I2S captures."""
    try:
        with wave.open(io.BytesIO(audio), "rb") as wav:
            if wav.getsampwidth() != 2:
                return None
            pcm = wav.readframes(wav.getnframes())
    except (EOFError, wave.Error):
        return None

    sample_count = len(pcm) // 2
    if sample_count == 0:
        return None

    zero_count = 0
    neg_one_count = 0
    sum_squares = 0
    for offset in range(0, sample_count * 2, 2):
        sample = int.from_bytes(pcm[offset:offset + 2], "little", signed=True)
        zero_count += sample == 0
        neg_one_count += sample == -1
        sum_squares += sample * sample

    return {
        "samples": sample_count,
        "zero": zero_count,
        "neg_one": neg_one_count,
        "zero_neg_one_ratio": (zero_count + neg_one_count) / sample_count,
        "rms": round(math.sqrt(sum_squares / sample_count), 1),
    }


@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "ok": True,
        "message": "ESP32 AI parse server is running",
        "deepseek_configured": bool(DEEPSEEK_API_KEY and not DEEPSEEK_API_KEY.startswith("这里填")),
        "model": DEEPSEEK_MODEL,
        "asr_loaded": _whisper_model is not None
    })


@app.route("/parse_text", methods=["POST"])
def parse_text_api():
    data = request.get_json(silent=True) or {}
    text = (data.get("text") or request.form.get("text") or "").strip()

    print("=" * 60)
    print("Received:", text)

    if not text:
        return jsonify({
            "success": False,
            "source": "none",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": "empty text"
        })

    result = parse_reminder_text(text)
    print("Result:", result)
    return jsonify(result)


@app.route("/voice", methods=["POST"])
def voice_api():
    if request.files:
        upload = next(iter(request.files.values()))
        audio = upload.read()
    else:
        audio = request.get_data() or b""

    if not audio:
        return jsonify({
            "success": False,
            "source": "asr",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": "没有收到 WAV 音频"
        }), 400

    LAST_VOICE_PATH.write_bytes(audio)

    audio_stats = inspect_wav_audio(audio)
    print("WAV diagnostics:", audio_stats)
    if audio_stats and audio_stats["zero_neg_one_ratio"] >= 0.90:
        ratio_percent = audio_stats["zero_neg_one_ratio"] * 100
        return jsonify({
            "success": False,
            "source": "audio_debug",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": (
                f"I2S 音频异常：0/-1 占 {ratio_percent:.1f}%，"
                f"RMS={audio_stats['rms']}。请切换 LEFT/RIGHT 并检查 SD/SCK/WS、L/R 和焊点。"
            )
        }), 422

    if audio_stats and audio_stats["rms"] < 80:
        return jsonify({
            "success": False,
            "source": "audio_debug",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": (
                f"音频采样正常，但音量太低：RMS={audio_stats['rms']}。"
                "请靠近麦克风说话，并使用 MIC_SAMPLE_SHIFT=14。"
            )
        }), 422

    path = None
    try:
        with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as temp:
            temp.write(audio)
            path = Path(temp.name)

        asr_text = transcribe_wav(path)
        print("ASR text:", asr_text)
        if not asr_text:
            return jsonify({
                "success": False,
                "source": "asr",
                "title": "",
                "delay_seconds": -1,
                "remind_time": "",
                "asr_text": "",
                "recognized_text": "",
                "error": "没有识别到有效语音"
            }), 422

        result = parse_reminder_text(asr_text)
        source = result.get("source", "unknown")
        result["source"] = f"voice_whisper_{source}"
        result["asr_text"] = asr_text
        result["recognized_text"] = asr_text
        return jsonify(result)
    except ModuleNotFoundError:
        return jsonify({
            "success": False,
            "source": "asr",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": "服务器未安装 faster-whisper"
        }), 503
    except Exception as e:
        return jsonify({
            "success": False,
            "source": "asr",
            "title": "",
            "delay_seconds": -1,
            "remind_time": "",
            "error": f"语音识别失败：{e}"
        }), 500
    finally:
        if path:
            path.unlink(missing_ok=True)


@app.route("/last_voice.wav", methods=["GET"])
def last_voice_wav():
    if not LAST_VOICE_PATH.exists():
        return jsonify({
            "success": False,
            "error": "还没有收到语音文件"
        }), 404

    return send_file(
        LAST_VOICE_PATH,
        mimetype="audio/wav",
        as_attachment=False,
        download_name="last_voice.wav"
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False, use_reloader=False)
