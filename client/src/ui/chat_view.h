#pragma once
#include <QLabel>
#include <QListView>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QWidget>

class ChatsModel;
class MessagesModel;
class MessageDelegate;
class EmojiPanel;
class QStackedWidget;

// Multi-line input: Enter sends (or Ctrl+Enter, see settings), Shift+Enter inserts a new line.
class InputEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InputEdit(QWidget *parent = nullptr);
signals:
    void submit();
protected:
    void keyPressEvent(QKeyEvent *e) override;
private:
    void adjustHeight();
};

class ChatView : public QWidget {
    Q_OBJECT
public:
    explicit ChatView(ChatsModel *chats, QWidget *parent = nullptr);
    void setChat(int row);
    int chatRow() const;
    void refresh();           // theme / prefs changed
    void toggleEmoji();
signals:
    void sendRequested(int row, const QString &text);
    void infoRequested(int row);
    void timerRequested(int row);
    void searchRequested(int row);
    void notice(const QString &title, const QString &text);
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *) override;
private:
    void submit();
    void attach();
    void updateBars();
    ChatsModel *m_chats;
    MessagesModel *m_messages;
    MessageDelegate *m_delegate;
    QStackedWidget *m_stack, *m_bottom;
    QListView *m_list;
    QLabel *m_title, *m_subtitle, *m_pinned;
    QWidget *m_pinnedBar, *m_header;
    InputEdit *m_input;
    QToolButton *m_searchBtn, *m_callBtn, *m_infoBtn, *m_moreBtn, *m_attachBtn, *m_emojiBtn, *m_sendBtn, *m_timerBtn, *m_muteBtn;
    EmojiPanel *m_emoji;
    QPixmap m_wallpaper;
};
