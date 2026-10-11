# MINI max — исправленная версия

Основа: исходный проект MINI-max из репозитория Wharner-APP/MINI-max; предоставленный `MINI-max-client-full.zip` использован как локальная копия исходников и сверка ключевых файлов проведена с текущим `main`.

## Что исправлено
- Исправлен compile error в `client/src/ui/chat_view.cpp`: добавлен `MessagesModel::message(int)`.
- Капча переведена с самодельного slider-puzzle клиентского UI на hCaptcha: сервер создаёт одноразовый challenge, клиент открывает его в браузере и автоматически опрашивает `/api/captcha/status`.
- Серверная регистрация принимает только подтверждённый `captcha_id`; `captcha_x` оставлен для обратной совместимости и для hCaptcha равен 0.
- Секрет hCaptcha не вшит в клиент и не попадает в GitHub Actions archive.

## Проверки
- Все 33 прямых client HTTP API-вызова сопоставлены с существующими server routes — совпадают.
- Сервер: `py_compile` — PASS.
- Сервер: 3 hCaptcha unit-теста — PASS.
- Сервер: HTTP smoke test `/api/ping`, `/api/get_captcha`, `/captcha/<id>`, `/api/captcha/status` и mocked `/api/captcha/verify` — PASS.
- Полная Qt-сборка локально здесь не запускается, потому что в окружении нет Qt 5/6; предыдущие GitHub Actions показывали одну компиляционную ошибку `MessagesModel::message`, которая исправлена.
