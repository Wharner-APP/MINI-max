#include "core/demo_data.h"

#include "core/chats_model.h"

namespace DemoData {
static Message msg(const QString &text, bool out, const QString &sender, int minsAgo, int status = 2) {
    Message m;
    m.text = text; m.outgoing = out; m.sender = sender; m.senderColor = qHash(sender) % 7;
    m.time = QDateTime::currentDateTime().addSecs(-60 * minsAgo);
    m.status = status;
    return m;
}

void populate(ChatsModel *m) {
    auto add = [&](const QString &title, ChatKind k, int unread, bool pinned, bool archived, bool verified, int members, const QString &user, const QString &about, const QVector<Message> &msgs) {
        Chat c;
        c.title = title; c.kind = k; c.unread = 0; c.pinned = pinned; c.archived = archived; c.verified = verified;
        c.members = members; c.username = user; c.about = about;
        const int row = m->addChat(c);
        for (const Message &x : msgs) m->addMessage(row, x);
        m->chat(row)->unread = unread;
        m->touch(row);
        return row;
    };
    add("Избранное", ChatKind::Saved, 0, true, false, false, 0, {}, {}, {msg("Заметка для себя", true, {}, 600, 1)});
    add("Анна Морозова", ChatKind::Private, 2, false, false, false, 0, "anna_m", "Дизайнер",
        {msg("Привет! Посмотришь макет?", false, "Анна", 55), msg("Да, сейчас открою", true, {}, 50),
         msg("Ну смотри, раскрытие настроек можно и нужно выключить, и всё будет по-другому.", false, "Анна", 14),
         msg("окей", true, {}, 12)});
    Message reply = msg("Тогда давай завтра в 12:00", true, {}, 8, 2);
    reply.replyTo = "Когда тебе удобно созвониться?"; reply.replyAuthor = "Анна Морозова";
    m->addMessage(1, reply);
    Message withReact = msg("Релиз клиента запланирован на понедельник 🎉", false, "Админ", 120);
    withReact.reactions = {{"👍", 12}, {"🔥", 4}};
    add("Команда проекта", ChatKind::Group, 5, true, false, false, 20, "team_chat", "Рабочий чат команды",
        {msg("Всем привет!", false, "Игорь", 300), msg("Кто смотрел сборку?", false, "Мария", 280), withReact,
         msg("Проверю после обеда", true, {}, 100), msg("Сборка для Windows готова", false, "Игорь", 20)});
    m->chat(2)->pinnedText = "Правила чата: уважайте друг друга";
    Message post = msg("В скором времени появятся рамки для фото профиля для подписчиков Premium.", false, "Новости MINI max", 40);
    post.views = 118200; post.reactions = {{"⭐", 2}, {"🤡", 2413}, {"❤", 110}};
    add("Новости MINI max", ChatKind::Channel, 12, false, false, true, 1255534, "mm_news", "Официальный канал: новости, обновления и розыгрыши", {post});
    add("Игровой канал", ChatKind::Channel, 365, false, false, true, 90210, "games_ch", {}, {msg("Вышло большое обновление", false, "Игровой канал", 30)});
    add("Погода", ChatKind::Bot, 0, false, false, false, 0, "weather_bot", "Бот прогноза погоды", {msg("Отправьте название города", false, "Погода", 900)});
    add("Старый чат", ChatKind::Group, 7, false, true, false, 12, {}, {}, {msg("давно не писали", false, "Олег", 4000)});
    add("Архивный канал", ChatKind::Channel, 1377, false, true, true, 5000, {}, {}, {msg("Интересные отсылки", false, "Архивный канал", 3000)});
}
}  // namespace DemoData
