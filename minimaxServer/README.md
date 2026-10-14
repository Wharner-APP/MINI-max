# MINI max server (Windows, без GUI)

1. Python 3.11+ (галочка «Add to PATH»). Запуск: `run_server.cmd` — сам создаст venv, поставит `pillow` и `pynacl`, откроет консоль `mm>` (`help`).
2. `configs\server.toml`: порт, капча (`hcaptcha_secret` или переменная `MM_HCAPTCHA_SECRET`), лимиты Free/Premium, звонки (STUN/TURN), боты, GIF (GIPHY), комиссия Stars.
3. Туннель: `start_tunnel.cmd` (bore) → порт вписать в `configs\connect.ip` клиента.
4. Данные: `data\` (БД SQLite, `media\`, `server.log`, `updates\`).
5. Тесты: `python -m unittest discover -s tests` (8 сквозных сценариев: профиль/приватность, сообщения, медиа, каналы, звонки, секретные чаты, боты, Stars).
6. Документация: `docs\API.md` (все маршруты), `docs\E2E.md` (шифрование, X3DH + Double Ratchet), эталон шифрования — `tools\ratchet_ref.py`.

Звонки: сервер выполняет только сигналинг (SDP/ICE) и выдаёт ICE-серверы. Для прохода через NAT нужен свой TURN (coturn, режим `use-auth-secret`): впишите `turn_urls` и `turn_secret`.


## Auto-update

The server serves signed client updates from `data/updates/<platform>/`. Keep `keys/update_private.key` private. The corresponding public key is embedded in the client.
