# Контракт API клиент ↔ сервер

Транспорт: HTTP или HTTPS (адрес — `configs/connect.ip`), JSON в UTF-8. После входа клиент шлёт `Authorization: Bearer <token>`.
Ошибка: HTTP 4xx/5xx и тело `{"error":"код","message":"текст для пользователя"}`.
Референс-реализация для разработки: `tools/mock_server.py`.

| Метод | Путь | Тело / ответ |
|---|---|---|
| GET | `/api/ping` | `{"ok":true,"server":"имя"}` — по нему клиент проверяет связь (каждые 5 с) |
| GET | `/api/get_captcha` | `{"captcha_id","background","piece","piece_y","width","height"}`; `background`/`piece` — URL или `data:image/png;base64,…`. Заголовок `X-Captcha-Key` содержит ключ из `CaptchaAPI.cfg`, если он был задан при сборке |
| POST | `/api/device/check` | `{"fingerprint","macs":[]}` → `{"registered":true/false}` |
| POST | `/api/register` | `{"login","display_name","auth_key","captcha_id","captcha_x","device":{fingerprint,macs,ips,cpu,os,hostname,machine_id,ram_mb}}` → `{"token","user":{id,login,display_name,bio}}`. Ошибка капчи: `{"error":"captcha_failed"}` |
| POST | `/api/login` | `{"login","auth_key"}` → как у регистрации |
| POST | `/api/profile/update` | `{"bio"}` |
| POST | `/api/sessions/terminate_others` | завершить остальные сеансы |
| POST | `/api/chats/create` | `{"kind":"group"/"channel","title"}` |
| POST | `/api/payments/create` | (если `[payments] provider="server"`) `{"kind","item","amount_minor","currency","quantity","recipient"}` → `{"payment_id","checkout_url"}` |
| GET | `/update/<platform>/manifest.json`, `manifest.sig`, `files/<путь>` | см. `docs/update.md` |

## Пароль не уходит на сервер
Клиент считает `auth_key` = KDF(Argon2id(пароль, соль=hash(логин))) и отправляет только его (hex, 32 байта).
Сервер должен хранить `hash(auth_key)` (например, Argon2id/bcrypt от `auth_key`), а не сам ключ. Второй ключ (`local_key`), которым шифруются
переписки на ПК, из пароля выводится отдельно и на сервер не передаётся.

## Капча
`captcha_x` — координата X левого края кусочка в пикселях фонового изображения. Сервер хранит правильный ответ по `captcha_id`,
сверяет с допуском и делает ID одноразовым.

## Защита от повторной регистрации
Клиент проверяет файлы-метки на ПК и `POST /api/device/check`. Это защита «для честных»: окончательное решение всегда принимает сервер
(по `fingerprint`, MAC и IP из `/api/register`) — клиентскую проверку можно обойти.
