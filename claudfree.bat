@echo off
setlocal

set "ANTHROPIC_AUTH_TOKEN=sk-fbcc0a9975447189-a7a320-143c2875"
set "ANTHROPIC_BASE_URL=http://localhost:20128/v1"

REM Отключаем experimental betas
set "CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS=1"

REM Модель — DeepSeek
set "ANTHROPIC_MODEL=kr/qwen3-coder-next"

REM Реальное окно модели 
set "CLAUDE_CODE_MAX_CONTEXT_TOKENS=128000000000"

REM Отключаем проверку каталога моделей
set "CLAUDE_CODE_DISABLE_UNKNOWN_MODEL_WINDOW_ENFORCEMENT=1"

echo Starting J. A. R. V. I. S. AI on Claude Code... Please wait
claude 

endlocal