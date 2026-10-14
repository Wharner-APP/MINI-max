# MINI max server — API для клиента

Транспорт: HTTP(S), JSON UTF-8. После входа: `Authorization: Bearer <token>`. Ошибка: HTTP 4xx/5xx и `{"error":"код","message":"текст"}`.
Файлы: `/media/...` (бинарные). Бот-API: `/bot<token>/<method>` (как у Telegram).

## Как устроена синхронизация
`GET /api/sync?since=<last>&ev=<last_event>&known=<id,id>&wait=<сек до 20>` — long-poll. Ответ: `messages` (новые, с реакциями/медиа/опросом), `chats` (чаты, которых нет в `known`, с историей, `unread`, `peer_read`, `prefs`, `draft`, `init` для секретных), `events` (правки, удаления, реакции, прочтения, закрепы, опросы, запросы на вступление, смена ролей), `ephemeral` (typing, входящие звонки и сигналы WebRTC, подарки), `last`, `last_event`, `me`.
Первый запрос без `ev` возвращает только текущий курсор событий. Секретные чаты доставляются только на устройство с `device_id` (передаётся при входе/регистрации вместе с `device_name`, `platform`, `app_version`).

## Идентификаторы
`login` — неизменяемый логин для входа (из него клиент выводит ключи); `username` — публичное имя (@), меняется через `/api/profile/update`; везде, где API принимает `login`, подходит `username` или старый `login`. Сообщения содержат `sender` (username) и `sender_id` (используйте id).

## Шифрование и политика полей `enc`
Личные чаты, приватные группы/каналы, секретные чаты: `enc:1` обязателен (сервер отвергает открытый текст). Публичные чаты и чаты с ботами: `enc:0`. Подробнее — `docs/E2E.md`.

## JSON-маршруты

| Метод | Путь | Авторизация | Описание |
|---|---|---|---|
| POST | `/api/2fa/confirm` | Bearer |  |
| POST | `/api/2fa/disable` | Bearer |  |
| POST | `/api/2fa/setup` | Bearer |  |
| POST | `/api/account/change_password` | Bearer |  |
| POST | `/api/account/delete` | Bearer |  |
| GET | `/api/blocks` | Bearer |  |
| POST | `/api/blocks/add` | Bearer |  |
| POST | `/api/blocks/remove` | Bearer |  |
| POST | `/api/bots/callback` | Bearer |  |
| POST | `/api/bots/create` | Bearer |  |
| POST | `/api/bots/delete` | Bearer |  |
| GET | `/api/bots/mine` | Bearer |  |
| POST | `/api/bots/update` | Bearer |  |
| POST | `/api/calls/accept` | Bearer |  |
| POST | `/api/calls/decline` | Bearer |  |
| POST | `/api/calls/end` | Bearer |  |
| GET | `/api/calls/history` | Bearer |  |
| GET | `/api/calls/ice` | Bearer |  |
| POST | `/api/calls/join` | Bearer |  |
| POST | `/api/calls/rate` | Bearer |  |
| POST | `/api/calls/signal` | Bearer |  |
| POST | `/api/calls/start` | Bearer |  |
| GET | `/api/calls/state` | Bearer |  |
| GET | `/api/captcha/status` | нет |  |
| POST | `/api/captcha/verify` | нет |  |
| POST | `/api/chats/add_member` | Bearer |  |
| POST | `/api/chats/approve` | Bearer |  |
| POST | `/api/chats/create` | Bearer |  |
| POST | `/api/chats/delete` | Bearer |  |
| POST | `/api/chats/invite/new` | Bearer |  |
| POST | `/api/chats/join` | Bearer |  |
| POST | `/api/chats/join_invite` | Bearer |  |
| GET | `/api/chats/join_requests` | Bearer |  |
| POST | `/api/chats/leave` | Bearer |  |
| GET | `/api/chats/members` | Bearer |  |
| GET | `/api/chats/prefs` | Bearer |  |
| POST | `/api/chats/prefs` | Bearer |  |
| POST | `/api/chats/remove_member` | Bearer |  |
| POST | `/api/chats/set_role` | Bearer |  |
| POST | `/api/chats/update` | Bearer |  |
| GET | `/api/contacts` | Bearer |  |
| POST | `/api/contacts/add` | Bearer |  |
| POST | `/api/contacts/remove` | Bearer |  |
| POST | `/api/device/check` | нет |  |
| POST | `/api/dm/open` | Bearer |  |
| GET | `/api/drafts` | Bearer |  |
| POST | `/api/drafts` | Bearer |  |
| GET | `/api/e2e/bundle` | Bearer |  |
| GET | `/api/e2e/devices` | Bearer |  |
| POST | `/api/e2e/prekeys` | Bearer |  |
| GET | `/api/e2e/prekeys/count` | Bearer |  |
| GET | `/api/get_captcha` | нет |  |
| POST | `/api/gifs/save` | Bearer |  |
| GET | `/api/gifs/saved` | Bearer |  |
| GET | `/api/gifs/search` | Bearer |  |
| POST | `/api/gifs/unsave` | Bearer |  |
| GET | `/api/gifts/catalog` | Bearer |  |
| POST | `/api/gifts/convert` | Bearer |  |
| POST | `/api/gifts/manage` | Bearer |  |
| GET | `/api/gifts/mine` | Bearer |  |
| POST | `/api/gifts/upgrade` | Bearer |  |
| POST | `/api/inline/chosen` | Bearer |  |
| POST | `/api/inline/query` | Bearer |  |
| POST | `/api/invoices/pay` | Bearer |  |
| GET | `/api/keys/backup` | Bearer |  |
| POST | `/api/keys/backup` | Bearer |  |
| GET | `/api/keys/get` | Bearer |  |
| POST | `/api/keys/publish` | Bearer |  |
| POST | `/api/login` | нет |  |
| POST | `/api/logout` | Bearer |  |
| GET | `/api/me` | Bearer |  |
| POST | `/api/media/delete` | Bearer |  |
| POST | `/api/media/finish` | Bearer |  |
| GET | `/api/media/info` | Bearer |  |
| POST | `/api/media/init` | Bearer |  |
| POST | `/api/messages/delete` | Bearer |  |
| POST | `/api/messages/edit` | Bearer |  |
| GET | `/api/messages/get` | Bearer |  |
| GET | `/api/messages/history` | Bearer |  |
| POST | `/api/messages/pin` | Bearer |  |
| POST | `/api/messages/react` | Bearer |  |
| POST | `/api/messages/read` | Bearer |  |
| GET | `/api/messages/scheduled` | Bearer |  |
| POST | `/api/messages/scheduled/delete` | Bearer |  |
| POST | `/api/messages/scheduled/send_now` | Bearer |  |
| GET | `/api/messages/search` | Bearer |  |
| POST | `/api/messages/send` | Bearer |  |
| POST | `/api/messages/unlock` | Bearer |  |
| POST | `/api/payments/create` | Bearer |  |
| GET | `/api/ping` | нет |  |
| POST | `/api/polls/close` | Bearer |  |
| POST | `/api/polls/vote` | Bearer |  |
| GET | `/api/polls/voters` | Bearer |  |
| POST | `/api/profile/avatar` | Bearer |  |
| GET | `/api/profile/get` | Bearer |  |
| POST | `/api/profile/update` | Bearer |  |
| POST | `/api/register` | нет |  |
| POST | `/api/report` | Bearer |  |
| GET | `/api/resolve` | Bearer |  |
| GET | `/api/search` | Bearer |  |
| POST | `/api/secret/ack` | Bearer | The receiving device confirms it stored the messages -> the server deletes them (no server-side history for secret chats). |
| POST | `/api/secret/create` | Bearer |  |
| POST | `/api/secret/delete` | Bearer |  |
| GET | `/api/secret/pending` | Bearer | Messages for this device that were not acknowledged yet (even if the sync cursor moved past them). |
| GET | `/api/sessions` | Bearer |  |
| POST | `/api/sessions/terminate` | Bearer |  |
| POST | `/api/sessions/terminate_others` | Bearer |  |
| GET | `/api/settings` | Bearer |  |
| POST | `/api/settings` | Bearer |  |
| GET | `/api/stars/stats` | Bearer |  |
| POST | `/api/stars/tip` | Bearer |  |
| GET | `/api/stars/transactions` | Bearer |  |
| GET | `/api/status` | Bearer | Online / last-seen status of users (privacy rules applied): ?ids=1,2,3 |
| POST | `/api/stickers/add` | Bearer |  |
| POST | `/api/stickers/create_pack` | Bearer |  |
| POST | `/api/stickers/delete_pack` | Bearer |  |
| POST | `/api/stickers/install` | Bearer |  |
| GET | `/api/stickers/mine` | Bearer |  |
| GET | `/api/stickers/pack` | Bearer |  |
| POST | `/api/stickers/remove` | Bearer |  |
| GET | `/api/stickers/search` | Bearer |  |
| POST | `/api/stickers/uninstall` | Bearer |  |
| GET | `/api/sync` | Bearer |  |
| POST | `/api/typing` | Bearer |  |
| POST | `/api/webapp/init` | Bearer |  |

## Потоковые маршруты

| Метод | Путь | Описание |
|---|---|---|
| POST | `/media/upload?kind=&name=&mime=&w=&h=&dur=&enc=&meta=` | тело — файл целиком (до 2 ГБ Free / 4 ГБ Premium); kind: photo, video, file, voice, round, audio, sticker, gif, avatar, thumb |
| PUT | `/media/chunk?id=&offset=` | докачка (после `/api/media/init`, закончить `/api/media/finish`); при несовпадении смещения — 409 и верное `offset` |
| GET | `/media/<id>` (+`?token=`) | скачивание, поддерживает `Range` (стриминг видео) |
| GET/POST | `/bot<token>/<method>` | Bot API |
| GET | `/file/bot<token>/<file_id>` | файлы для ботов |
| GET | `/update/<platform>/...` | автообновление клиента |

## Методы Bot API

`answerCallbackQuery`, `answerInlineQuery`, `answerPreCheckoutQuery`, `answerWebAppQuery`, `banChatMember`, `deleteMessage`, `deleteMyCommands`, `deleteWebhook`, `editMessageCaption`, `editMessageReplyMarkup`, `editMessageText`, `forwardMessage`, `getChat`, `getChatAdministrators`, `getChatMember`, `getChatMemberCount`, `getChatMenuButton`, `getFile`, `getMe`, `getMyCommands`, `getUpdates`, `getWebhookInfo`, `leaveChat`, `pinChatMessage`, `refundStarPayment`, `sendAnimation`, `sendAudio`, `sendChatAction`, `sendDocument`, `sendInvoice`, `sendMessage`, `sendPhoto`, `sendPoll`, `sendSticker`, `sendVideo`, `sendVideoNote`, `sendVoice`, `setChatMenuButton`, `setMessageReaction`, `setMyCommands`, `setMyDescription`, `setMyName`, `setWebhook`, `stopPoll`, `unbanChatMember`, `unpinChatMessage`, `uploadFile` (raw).

Типы апдейтов: `message`, `edited_message`, `channel_post`, `callback_query`, `inline_query`, `chosen_inline_result`, `pre_checkout_query`, `my_chat_member`. Сообщения с оплатой содержат `successful_payment`. Мини-приложения: `POST /api/webapp/init` → `init_data` (подпись как у Telegram WebApp, проверка: `ext_bots.validate_init_data`).

## Лимиты Free / Premium (`[free_limits]` / `[premium_limits]` в server.toml)

| Параметр | Free | Premium |
|---|---|---|
| channels | 500 | 1000 |
| folders | 10 | 30 |
| pinned_chats | 5 | 10 |
| public_links | 10 | 20 |
| upload_mb | 2048 | 4096 |
| reactions | 1 | 3 |
| bio | 70 | 140 |
| caption | 1024 | 4096 |

Только Premium: эмодзи-статус, цвет имени, ограничение входящих сообщений/голосовых, платные входящие сообщения, премиум-стикеры, несколько реакций.

## Приватность (`POST /api/settings {kind:"privacy", data:{...}}`)

Ключи: `last_seen, photos, bio, birthday, forwards, calls, voice, messages, gifts, invites, read_receipts` — значение `all|contacts|nobody` или `{rule, allow:[user_id], deny:[user_id]}`; плюс `paid_messages_stars`, `delete_after_months`, `session_ttl_days`. Остальные разделы настроек (`notifications, chat, folders, ui, battery, advanced, general`) — произвольный JSON ≤ 64 КБ, синхронизируется между устройствами.
