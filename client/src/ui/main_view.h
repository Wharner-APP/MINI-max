#pragma once
#include <QLineEdit>
#include <QListView>
#include <QToolButton>
#include <QWidget>

class ChatsModel;
class ChatFilterProxy;
class ChatListDelegate;
class ChatView;
class FolderRail;
class Drawer;
class PopupHost;
class QLabel;

class MainView : public QWidget {
    Q_OBJECT
public:
    explicit MainView(ChatsModel *chats, QWidget *parent = nullptr);
    PopupHost *popup() const { return m_popup; }
    void setOnline(bool online);
    void openChat(int sourceRow);
    void openDrawer();
    void showEmpty();
    void refreshTheme();
    void toggleArchive(bool on);
    void openDevTarget(const QString &name);
protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    void rebuildFolders();
    void updateLayoutMode();
    void searchInChat(int row);
    ChatsModel *m_chats;
    ChatFilterProxy *m_proxy;
    FolderRail *m_rail;
    QWidget *m_listPanel, *m_archiveRow, *m_listHeader, *m_archiveHeader;
    QLineEdit *m_search;
    QListView *m_list;
    ChatListDelegate *m_delegate;
    ChatView *m_chatView;
    Drawer *m_drawer;
    PopupHost *m_popup;
    QToolButton *m_backBtn;
    QLabel *m_archiveLabel = nullptr;
    int m_currentRow = -1;
    bool m_online = false, m_archive = false, m_compact = false;
};
