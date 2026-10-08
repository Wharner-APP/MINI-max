#include "ui/main_view.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <mm/build_info.h>

#include "core/chats_model.h"
#include "core/engine.h"
#include "core/context.h"
#include "core/context.h"
#include "core/prefs.h"
#include "ui/chat_view.h"
#include "ui/delegates.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/popup.h"
#include "ui/theme.h"
#include "ui/widgets.h"

// ---------------------------------------------------------------- left rail with folders
struct RailItem { QString id, icon, label; int badge = 0; };

class FolderRail : public QWidget {
    Q_OBJECT
public:
    explicit FolderRail(QWidget *parent = nullptr) : QWidget(parent) { setFixedWidth(72); setMouseTracking(true); }
    void setItems(const QVector<RailItem> &items) { m_items = items; update(); }
    void setCurrent(const QString &id) { m_current = id; update(); }
signals:
    void selected(const QString &id);
    void menuClicked();
    void editClicked();
protected:
    QRect itemRect(int i) const { return QRect(0, 60 + i * 62, width(), 62); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const Palette &c = pal();
        p.fillRect(rect(), c.railBg);
        p.drawPixmap(24, 18, Icons::pixmap("menu", c.textSecondary, 24));
        QFont small = font();
        small.setPointSizeF(font().pointSizeF() - 2.5);
        for (int i = 0; i < m_items.size(); ++i) {
            const RailItem &it = m_items[i];
            const QRect r = itemRect(i);
            const bool active = it.id == m_current;
            if (active) p.fillRect(r, c.sidebarBg);
            else if (i == m_hover) p.fillRect(r, c.itemHover.darker(130));
            const QColor col = active ? c.link : c.textSecondary;
            p.drawPixmap(r.center().x() - 15, r.top() + 8, Icons::pixmap(it.icon, col, 30));
            p.setFont(small);
            p.setPen(col);
            p.drawText(QRect(r.left() + 2, r.top() + 40, r.width() - 4, 18), Qt::AlignCenter, QFontMetrics(small).elidedText(it.label, Qt::ElideRight, r.width() - 6));
            if (it.badge > 0) {
                const QString s = QString::number(it.badge);
                const int w = qMax(18, QFontMetrics(small).horizontalAdvance(s) + 10);
                const QRect b(r.center().x() + 3, r.top() + 4, w, 18);
                p.setPen(Qt::NoPen);
                p.setBrush(c.badge);
                p.drawRoundedRect(b, 9, 9);
                p.setPen(Qt::white);
                p.drawText(b, Qt::AlignCenter, s);
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        int h = -1;
        for (int i = 0; i < m_items.size(); ++i) if (itemRect(i).contains(e->pos())) h = i;
        if (h != m_hover) { m_hover = h; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = -1; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->pos().y() < 56) { emit menuClicked(); return; }
        for (int i = 0; i < m_items.size(); ++i)
            if (itemRect(i).contains(e->pos())) { if (m_items[i].id == "edit") emit editClicked(); else emit selected(m_items[i].id); return; }
    }
private:
    QVector<RailItem> m_items;
    QString m_current = "all";
    int m_hover = -1;
};

// ---------------------------------------------------------------- slide-in drawer
class Drawer : public QWidget {
    Q_OBJECT
public:
    explicit Drawer(QWidget *parent) : QWidget(parent) {
        hide();
        m_panel = new QWidget(this);
        m_panel->setFixedWidth(274);
    }
    QWidget *panel() const { return m_panel; }
    void build(std::function<void(QVBoxLayout *)> fill) {
        delete m_panel->layout();
        qDeleteAll(m_panel->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
        auto *l = new QVBoxLayout(m_panel);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        fill(l);
    }
    void openAnimated() {
        setGeometry(parentWidget()->rect());
        m_panel->setGeometry(-274, 0, 274, height());
        show();
        raise();
        auto *a = new QPropertyAnimation(m_panel, "pos", this);
        a->setDuration(Prefs::instance().getBool("ui/animations", true) ? 180 : 0);
        a->setStartValue(QPoint(-274, 0));
        a->setEndValue(QPoint(0, 0));
        a->start(QAbstractAnimation::DeleteWhenStopped);
        m_dim = 1;
        update();
    }
    void closeAnimated() {
        auto *a = new QPropertyAnimation(m_panel, "pos", this);
        a->setDuration(Prefs::instance().getBool("ui/animations", true) ? 140 : 0);
        a->setStartValue(m_panel->pos());
        a->setEndValue(QPoint(-274, 0));
        connect(a, &QAbstractAnimation::finished, this, [this] { hide(); });
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(rect(), QColor(0, 0, 0, 130)); }
    void mousePressEvent(QMouseEvent *e) override { if (e->pos().x() > 274) closeAnimated(); }
    void resizeEvent(QResizeEvent *) override { m_panel->setFixedHeight(height()); }
private:
    QWidget *m_panel;
    int m_dim = 0;
};

// ---------------------------------------------------------------- main view
MainView::MainView(ChatsModel *chats, QWidget *parent) : QWidget(parent), m_chats(chats) {
    m_proxy = new ChatFilterProxy(this);
    m_proxy->setSourceModel(chats);
    m_rail = new FolderRail;

    m_search = new QLineEdit;
    m_search->setPlaceholderText("Поиск");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(36);
    m_listHeader = new QWidget;
    auto *lh = new QHBoxLayout(m_listHeader);
    lh->setContentsMargins(12, 10, 12, 8);
    lh->addWidget(m_search);

    m_archiveHeader = new QWidget;
    auto *ah = new QHBoxLayout(m_archiveHeader);
    ah->setContentsMargins(12, 10, 12, 8);
    m_backBtn = new QToolButton;
    m_backBtn->setIconSize({22, 22});
    ah->addWidget(m_backBtn);
    auto *at = new QLabel("Архив");
    QFont af = font();
    af.setBold(true);
    af.setPointSizeF(af.pointSizeF() + 1);
    at->setFont(af);
    ah->addWidget(at, 1);
    m_archiveHeader->hide();
    connect(m_backBtn, &QToolButton::clicked, this, [this] { toggleArchive(false); });

    m_archiveRow = new QWidget;
    m_archiveRow->setFixedHeight(62);
    m_archiveRow->setCursor(Qt::PointingHandCursor);
    m_archiveRow->installEventFilter(this);
    auto *arl = new QHBoxLayout(m_archiveRow);
    arl->setContentsMargins(10, 7, 12, 7);
    auto *aic = new QLabel;
    aic->setObjectName("archiveIcon");
    aic->setFixedSize(48, 48);
    arl->addWidget(aic);
    m_archiveLabel = new QLabel;
    m_archiveLabel->setObjectName("archiveText");
    arl->addWidget(m_archiveLabel, 1);

    m_list = new QListView;
    m_delegate = new ChatListDelegate(m_list);
    m_list->setModel(m_proxy);
    m_list->setItemDelegate(m_delegate);
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &ix) { openChat(m_proxy->mapToSource(ix).row()); });

    m_listPanel = new QWidget;
    auto *lp = new QVBoxLayout(m_listPanel);
    lp->setContentsMargins(0, 0, 0, 0);
    lp->setSpacing(0);
    lp->addWidget(m_listHeader);
    lp->addWidget(m_archiveHeader);
    lp->addWidget(m_archiveRow);
    lp->addWidget(m_list, 1);

    m_chatView = new ChatView(chats);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_rail);
    row->addWidget(m_listPanel);
    auto *sep = new QFrame;
    sep->setFixedWidth(1);
    sep->setObjectName("sep");
    row->addWidget(sep);
    row->addWidget(m_chatView, 1);

    m_popup = new PopupHost(this);
    m_drawer = new Drawer(this);

    connect(m_search, &QLineEdit::textChanged, m_proxy, &ChatFilterProxy::setSearch);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &q) {
        if (q.trimmed().size() < 2 || !AppContext::i().engine) return;
        AppContext::i().engine->search(q, [this, q](const QVector<SearchHit> &hits) {
            if (m_search->text().trimmed() != q.trimmed()) return;  // stale
            // Show quick results as a notice with count; full UI panel can expand later
            if (hits.isEmpty()) return;
            // Open DM / join on Enter is handled via context menu below — keep local filter primary
            Q_UNUSED(hits);
        });
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        const QString q = m_search->text().trimmed();
        if (q.size() < 2 || !AppContext::i().engine) return;
        AppContext::i().engine->search(q, [this](const QVector<SearchHit> &hits) {
            if (hits.isEmpty()) { Pages::infoDialog(m_popup, "Поиск", "Ничего не найдено."); return; }
            QStringList opts;
            QVector<SearchHit> copy = hits;
            for (const SearchHit &h : copy) {
                if (h.type == SearchHit::User)
                    opts << QString("%1 (%2)%3").arg(h.name.isEmpty() ? h.login : h.name, h.login, h.isBot ? " · бот" : "");
                else
                    opts << QString("%1 · %2%3").arg(h.title, h.kind, h.username.isEmpty() ? "" : (" @" + h.username));
            }
            m_popup->choice("Результаты поиска", opts, 0, [this, copy](int i) {
                if (i < 0 || i >= copy.size()) return;
                const SearchHit &h = copy[i];
                if (h.type == SearchHit::User)
                    AppContext::i().engine->openDm(h.login.isEmpty() ? h.username : h.login);
                else if (!h.username.isEmpty())
                    AppContext::i().engine->joinByUsername(h.username);
            });
        });
    });
    connect(m_rail, &FolderRail::selected, this, [this](const QString &id) { toggleArchive(false); m_proxy->setFolder(id); m_rail->setCurrent(id); });
    connect(m_rail, &FolderRail::menuClicked, this, &MainView::openDrawer);
    connect(m_rail, &FolderRail::editClicked, this, [this] { Pages::settings(m_popup); });
    connect(m_chatView, &ChatView::sendRequested, this, [this](int row, const QString &text) {
        const Chat *c = m_chats->chat(row);
        if (!c) return;
        if (Engine *eng = AppContext::i().engine) {
            eng->sendText(c->id, text);
            eng->markRead(c->id, c->messages.isEmpty() ? 0 : c->messages.last().id);
        } else {
            m_chats->appendMessage(row, text, true);
        }
        if (c->autoDeleteSec > 0) m_chats->purgeOlderThan(row, c->autoDeleteSec);
    });
    connect(m_chatView, &ChatView::infoRequested, this, [this](int row) { if (row >= 0) Pages::chatProfile(m_popup, row); });
    connect(m_chatView, &ChatView::timerRequested, this, [this](int row) { Pages::autoDeleteTimer(m_popup, row); });
    connect(m_chatView, &ChatView::searchRequested, this, &MainView::searchInChat);
    connect(m_chatView, &ChatView::notice, this, [this](const QString &t, const QString &x) { Pages::infoDialog(m_popup, t, x); });
    connect(chats, &ChatsModel::dataChanged, this, [this] { rebuildFolders(); });
    connect(chats, &ChatsModel::rowsInserted, this, [this] { rebuildFolders(); });
    connect(&Prefs::instance(), &Prefs::changed, this, [this](const QString &k) {
        if (k == "folders/list") rebuildFolders();
        if (k == "chat/wallpaper") m_chatView->refresh();
    });
    AppContext::i().openChatRow = [this](int r) { openChat(r); };
    rebuildFolders();
    refreshTheme();
    showEmpty();
}

bool MainView::eventFilter(QObject *o, QEvent *e) {
    if (o == m_archiveRow && e->type() == QEvent::MouseButtonRelease) { toggleArchive(true); return true; }
    return QWidget::eventFilter(o, e);
}

void MainView::refreshTheme() {
    const Palette &c = pal();
    setStyleSheet(QString("#sep { background:%1; } #archiveText { background: transparent; }").arg(c.railBg.name()));
    m_listPanel->setStyleSheet(QString("QWidget { background:%1; } QLineEdit { background:%2; }").arg(c.sidebarBg.name(), c.inputBg.name()));
    m_list->setStyleSheet(QString("QListView { background:%1; }").arg(c.sidebarBg.name()));
    m_backBtn->setIcon(Icons::icon("back", c.textSecondary, 22));
    m_archiveRow->setStyleSheet(QString("QWidget { background:%1; } QWidget:hover { background:%2; }").arg(c.sidebarBg.name(), c.itemHover.name()));
    if (auto *ic = m_archiveRow->findChild<QLabel *>("archiveIcon")) {
        QPixmap pm(96, 96);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(c.badgeMuted);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 48, 48);
        p.drawPixmap(12, 12, Icons::pixmap("archive", Qt::white, 24));
        ic->setPixmap(pm);
    }
    m_chatView->refresh();
    rebuildFolders();
    update();
}

void MainView::rebuildFolders() {
    QVector<RailItem> items;
    auto count = [this](auto pred) {
        int n = 0;
        for (int i = 0; i < m_chats->rowCount(); ++i) { const Chat *c = m_chats->chat(i); if (!c->archived && c->unread > 0 && pred(*c)) ++n; }
        return n;
    };
    items.push_back({"all", "chats", "Все чаты", count([](const Chat &) { return true; })});
    items.push_back({"groups", "group", "Группы", count([](const Chat &c) { return c.kind == ChatKind::Group; })});
    items.push_back({"private", "person", "ЛС", count([](const Chat &c) { return c.kind == ChatKind::Private || c.kind == ChatKind::Saved; })});
    items.push_back({"bots", "bot", "Боты", count([](const Chat &c) { return c.kind == ChatKind::Bot; })});
    items.push_back({"channels", "channel", "Каналы", count([](const Chat &c) { return c.kind == ChatKind::Channel; })});
    for (const QString &f : Prefs::instance().get("folders/list").toStringList()) {
        const QString id = f.section('|', 0, 0), name = f.section('|', 1);
        items.push_back({"custom:" + id, "folder", name, count([id](const Chat &c) { return c.folders.contains(id); })});
    }
    items.push_back({"edit", "sliders", "Ред.", 0});
    m_rail->setItems(items);
    // archive row
    int archived = 0, unread = 0;
    QString last;
    for (int i = 0; i < m_chats->rowCount(); ++i) { const Chat *c = m_chats->chat(i); if (c->archived) { ++archived; unread += c->unread; last = c->title; } }
    m_archiveRow->setVisible(archived > 0 && !m_archive && !m_compact && m_search->text().isEmpty());
    m_listHeader->setVisible(!m_archive && !m_compact);
    m_archiveHeader->setVisible(m_archive && !m_compact);
    if (m_archiveLabel) m_archiveLabel->setText(QString("<b>Архив</b><br><span style='color:%1'>%2</span>").arg(pal().textSecondary.name(), last.toHtmlEscaped()));
}

void MainView::toggleArchive(bool on) {
    m_archive = on;
    m_proxy->setArchiveView(on);
    rebuildFolders();
}

void MainView::setOnline(bool online) {
    m_online = online;
    m_search->setPlaceholderText(online ? "Поиск" : "Соединение...");
}

void MainView::openChat(int row) {
    m_currentRow = row;
    if (const Chat *c = m_chats->chat(row); c && c->archived != m_archive) toggleArchive(c->archived);
    if (const Chat *c = m_chats->chat(row); c && AppContext::i().engine) {
        const qint64 last = c->messages.isEmpty() ? 0 : c->messages.last().id;
        AppContext::i().engine->markRead(c->id, last);
    }
    m_chatView->setChat(row);
    for (int i = 0; i < m_proxy->rowCount(); ++i) {
        const QModelIndex ix = m_proxy->index(i, 0);
        if (m_proxy->mapToSource(ix).row() == row) { m_list->setCurrentIndex(ix); break; }
    }
    updateLayoutMode();
}

void MainView::showEmpty() {
    m_currentRow = -1;
    m_chatView->setChat(-1);
    m_list->clearSelection();
    updateLayoutMode();
}

void MainView::updateLayoutMode() {
    m_compact = m_currentRow >= 0 && width() < 1500;
    m_delegate->setCompact(m_compact);
    m_listPanel->setFixedWidth(m_compact ? 66 : qBound(260, width() / 4, 340));
    rebuildFolders();
    m_list->doItemsLayout();
    m_list->viewport()->update();
}

void MainView::resizeEvent(QResizeEvent *) {
    updateLayoutMode();
    if (m_drawer->isVisible()) m_drawer->setGeometry(rect());
}

void MainView::searchInChat(int row) {
    const Chat *c = m_chats->chat(row);
    if (!c) return;
    m_popup->input("Поиск по сообщениям", "Что искать?", {}, "Найти", [this, row](const QString &q) {
        const Chat *ch = m_chats->chat(row);
        int found = 0;
        for (const Message &m : ch->messages) if (m.text.contains(q, Qt::CaseInsensitive)) ++found;
        Pages::infoDialog(m_popup, "Поиск", found ? QString("Найдено сообщений: %1").arg(found) : QString("Ничего не найдено."));
    });
}

void MainView::openDrawer() {
    const Session &s = AppContext::i().session;
    const Palette &c = pal();
    m_drawer->panel()->setStyleSheet(QString("QWidget { background:%1; }").arg(c.popupBg.name()));
    m_drawer->build([&](QVBoxLayout *l) {
        auto *head = new QWidget;
        auto *hl = new QVBoxLayout(head);
        hl->setContentsMargins(20, 24, 16, 12);
        auto *av = new QLabel;
        av->setPixmap(profileAvatarPixmap(60, s.displayName, qHash(s.login)));
        hl->addWidget(av);
        hl->addSpacing(8);
        auto *nm = new QLabel(QString("<b>%1</b><br><a href='#' style='color:%2;text-decoration:none'>Установить эмодзи-статус</a>").arg(s.displayName.toHtmlEscaped(), c.link.name()));
        connect(nm, &QLabel::linkActivated, this, [this] { Pages::premium(m_popup, false); m_drawer->closeAnimated(); });
        hl->addWidget(nm);
        l->addWidget(head);
        auto addRow = [&](const QString &icon, const QString &text, std::function<void()> cb) {
            auto *r = new ClickRow(icon, text, {});
            r->setRowHeight(44);
            connect(r, &ClickRow::clicked, this, [this, cb] { m_drawer->closeAnimated(); cb(); });
            l->addWidget(r);
            return r;
        };
        auto *sep = new QFrame; sep->setFixedHeight(8); sep->setStyleSheet(QString("background:%1;").arg(c.divider.name())); l->addWidget(sep);
        addRow("account", "Мой профиль", [this] { Pages::myProfile(m_popup); });
        auto *sep2 = new QFrame; sep2->setFixedHeight(1); sep2->setStyleSheet(QString("background:%1;").arg(c.divider.name())); l->addWidget(sep2);
        addRow("group", "Создать группу", [this] { Pages::createGroup(m_popup, false); });
        addRow("channel", "Создать канал", [this] { Pages::createGroup(m_popup, true); });
        addRow("person", "Контакты", [this] { Pages::contacts(m_popup); });
        addRow("phone", "Звонки", [this] { Pages::calls(m_popup); });
        addRow("bookmark", "Избранное", [this] {
            for (int i = 0; i < m_chats->rowCount(); ++i) if (m_chats->chat(i)->kind == ChatKind::Saved) { openChat(i); return; }
        });
        addRow("gear", "Настройки", [this] { Pages::settings(m_popup); });
        auto *night = new ClickRow("moon", "Ночной режим", {});
        night->setRowHeight(44);
        auto *sw = new Switch;
        sw->setChecked(ThemeManager::instance().isDark());
        night->setSwitch(sw);
        connect(sw, &QAbstractButton::toggled, this, [](bool on) { Prefs::instance().set("ui/theme", on ? "dark" : "light"); ThemeManager::instance().setMode(on ? "dark" : "light"); });
        l->addWidget(night);
        l->addStretch(1);
        auto *foot = new QLabel(QString("<span style='color:%1'>%2 Desktop<br>Версия %3 – <a href='%4' style='color:%5'>О программе</a></span>")
                                    .arg(c.textSecondary.name(), MM_APP_NAME, MM_VERSION_STR, MM_WEBSITE, c.link.name()));
        foot->setOpenExternalLinks(true);
        foot->setContentsMargins(24, 8, 8, 18);
        l->addWidget(foot);
    });
    m_drawer->openAnimated();
}

void MainView::openDevTarget(const QString &name) {
    if (name == "drawer") openDrawer();
    else if (name == "settings") Pages::settings(m_popup);
    else if (name == "premium") Pages::premium(m_popup, false);
    else if (name == "stars") Pages::stars(m_popup, false);
    else if (name == "gifts") Pages::giftPeople(m_popup);
    else if (name == "profile") Pages::chatProfile(m_popup, 3);
    else if (name == "archive") toggleArchive(true);
    else if (name == "chat") openChat(2);
    else if (name == "channel") openChat(4);
    else if (name == "emoji") m_chatView->toggleEmoji();
    else if (name == "business") { Pages::settings(m_popup); Pages::business(m_popup); }
    else if (name == "payment") { PaymentRequest r; r.kind = "premium"; Pages::startPayment(m_popup, r); }
}

#include "main_view.moc"
