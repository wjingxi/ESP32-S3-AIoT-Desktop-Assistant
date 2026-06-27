@echo off
cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
  python -m venv .venv
)

call ".venv\Scripts\activate.bat"
python -m pip install -r requirements.txt
set WHISPER_MODEL=tiny
set ASR_DEVICE=cpu
set ASR_COMPUTE_TYPE=int8
python app.py
pause
