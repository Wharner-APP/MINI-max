#pragma once
#include <QAbstractListModel>

class ChatsModel;

// Messages of the opened chat.
class MessagesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TextRole = Qt::UserRole + 1, OutgoingRole, TimeRole, SenderRole, SenderColorRole, ReplyRole, ReplyAuthorRole,
                 EditedRole, StatusRole, ReactionsRole, DateHeaderRole, ViewsRole, ShowSenderRole };
    explicit MessagesModel(ChatsModel *chats, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_count; }
    QVariant data(const QModelIndex &index, int role) const override;
    void setChatRow(int row);
    int chatRow() const { return m_row; }
private:
    ChatsModel *m_chats;
    int m_row = -1, m_count = 0;
};
