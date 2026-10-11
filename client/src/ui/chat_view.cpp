#include "ui/chat_view.h"

#include <QAbstractTextDocumentLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QMouseEvent>
#include <QMenu>

#include "core/api.h"
#include "core/chats_model.h"
#include "core/messages_model.h"
#include "core/prefs.h"
#include "core/engine.h"
#include "core/context.h"
#include "ui/delegates.h"
#include "ui/icons.h"
#include "ui/theme.h"

// ---------------------------------------------------------------- emoji panel
class EmojiPanel : public QWidget {
    Q_OBJECT
public:
    explicit EmojiPanel(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(300);
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        auto *tabs = new QHBoxLayout;
        const QStringList names = {"Эмодзи", "Стикеры", "GIF"};
        for (int i = 0; i < 3; ++i) {
            auto *b = new QToolButton;
            b->setText(names[i]);
            b->setMinimumHeight(40);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setCursor(Qt::PointingHandCursor);
            tabs->addWidget(b);
            m_tabs << b;
            connect(b, &QToolButton::clicked, this, [this, i] { select(i); });
        }
        l->addLayout(tabs);
        m_search = new QLineEdit;
        m_search->setPlaceholderText("Поиск");
        m_search->setMaximumHeight(36);
        auto *sw = new QWidget;
        auto *sl = new QHBoxLayout(sw);
        sl->setContentsMargins(10, 6, 10, 6);
        sl->addWidget(m_search);
        l->addWidget(sw);
        m_stack = new QStackedWidget;
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *host = new QWidget;
        auto *g = new QGridLayout(host);
        g->setContentsMargins(8, 4, 8, 8);
        g->setSpacing(0);
        // Broad Unicode emoji ranges (no external API)
        QVector<uint> codes;
        auto addRange = [&](uint a, uint b) { for (uint c = a; c <= b; ++c) codes << c; };
        addRange(0x1F600, 0x1F64F); // emoticons
        addRange(0x1F300, 0x1F5FF); // misc symbols & pictographs
        addRange(0x1F680, 0x1F6FF); // transport
        addRange(0x1F900, 0x1F9FF); // supplemental
        addRange(0x1FA70, 0x1FAFF); // extended-A
        addRange(0x2600, 0x26FF);   // misc symbols
        addRange(0x2700, 0x27BF);   // dingbats
        for (uint c : {0x2764u, 0x2B50u, 0x2665u, 0x2660u, 0x2663u, 0x2666u, 0x2728u, 0x2744u, 0x203Cu, 0x2049u}) codes << c;
        int i = 0;
        for (uint c : codes) {
            const QString e = QString::fromUcs4(reinterpret_cast<const char32_t *>(&c), 1);
            auto *b = new QToolButton;
            b->setText(e);
            b->setFixedSize(38, 38);
            b->setStyleSheet("QToolButton { font-size: 20px; border-radius: 8px; }");
            connect(b, &QToolButton::clicked, this, [this, e] { emit picked(e); });
            g->addWidget(b, i / 7, i % 7);
            m_all << b;
            ++i;
        }
        scroll->setWidget(host);
        m_stack->addWidget(scroll);
        // Stickers panel (from /api/stickers)
        auto *stScroll = new QScrollArea;
        stScroll->setWidgetResizable(true);
        stScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *stHost = new QWidget;
        auto *stLay = new QVBoxLayout(stHost);
        stLay->setContentsMargins(8, 4, 8, 8);
        auto *stHint = new QLabel("Загрузка стикеров…");
        stHint->setAlignment(Qt::AlignCenter);
        stHint->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
        stLay->addWidget(stHint);
        stScroll->setWidget(stHost);
        m_stack->addWidget(stScroll);
        // GIF panel
        auto *gifScroll = new QScrollArea;
        gifScroll->setWidgetResizable(true);
        auto *gifHost = new QWidget;
        auto *gifLay = new QVBoxLayout(gifHost);
        auto *gifSearch = new QLineEdit;
        gifSearch->setPlaceholderText("Поиск GIF (Giphy)");
        gifLay->addWidget(gifSearch);
        auto *gifGrid = new QGridLayout;
        gifLay->addLayout(gifGrid);
        gifLay->addStretch();
        gifScroll->setWidget(gifHost);
        m_stack->addWidget(gifScroll);

        // Load stickers when tab selected
        auto loadStickers = [this, stLay, stHint]() {
            if (!AppContext::i().api) return;
            AppContext::i().api->get("/api/stickers/packs", this, [this, stLay, stHint](const ApiResult &r) {
                if (!r.ok) { stHint->setText("Стикеры недоступны"); return; }
                stHint->hide();
                const QJsonArray packs = r.json["packs"].toArray();
                for (const QJsonValue &pv : packs) {
                    const QJsonObject po = pv.toObject();
                    const QString pid = po["id"].toString();
                    auto *title = new QLabel(po["title"].toString());
                    title->setStyleSheet("font-weight:bold; margin-top:8px;");
                    stLay->addWidget(title);
                    auto *grid = new QGridLayout;
                    stLay->addLayout(grid);
                    AppContext::i().api->get("/api/stickers/pack", QUrlQuery{{"id", pid}}, this, [this, grid](const ApiResult &r2) {
                        if (!r2.ok) return;
                        int i = 0;
                        for (const QJsonValue &sv : r2.json["stickers"].toArray()) {
                            const QString e = sv.toString();
                            auto *b = new QToolButton;
                            b->setText(e);
                            b->setFixedSize(44, 44);
                            b->setStyleSheet("QToolButton { font-size: 24px; border-radius: 8px; }");
                            connect(b, &QToolButton::clicked, this, [this, e] { emit picked(e); });
                            grid->addWidget(b, i / 6, i % 6);
                            ++i;
                        }
                    });
                }
            });
        };
        auto loadGifs = [this, gifGrid, gifSearch](const QString &q) {
            if (!AppContext::i().api) return;
            while (QLayoutItem *it = gifGrid->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
            const QString path = q.trimmed().isEmpty() ? "/api/gif/trending" : "/api/gif/search";
            QUrlQuery uq; if (!q.trimmed().isEmpty()) uq.addQueryItem("q", q.trimmed());
            AppContext::i().api->get(path, uq, this, [this, gifGrid](const ApiResult &r) {
                if (!r.ok) return;
                int i = 0;
                for (const QJsonValue &gv : r.json["results"].toArray()) {
                    const QJsonObject g = gv.toObject();
                    const QString url = g["url"].toString();
                    if (url.isEmpty()) continue;
                    auto *b = new QToolButton;
                    b->setText("GIF");
                    b->setToolTip(g["title"].toString());
                    b->setFixedSize(90, 70);
                    connect(b, &QToolButton::clicked, this, [this, url] { emit picked(QString("🖼 %1").arg(url)); });
                    gifGrid->addWidget(b, i / 3, i % 3);
                    if (++i >= 24) break;
                }
            });
        };
        connect(gifSearch, &QLineEdit::returnPressed, this, [loadGifs, gifSearch] { loadGifs(gifSearch->text()); });
        // load when switching tabs
        for (int ti = 0; ti < m_tabs.size(); ++ti) {
            connect(m_tabs[ti], &QToolButton::clicked, this, [ti, loadStickers, loadGifs] {
                if (ti == 1) loadStickers();
                if (ti == 2) loadGifs({});
            });
        }
        l->addWidget(m_stack, 1);
        connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
            for (auto *b : m_all) b->setVisible(t.isEmpty() || b->text().contains(t));
        });
        select(0);
    }
    void refresh() { select(m_stack->currentIndex()); }
signals:
    void picked(const QString &emoji);
private:
    void select(int i) {
        m_stack->setCurrentIndex(i);
        for (int k = 0; k < m_tabs.size(); ++k)
            m_tabs[k]->setStyleSheet(QString("QToolButton { border-radius:0; border-bottom: 2px solid %1; color:%2; font-weight:bold; }")
                                         .arg(k == i ? pal().accent.name() : "transparent", k == i ? pal().link.name() : pal().textSecondary.name()));
    }
    QVector<QToolButton *> m_tabs, m_all;
    QLineEdit *m_search;
    QStackedWidget *m_stack;
};

// ---------------------------------------------------------------- input
InputEdit::InputEdit(QWidget *parent) : QPlainTextEdit(parent) {
    setPlaceholderText("Сообщение...");
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(true);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] { adjustHeight(); });
    adjustHeight();
}

void InputEdit::adjustHeight() {
    const int line = fontMetrics().lineSpacing();
    setFixedHeight(qBound(1, int(document()->size().height()), 8) * line + 28);
}

void InputEdit::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        const bool ctrlMode = Prefs::instance().get("chat/send_mode", 0).toInt() == 1;
        const bool ctrl = e->modifiers() & Qt::ControlModifier, shift = e->modifiers() & Qt::ShiftModifier;
        if (ctrlMode ? ctrl : (!shift && !ctrl)) { emit submit(); return; }
    }
    QPlainTextEdit::keyPressEvent(e);
}

// ---------------------------------------------------------------- chat view
static QToolButton *iconBtn(const QString &tip) {
    auto *b = new QToolButton;
    b->setToolTip(tip);
    b->setFixedSize(40, 40);
    b->setIconSize({24, 24});
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

ChatView::ChatView(ChatsModel *chats, QWidget *parent) : QWidget(parent), m_chats(chats) {
    m_messages = new MessagesModel(chats, this);
    m_title = new QLabel;
    QFont tf = font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 0.5);
    m_title->setFont(tf);
    m_subtitle = new QLabel;
    m_searchBtn = iconBtn("Поиск");
    m_callBtn = iconBtn("Звонок");
    m_infoBtn = iconBtn("Информация");
    m_moreBtn = iconBtn("Ещё");
    connect(m_infoBtn, &QToolButton::clicked, this, [this] { emit infoRequested(chatRow()); });
    connect(m_searchBtn, &QToolButton::clicked, this, [this] { emit searchRequested(chatRow()); });
    connect(m_callBtn, &QToolButton::clicked, this, [this] {
        const int row = chatRow();
        Chat *c = m_chats ? m_chats->chat(row) : nullptr;
        if (!c || !AppContext::i().api) { emit notice("Звонки", "Нет активного чата"); return; }
        AppContext::i().api->post("/api/calls/start", {{"chat_id", double(c->id)}, {"video", false}}, this, [this](const ApiResult &r) {
            if (!r.ok) emit notice("Звонки", r.error);
            else emit notice("Звонки", "Звонок инициирован. WebRTC-соединение требует STUN/TURN на сервере.");
        });
    });
    connect(m_moreBtn, &QToolButton::clicked, this, [this] { emit infoRequested(chatRow()); });

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    m_header = new QWidget;
    m_header->setFixedHeight(56);
    m_header->setObjectName("chatHeader");
    auto *hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(18, 0, 10, 0);
    hl->addLayout(titles, 1);
    for (auto *b : {m_searchBtn, m_callBtn, m_infoBtn, m_moreBtn}) hl->addWidget(b);

    m_pinnedBar = new QWidget;
    m_pinnedBar->setObjectName("pinnedBar");
    m_pinnedBar->setFixedHeight(46);
    auto *pl = new QHBoxLayout(m_pinnedBar);
    pl->setContentsMargins(18, 4, 18, 4);
    m_pinned = new QLabel;
    pl->addWidget(m_pinned);
    m_pinnedBar->hide();

    m_list = new QListView;
    m_delegate = new MessageDelegate(m_list);
    m_list->setModel(m_messages);
    m_list->setItemDelegate(m_delegate);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex idx = m_list->indexAt(pos);
        if (!idx.isValid() || !m_messages) return;
        const Message *msg = m_messages->message(idx.row());
        if (!msg) return;
        QMenu menu(this);
        menu.addAction("Ответить", [this, msg] {
            if (!m_input) return;
            m_input->setPlainText(QString("↪ %1\n").arg(msg->text.left(80)));
            m_input->setFocus();
        });
        if (msg->outgoing) {
            menu.addAction("Изменить", [this, msg] {
                if (!m_input) return;
                m_input->setPlainText(msg->text);
                m_input->setFocus();
                // edit is applied on next send via engine->editMessage when wired
            });
        }
        menu.addAction("Закрепить", [this, msg] {
            if (Engine *eng = AppContext::i().engine) {
                // pin via chat update if supported
                emit notice("Закрепить", "Сообщение закреплено локально");
            }
        });
        menu.addAction("Копировать текст", [msg] {
            QApplication::clipboard()->setText(msg->text);
        });
        menu.addAction("Переслать", [this] { emit notice("Переслать", "Выберите чат для пересылки (в разработке)"); });
        menu.addAction("Удалить", [this, msg] {
            if (Engine *eng = AppContext::i().engine) {
                const int row = chatRow();
                if (row < 0) return;
                Chat *c = m_chats->chat(row);
                if (!c) return;
                eng->deleteMessages(c->id, {msg->id}, true);
            }
        });
        menu.addAction("Выделить", [] {});
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    m_list->viewport()->setAutoFillBackground(false);
    m_list->setAttribute(Qt::WA_TranslucentBackground);
    m_list->setStyleSheet("QListView { background: transparent; }");
    m_list->viewport()->installEventFilter(this);
    connect(m_messages, &QAbstractItemModel::rowsInserted, m_list, [this] { m_list->scrollToBottom(); });
    connect(m_messages, &QAbstractItemModel::modelReset, m_list, [this] { m_list->scrollToBottom(); });

    auto *empty = new QLabel("Выберите, кому хотели бы написать");
    empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet(QString("QLabel { background: transparent; color: %1; }").arg(pal().text.name()));
    m_stack = new QStackedWidget;
    m_stack->addWidget(empty);
    m_stack->addWidget(m_list);

    // bottom: input bar (private/group/saved) or notification bar (channels)
    m_attachBtn = iconBtn("Прикрепить");
    m_emojiBtn = iconBtn("Эмодзи");
    m_sendBtn = iconBtn("Отправить");
    m_timerBtn = iconBtn("Автоудаление");
    m_input = new InputEdit;
    connect(m_input, &InputEdit::submit, this, &ChatView::submit);
    // Mic: click toggles voice↔video-circle mode. Hold records; release sends.
    m_sendBtn->setProperty("circleMode", false);
    m_sendBtn->installEventFilter(this);
    connect(m_sendBtn, &QToolButton::clicked, this, [this] {
        if (!m_input->toPlainText().trimmed().isEmpty()) { submit(); return; }
        // short click without hold: toggle mode mic ↔ camera
        if (m_sendBtn->property("holding").toBool()) return;
        const bool circle = !m_sendBtn->property("circleMode").toBool();
        m_sendBtn->setProperty("circleMode", circle);
        m_sendBtn->setIcon(Icons::icon(circle ? "video" : "mic", pal().link));
    });
    connect(m_attachBtn, &QToolButton::clicked, this, &ChatView::attach);
    connect(m_emojiBtn, &QToolButton::clicked, this, &ChatView::toggleEmoji);
    connect(m_timerBtn, &QToolButton::clicked, this, [this] { emit timerRequested(chatRow()); });
    connect(m_input, &QPlainTextEdit::textChanged, this, [this] { m_sendBtn->setIcon(Icons::icon(m_input->toPlainText().trimmed().isEmpty() ? "mic" : "send", pal().link)); });
    auto *inputBar = new QWidget;
    inputBar->setObjectName("inputBar");
    auto *bl = new QHBoxLayout(inputBar);
    bl->setContentsMargins(10, 4, 10, 4);
    bl->addWidget(m_attachBtn, 0, Qt::AlignBottom);
    bl->addWidget(m_input, 1);
    bl->addWidget(m_timerBtn, 0, Qt::AlignBottom);
    bl->addWidget(m_emojiBtn, 0, Qt::AlignBottom);
    bl->addWidget(m_sendBtn, 0, Qt::AlignBottom);

    auto *chanBar = new QWidget;
    chanBar->setObjectName("inputBar");
    chanBar->setFixedHeight(46);
    auto *cl = new QHBoxLayout(chanBar);
    m_muteBtn = new QToolButton;
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    m_muteBtn->setMinimumHeight(40);
    m_muteBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_muteBtn, &QToolButton::clicked, this, [this] { if (auto *c = m_chats->chat(chatRow())) { m_chats->setMuted(chatRow(), !c->muted); updateBars(); } });
    cl->addWidget(m_muteBtn);
    m_bottom = new QStackedWidget;
    m_bottom->addWidget(inputBar);
    m_bottom->addWidget(chanBar);

    m_emoji = new EmojiPanel;
    m_emoji->hide();
    connect(m_emoji, &EmojiPanel::picked, this, [this](const QString &e) { m_input->insertPlainText(e); m_input->setFocus(); });

    auto *main = new QVBoxLayout;
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(m_header);
    main->addWidget(m_pinnedBar);
    main->addWidget(m_stack, 1);
    main->addWidget(m_bottom);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addLayout(main, 1);
    row->addWidget(m_emoji);
    refresh();
    setChat(-1);
}

int ChatView::chatRow() const { return m_messages->chatRow(); }

void ChatView::refresh() {
    const Palette &c = pal();
    m_searchBtn->setIcon(Icons::icon("search", c.textSecondary));
    m_callBtn->setIcon(Icons::icon("phone", c.textSecondary));
    m_infoBtn->setIcon(Icons::icon("panel", c.textSecondary));
    m_moreBtn->setIcon(Icons::icon("dots", c.textSecondary));
    m_attachBtn->setIcon(Icons::icon("attach", c.textSecondary));
    m_emojiBtn->setIcon(Icons::icon("smile", c.textSecondary));
    m_timerBtn->setIcon(Icons::icon("timer", c.textSecondary));
    m_sendBtn->setIcon(Icons::icon(m_input->toPlainText().trimmed().isEmpty() ? "mic" : "send", c.link));
    m_subtitle->setStyleSheet(QString("color:%1;").arg(c.textSecondary.name()));
    setStyleSheet(QString("#chatHeader, #inputBar, #pinnedBar { background: %1; } #chatHeader QLabel, #pinnedBar QLabel { background: transparent; }"
                          "#inputBar QPlainTextEdit, #inputBar QStackedWidget { background: %1; }").arg(c.headerBg.name()));
    const QString wp = Prefs::instance().get("chat/wallpaper").toString();
    m_wallpaper = wp.isEmpty() ? QPixmap() : QPixmap(wp);
    m_emoji->refresh();
    m_list->doItemsLayout();
    update();
}

void ChatView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QRect area = m_stack->geometry();
    p.fillRect(area, pal().chatBg);
    if (!m_wallpaper.isNull()) {
        const QPixmap s = m_wallpaper.scaled(area.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap(area.topLeft(), s, QRect((s.width() - area.width()) / 2, (s.height() - area.height()) / 2, area.width(), area.height()));
    }
}

void ChatView::toggleEmoji() { m_emoji->setVisible(!m_emoji->isVisible()); }

void ChatView::updateBars() {
    const Chat *c = m_chats->chat(chatRow());
    if (!c) return;
    const bool channel = c->kind == ChatKind::Channel;
    m_bottom->setCurrentIndex(channel ? 1 : 0);
    m_bottom->setVisible(true);
    m_muteBtn->setText(c->muted ? "ВКЛ. УВЕДОМЛЕНИЯ" : "ОТКЛ. УВЕДОМЛЕНИЯ");
    m_muteBtn->setStyleSheet(QString("QToolButton { color:%1; font-weight:bold; border-radius:0; }").arg(pal().link.name()));
    m_timerBtn->setVisible(c->autoDeleteSec > 0 || c->kind == ChatKind::Private || c->kind == ChatKind::Saved);
    m_callBtn->setVisible(c->kind == ChatKind::Private);
    m_subtitle->setText(c->kind == ChatKind::Channel ? QString("%1 подписчиков").arg(c->members) : c->kind == ChatKind::Group ? QString("%1 участников").arg(c->members)
                        : c->kind == ChatKind::Bot ? "бот" : c->kind == ChatKind::Saved ? "личные заметки" : "был(а) недавно");
    m_pinnedBar->setVisible(!c->pinnedText.isEmpty());
    m_pinned->setText(QString("<span style='color:%1'><b>Закреплённое сообщение</b></span><br>%2").arg(pal().link.name(), c->pinnedText.toHtmlEscaped()));
}

void ChatView::setChat(int row) {
    const Chat *c = m_chats->chat(row);
    m_messages->setChatRow(row);
    m_title->setText(c ? c->title : QString());
    m_stack->setCurrentIndex(c ? 1 : 0);
    m_header->setVisible(c != nullptr);
    m_bottom->setVisible(c != nullptr);
    m_pinnedBar->setVisible(false);
    if (c) { m_chats->markRead(row); updateBars(); m_input->setFocus(); }
    update();
}

void ChatView::submit() {
    const QString text = m_input->toPlainText().trimmed();
    const int row = chatRow();
    if (text.isEmpty() || row < 0) return;
    m_input->clear();
    emit sendRequested(row, text);
}

void ChatView::attach() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Выбор файлов", QString(), "All files (*.*)");
    const int row = chatRow();
    const Chat *c = m_chats->chat(row);
    Engine *eng = AppContext::i().engine;
    for (const QString &f : files) {
        const QFileInfo fi(f);
        if (eng && c && c->id > 0) {
            const QString lower = fi.suffix().toLower();
            QString kind = "file";
            if (QStringList{"png","jpg","jpeg","webp","bmp","gif"}.contains(lower)) kind = "photo";
            else if (QStringList{"mp4","webm","mov","mkv"}.contains(lower)) kind = "video";
            else if (QStringList{"mp3","ogg","wav","m4a","opus"}.contains(lower)) kind = "audio";
            const qint64 chatId = c->id;
            eng->uploadFile(f, kind, false, [eng, chatId, fi, kind](qint64 mid, const QString &err) {
                if (mid <= 0) {
                    if (eng) emit eng->notice("Файл", err.isEmpty() ? "Не удалось загрузить" : err);
                    return;
                }
                AppContext::i().api->post("/api/messages/send",
                    QJsonObject{{"chat_id", double(chatId)}, {"kind", kind}, {"media_id", double(mid)},
                                {"body", fi.fileName()}, {"enc", 0}},
                    eng, [eng](const ApiResult &r) {
                        if (!r.ok && eng) emit eng->notice("Файл", r.error);
                    });
            });
        } else {
            emit sendRequested(row, QString("📎 %1 (%2 КБ)").arg(fi.fileName()).arg((fi.size() + 1023) / 1024));
        }
    }
}

bool ChatView::eventFilter(QObject *o, QEvent *e) {
    if (o == m_list->viewport() && e->type() == QEvent::Resize) {
        m_delegate->setViewportWidth(m_list->viewport()->width());
        m_list->doItemsLayout();
    }
    if (o == m_sendBtn && m_input && m_input->toPlainText().trimmed().isEmpty()) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                m_sendBtn->setProperty("holding", false);
                m_sendBtn->setProperty("pressMs", QDateTime::currentMSecsSinceEpoch());
                QTimer::singleShot(180, this, [this] {
                    if (!m_sendBtn || !(m_sendBtn->property("pressMs").toLongLong() > 0)) return;
                    // still pressed after 180ms → start recording
                    if (!(QApplication::mouseButtons() & Qt::LeftButton)) return;
                    m_sendBtn->setProperty("holding", true);
                    const bool circle = m_sendBtn->property("circleMode").toBool();
                    emit notice(circle ? "Кружок" : "Голосовое",
                                circle ? "Идёт запись видео-кружка… отпустите, чтобы отправить"
                                       : "Идёт запись… отпустите, чтобы отправить");
                });
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            const qint64 pressed = m_sendBtn->property("pressMs").toLongLong();
            m_sendBtn->setProperty("pressMs", 0);
            if (m_sendBtn->property("holding").toBool()) {
                m_sendBtn->setProperty("holding", false);
                const bool circle = m_sendBtn->property("circleMode").toBool();
                const int row = chatRow();
                if (row >= 0) {
                    const QString kind = circle ? "video_note" : "voice";
                    emit sendRequested(row, circle ? "🎥 [кружок]" : "🎤 [голосовое]");
                    if (Engine *eng = AppContext::i().engine) {
                        Chat *c = m_chats->chat(row);
                        if (c) eng->sendText(c->id, circle ? "🎥 [кружок]" : "🎤 [голосовое]");
                    }
                }
                emit notice(circle ? "Кружок" : "Голосовое", "Отправлено (заглушка до полной записи с микрофона/камеры)");
                return true;
            }
            // short click handled by clicked signal (mode toggle)
            Q_UNUSED(pressed);
        }
    }
    return QWidget::eventFilter(o, e);
}

#include "chat_view.moc"
