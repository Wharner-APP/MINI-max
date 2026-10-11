#include "core/messages_model.h"

#include <QLocale>
#include <QVariantList>

#include "core/chats_model.h"

MessagesModel::MessagesModel(ChatsModel *chats, QObject *parent) : QAbstractListModel(parent), m_chats(chats) {
    connect(chats, &ChatsModel::messageAppended, this, [this](int row) {
        if (row != m_row) return;
        const int n = m_chats->chat(row)->messages.size();
        beginInsertRows({}, m_count, n - 1);
        m_count = n;
        endInsertRows();
    });
    connect(chats, &ChatsModel::messagesReset, this, [this](int row) { if (row == m_row) setChatRow(row); });
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const {
    const Chat *c = m_chats->chat(m_row);
    if (!c || index.row() < 0 || index.row() >= c->messages.size()) return {};
    const Message &m = c->messages[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return m.text;
    case OutgoingRole: return m.outgoing;
    case TimeRole: return m.time.toString("HH:mm");
    case SenderRole: return m.sender;
    case SenderColorRole: return m.senderColor;
    case ReplyRole: return m.replyTo;
    case ReplyAuthorRole: return m.replyAuthor;
    case EditedRole: return m.edited;
    case StatusRole: return m.status;
    case ViewsRole: return m.views;
    case ShowSenderRole: {
        if (c->kind != ChatKind::Group || m.outgoing || m.sender.isEmpty()) return false;
        if (index.row() == 0) return true;
        const Message &p = c->messages[index.row() - 1];
        return p.sender != m.sender || p.outgoing || p.time.date() != m.time.date();
    }
    case ReactionsRole: {
        QVariantList l;
        for (const Reaction &r : m.reactions) l << QVariant::fromValue(QStringList{r.emoji, QString::number(r.count)});
        return l;
    }
    case DateHeaderRole: {
        if (index.row() > 0 && c->messages[index.row() - 1].time.date() == m.time.date()) return QString();
        const QDate d = m.time.date(), today = QDate::currentDate();
        if (d == today) return QStringLiteral("Сегодня");
        if (d == today.addDays(-1)) return QStringLiteral("Вчера");
        return QLocale(QLocale::Russian).toString(d, d.year() == today.year() ? "d MMMM" : "d MMMM yyyy");
    }
    }
    return {};
}

const Message *MessagesModel::message(int row) const {
    const Chat *c = m_chats ? m_chats->chat(m_row) : nullptr;
    if (!c || row < 0 || row >= c->messages.size()) return nullptr;
    return &c->messages[row];
}

void MessagesModel::setChatRow(int row) {
    beginResetModel();
    m_row = row;
    const Chat *c = m_chats->chat(row);
    m_count = c ? c->messages.size() : 0;
    endResetModel();
}
