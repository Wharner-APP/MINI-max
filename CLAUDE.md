# CLAUDE.md

Этот файл содержит рекомендации для Claude Code при работе с кодом в этом репозитории.

## Обзор проекта

MINI max — кросс-платформенный мессенджер с Qt-клиентом на C++ и Python-сервером. Особенности:
- Сквозное шифрование для приватных чатов и секретных чатов (X3DH + Double Ratchet)
- Stars — криптовалютная платёжная система
- hCaptcha для защиты от ботов
- Auto-update с Ed25519-подписью

## Архитектура

### Клиент (`client/`)
- Qt 6/5 Widgets приложение
- `core/` — бизнес-логика, API-клиент, модели, шифрование
- `ui/` — представления, виджеты, темы, иконки

### Общие компоненты (`common/`)
- `mmcommon` — пути, сетевые эндпоинты, манифесты

### Сервер (`minimaxServer/`)
- Python HTTP-сервер (stdlib only) + SQLite
- Скрипты для обновлений, капчи, платежей, ботов

### Апдейтер (`updater/`)
- Минимальное Qt-приложение для загрузки и проверки обновлений

## Сборка

```bash
# Требования: CMake ≥3.21, Qt 6 (Widgets, Network, Svg, Multimedia), C++17/20
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build

# Запуск клиента
build/client/minimax

# Режим разработки без сервера
build/client/minimax --demo
build/client/minimax --open settings|premium|stars|gifts|profile|drawer|archive|emoji|business|payment

# Сервер-заглушка для разработки
python3 tools/mock_server.py --port 8080
```

Опции CMake:
- `-DMINIMAX_QT_MAJOR=5|6` — версия Qt
- `-DMINIMAX_PLATFORM_ID=<id>` — платформа (minimaxWinX64, minimaxLinuxX86, minimaxMacX64)
- `-DMINIMAX_BUILD_TESTS=OFF` — отключить тесты

## Ключевые файлы

| Путь | Назначение |
|------|------------|
| `client/src/main.cpp` | Точка входа, парсинг CLI |
| `client/src/core/api.h/cpp` | HTTP-клиент |
| `client/src/core/engine.h/cpp` | Long-poll sync, обмен сообщениями |
| `client/src/core/crypto.h/cpp` | Обёртки libsodium |
| `client/src/ui/theme.h/cpp` | Менеджер тем (тёмная/светлая) |
| `client/src/core/chats_model.h/cpp` | Модель списка чатов |
| `client/src/core/messages_model.h/cpp` | Модель списка сообщений |
| `minimaxServer/api.py` | HTTP API эндпоинты |
| `minimaxServer/db.py` | SQLite схема |
| `minimaxServer/ext_msgs.py` | Логика сообщений |
| `configs/client.toml` | Настройки клиента |
| `configs/server.toml` | Настройки сервера |
| `configs/connect.ip` | Адрес сервера (генерируется при сборке) |
| `configs/update_public.key` | Ed25519 публичный ключ |

## API

- Транспорт: HTTP/HTTPS, JSON UTF-8
- Auth: `Authorization: Bearer <token>`
- Ошибка: HTTP 4xx/5xx + `{"error":"code","message":"текст"}`
- `/api/sync` — long-poll для реального времени (до 15 сек)

## Шифрование

| Тип чата | Шифрование | Сервер видит |
|----------|-----------|--------------|
| Приватный DM | X25519 + XChaCha20-Poly1305 | шифртекст, отправитель, время |
| Секретный чат | X3DH + Double Ratchet | шифрtekст, TTL |
| Публичная группа/канал | Нет | всё |
| Чат с ботом | Нет | всё |

## Поддерживаемые платформы

- Windows x64 (официально)
- Linux i386 (официально)
- macOS Intel x64 (официально)

## Примечания для разработки

- `CaptchaAPI.cfg` читается при сборке и вшивается в бинарник — **не попадает в архив**
- hCaptcha secret должен быть только на сервере
- `configs/connect.ip` генерируется при сборке и **никогда не перезаписывается**
- Обновления: `update.exe` проверяет Ed25519-подпись и SHA-256 файлов
- Релизы: GitHub Actions → Actions → Artifacts или Releases (для тегов `v*`)
