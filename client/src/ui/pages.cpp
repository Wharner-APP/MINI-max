#include <cmath>
#include "ui/pages.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>
#include <mm/build_info.h>
#include <mm/paths.h>

#include "core/api.h"
#include "core/engine.h"
#include <QFileDialog>
#include "core/catalog.h"
#include "core/chats_model.h"
#include "core/context.h"
#include "core/device_info.h"
#include "core/install_markers.h"
#include "core/prefs.h"
#include "ui/icons.h"
#include "ui/popup.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace Pages {
namespace {

Catalog &catalog() {
    static Catalog c = Catalog::load(MmPaths::configDir());
    return c;
}

QString gray(const QString &t) { return QString("<span style='color:%1'>%2</span>").arg(pal().textSecondary.name(), t); }

// Decorative header art: star / dollar with sparkles.
class Art : public QWidget {
public:
    enum Kind { PremiumStar, GoldStar, Dollar };
    Art(Kind k, QWidget *parent = nullptr) : QWidget(parent), m_kind(k) { setFixedHeight(120); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, 60);
        QColor c1 = m_kind == PremiumStar ? QColor("#6e9cff") : m_kind == GoldStar ? QColor("#ffd54a") : QColor("#b9b2f6");
        QColor c2 = m_kind == PremiumStar ? QColor("#c06bff") : m_kind == GoldStar ? QColor("#ff9f1a") : QColor("#8d82f0");
        for (int i = 0; i < 14; ++i) {  // sparkles
            const qreal a = i * 0.9 + 0.4, r = 62 + (i % 3) * 14;
            p.setPen(Qt::NoPen);
            p.setBrush(i % 2 ? c1 : c2);
            p.drawEllipse(c + QPointF(std::cos(a) * r * 1.5, std::sin(a) * r * 0.6), 2.2 + (i % 3), 2.2 + (i % 3));
        }
        QLinearGradient g(c - QPointF(40, 40), c + QPointF(40, 40));
        g.setColorAt(0, c1);
        g.setColorAt(1, c2);
        p.setBrush(g);
        if (m_kind == Dollar) {
            p.drawEllipse(c, 40, 40);
            p.setPen(Qt::white);
            QFont f = font();
            f.setBold(true);
            f.setPixelSize(44);
            p.setFont(f);
            p.drawText(QRectF(c.x() - 40, c.y() - 40, 80, 80), Qt::AlignCenter, "$");
        } else {
            QPainterPath path;
            for (int i = 0; i < 10; ++i) {
                const qreal r = i % 2 ? 19 : 44, a = -3.14159265358979323846 / 2 + i * 3.14159265358979323846 / 5 + 0.25;
                const QPointF pt = c + QPointF(std::cos(a) * r, std::sin(a) * r);
                if (i == 0) path.moveTo(pt); else path.lineTo(pt);
            }
            path.closeSubpath();
            p.setPen(QPen(g, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
    }
private:
    Kind m_kind;
};

QLabel *title(const QString &t, int deltaPt = 3) {
    auto *l = new QLabel(t);
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + deltaPt);
    l->setFont(f);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QLabel *sub(const QString &t) {
    auto *l = new QLabel(t);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    l->setContentsMargins(26, 4, 26, 12);
    l->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    return l;
}

// Coloured feature row used by Premium and Business pages.
QWidget *feature(const QString &icon, const QString &color, const QString &name, const QString &desc, bool chevron) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(16, 8, 16, 8);
    l->setSpacing(14);
    auto *ic = new QLabel;
    ic->setFixedSize(34, 34);
    QPixmap pm(68, 68);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(color));
        p.drawRoundedRect(QRectF(0, 0, 34, 34), 9, 9);
        p.drawPixmap(7, 7, Icons::pixmap(icon, Qt::white, 20));
    }
    ic->setPixmap(pm);
    auto *tx = new QLabel(QString("<b>%1</b><br><span style='color:%2'>%3</span>").arg(name, pal().textSecondary.name(), desc));
    tx->setWordWrap(true);
    l->addWidget(ic, 0, Qt::AlignTop);
    l->addWidget(tx, 1);
    if (chevron) {
        auto *ch = new QLabel;
        ch->setPixmap(Icons::pixmap("chevron", pal().textSecondary, 18));
        l->addWidget(ch);
    }
    return w;
}

}  // namespace

void infoDialog(PopupHost *h, const QString &titleText, const QString &text) {
    h->dialog(titleText, text, {{"OK", {}, false}});
}

void startPayment(PopupHost *h, const PaymentRequest &req) {
    PaymentManager::instance().pay(req, h, [h](const PaymentResult &r) {
        if (!r.ok) { h->dialog("Ошибка оплаты", r.error, {{"OK", {}, false}}); return; }
        QDesktopServices::openUrl(r.checkoutUrl);
        h->closeAll();
    });
}

// ---------------------------------------------------------------- Premium / Stars / Business
void premium(PopupHost *h, bool push) {
    auto *pg = new Page(h);
    pg->widget(new Art(Art::PremiumStar));
    pg->widget(title("MINI max Premium"));
    pg->widget(sub("Больше свободы и десятки эксклюзивных функций с подпиской MINI max Premium."));
    pg->divider();
    auto *sel = new int(0);
    const Catalog &cat = catalog();
    for (int i = 0; i < cat.plans.size(); ++i) {
        const PremiumPlan &pl = cat.plans[i];
        auto *rb = new QRadioButton;
        rb->setChecked(i == 0);
        rb->setStyleSheet("QRadioButton { padding: 6px 22px; }");
        QString perMonth = cat.money(pl.priceMinor / qMax(1, pl.months));
        rb->setText(QString("%1%2").arg(pl.title, pl.discount ? QString("   −%1%").arg(pl.discount) : QString()));
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 22, 0);
        hl->addWidget(rb, 1);
        auto *pr = new QLabel(perMonth + " в месяц");
        pr->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
        hl->addWidget(pr);
        pg->widget(row);
        QObject::connect(rb, &QRadioButton::toggled, pg, [sel, i](bool on) { if (on) *sel = i; });
    }
    pg->divider();
    struct F { const char *icon, *color, *name, *desc; };
    static const F feats[] = {
        {"video", "#f5882b", "Истории", "Публикация без лимитов, приоритетный показ, режим инкогнито и многое другое."},
        {"file", "#f4852a", "Безлимитное хранилище", "Загрузка файлов размером до 4 ГБ."},
        {"plus", "#f26a3d", "Удвоенные лимиты", "До 1000 каналов, 30 папок, 10 закреплённых чатов, 20 публичных ссылок."},
        {"shop", "#ee5c58", "MINI max для бизнеса", "Дополнительные возможности для владельцев бизнеса."},
        {"clock", "#e8566e", "Время захода", "Просмотр времени захода и прочтения при скрытии своих."},
        {"mic", "#e0508a", "Распознавание голоса", "Мгновенная расшифровка входящих голосовых сообщений."},
        {"smile", "#d44ba0", "Эмодзи-статусы", "Один из тысяч эмодзи в качестве статуса рядом с именем."},
        {"bookmark", "#c14fc0", "Теги в Избранном", "Добавление тегов к сообщениям в Избранном."},
        {"edit", "#a95ee0", "Цвета имени и профиля", "Выбор цвета и фонового эмодзи для профиля."},
        {"photo", "#8a6ff0", "Обои для Вас и собеседника", "Обои в личном чате для себя и собеседника."},
        {"star", "#7a78f5", "Значок подписчика", "Эксклюзивный значок Premium рядом с именем."},
        {"lock", "#6a82f7", "Ограничение сообщений", "Ограничить входящие сообщения или сделать их платными."},
        {"chat", "#5a8ef8", "Управление чатами", "Папка по умолчанию, автоархивация и скрытие новых чатов."},
        {"bell-off", "#4a96f9", "Отключение рекламы", "Скрытие рекламы в публичных каналах."},
        {"smile", "#3a9ff9", "Реакции без границ", "Несколько реакций на сообщение и неограниченный выбор эмодзи."},
        {"sticker", "#2ab0e8", "Эксклюзивные стикеры", "Большие стикеры с уникальной анимацией."},
    };
    for (const F &f : feats) pg->widget(feature(f.icon, f.color, f.name, f.desc, true));
    auto label = [sel, &cat] { const PremiumPlan &pl = cat.plans[*sel]; return QString("Подключить за %1 в месяц").arg(cat.money(pl.priceMinor / qMax(1, pl.months))); };
    pg->footerButton(label(), [h, sel] {
        const PremiumPlan &pl = catalog().plans[*sel];
        PaymentRequest r;
        r.kind = "premium"; r.item = pl.id; r.amountMinor = pl.priceMinor; r.currency = catalog().currency;
        startPayment(h, r);
    }, true);
    if (push) h->push(pg, {}); else h->open(pg, {});
}

void stars(PopupHost *h, bool push) {
    auto *pg = new Page(h);
    pg->widget(new Art(Art::GoldStar));
    pg->widget(title("Звёзды MINI max"));
    pg->widget(sub("Звёзды нужны для оплаты контента и услуг в мини-приложениях и для подарков."));
    auto *btn = new QPushButton("＋  Пополнить баланс");
    btn->setObjectName("primary");
    btn->setCursor(Qt::PointingHandCursor);
    auto *bw = new QWidget;
    auto *bl = new QHBoxLayout(bw);
    bl->setContentsMargins(60, 4, 60, 8);
    bl->addWidget(btn);
    pg->widget(bw);
    QObject::connect(btn, &QPushButton::clicked, pg, [h] {
        const Catalog &c = catalog();
        PaymentRequest r;
        r.kind = "stars"; r.currency = c.currency;
        if (!c.starPacks.isEmpty()) { r.item = QString::number(c.starPacks.first().stars); r.amountMinor = c.starPacks.first().priceMinor; }
        startPayment(h, r);
    });
    auto *bal = new QLabel(QString("Ваш баланс: ⭐ %1").arg(AppContext::i().stars));
    bal->setAlignment(Qt::AlignCenter);
    bal->setContentsMargins(0, 0, 0, 14);
    pg->widget(bal);
    pg->divider();
    pg->row("trending", "Открыть статистику", {}, [h] { infoDialog(h, "Статистика", "Статистика звёзд появится после подключения сервера."); });
    pg->row("gift", "Подарить звёзды друзьям", {}, [h] { giftPeople(h); });
    pg->row("shop", "Заработать звёзды с мини-приложениями", {}, [] { QDesktopServices::openUrl(QUrl(MM_WEBSITE)); });
    pg->divider();
    pg->section("Все операции");
    auto *empty = new QLabel("Операций пока нет");
    empty->setAlignment(Qt::AlignCenter);
    empty->setContentsMargins(0, 20, 0, 40);
    empty->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    pg->widget(empty);
    if (push) h->push(pg, {}); else h->open(pg, {});
}

void business(PopupHost *h) {
    auto *pg = new Page(h);
    pg->widget(new Art(Art::Dollar));
    pg->widget(title("MINI max для бизнеса"));
    pg->widget(sub("Дополнительные функции, с которыми обычный аккаунт можно превратить в бизнес-профиль."));
    pg->divider();
    struct F { const char *icon, *color, *name, *desc; };
    static const F feats[] = {
        {"pin", "#f5882b", "Адрес", "Геопозиция и адрес бизнеса в Вашем профиле."},
        {"clock", "#ee5c58", "Часы работы", "График работы по всем дням недели."},
        {"reply", "#e0508a", "Быстрые ответы", "Заготовки ответов с медиафайлами и форматированием."},
        {"smile", "#c14fc0", "Приветствия", "Автоматические сообщения для новых клиентов."},
        {"moon", "#a95ee0", "«Нет на месте»", "Автоматические ответы в нерабочее время."},
        {"link", "#7a78f5", "Ссылки на чат", "Ссылки для открытия чата с Вами с подстановкой текста."},
        {"chat", "#4a96f9", "Вид нового чата", "Ваше сообщение и стикер на странице пустого чата."},
        {"bot", "#2fb78a", "Чат-боты", "Подключение сторонних ботов для взаимодействия с клиентами."},
        {"folder", "#4cc04a", "Теги для чатов", "Названия папок рядом с каждым чатом в списке."},
    };
    for (const F &f : feats) pg->widget(feature(f.icon, f.color, f.name, f.desc, true));
    pg->footerButton(QString("Подключить за %1 в месяц").arg(catalog().money(catalog().businessMonthlyMinor)), [h] {
        PaymentRequest r;
        r.kind = "business"; r.item = "business_1m"; r.amountMinor = catalog().businessMonthlyMinor; r.currency = catalog().currency;
        startPayment(h, r);
    }, true);
    h->push(pg, {});
}

// ---------------------------------------------------------------- gifts
void giftPeople(PopupHost *h) {
    auto *pg = new Page(h);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Поиск людей для отправки подарка...");
    auto *sw = new QWidget;
    auto *sl = new QHBoxLayout(sw);
    sl->setContentsMargins(16, 8, 16, 8);
    sl->addWidget(search);
    pg->widget(sw);
    auto *self = pg->row("account", QString("%1 — купить подарок себе").arg(AppContext::i().session.displayName), {}, [h] {
        giftShop(h, AppContext::i().session.displayName, AppContext::i().session.login);
    });
    (void)self;
    pg->section("Частые контакты");
    auto *chats = AppContext::i().chats;
    int n = 0;
    for (int i = 0; chats && i < chats->rowCount(); ++i) {
        const Chat *c = chats->chat(i);
        if (c->kind != ChatKind::Private) continue;
        ++n;
        const QString name = c->title, login = c->username;
        pg->row("person", name, {}, [h, name, login] { giftShop(h, name, login); });
    }
    if (!n) pg->note("Здесь появятся люди, с которыми Вы часто общаетесь.");
    h->push(pg, "Отправить подарок");
}

void giftShop(PopupHost *h, const QString &name, const QString &login) {
    auto *pg = new Page(h);
    auto *av = new QLabel;
    av->setPixmap(avatarPixmap(64, name, qHash(name)));
    av->setAlignment(Qt::AlignCenter);
    av->setContentsMargins(0, 14, 0, 6);
    pg->widget(av);
    pg->widget(title("Подарить Premium"));
    pg->widget(sub(QString("Подарите <b>%1</b> доступ к эксклюзивным функциям.").arg(name.toHtmlEscaped())));
    // premium tiers
    auto *tiers = new QWidget;
    auto *tl = new QHBoxLayout(tiers);
    tl->setContentsMargins(12, 0, 12, 12);
    const Catalog &cat = catalog();
    for (const PremiumPlan &pl : cat.plans) {
        if (pl.months == 1) continue;
        auto *b = new QPushButton(QString("%1 мес.\nPremium\n%2").arg(pl.months).arg(cat.money(pl.priceMinor)));
        b->setMinimumHeight(110);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QPushButton { background:%1; border-radius:10px; font-weight:bold; color:%2; } QPushButton:hover { background:%3; }")
                             .arg(pal().sectionBg.name(), pal().text.name(), pal().itemHover.name()));
        QObject::connect(b, &QPushButton::clicked, pg, [h, pl, login] {
            PaymentRequest r;
            r.kind = "premium"; r.item = pl.id; r.amountMinor = pl.priceMinor; r.currency = catalog().currency; r.recipient = login;
            startPayment(h, r);
        });
        tl->addWidget(b);
    }
    pg->widget(tiers);
    pg->widget(title("Отправить подарок"));
    pg->widget(sub("Подарки можно хранить в профиле или обменять на звёзды."));
    auto *grid = new QWidget;
    auto *gl = new QGridLayout(grid);
    gl->setContentsMargins(12, 0, 12, 12);
    gl->setSpacing(8);
    int i = 0;
    for (const GiftItem &g : cat.gifts) {
        auto *b = new QPushButton(QString("%1\n\n⭐ %2").arg(g.emoji).arg(g.stars));
        b->setMinimumHeight(110);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(g.name);
        b->setStyleSheet(QString("QPushButton { background:%1; border-radius:10px; font-size:26px; color:%2; } QPushButton:hover { background:%3; }")
                             .arg(pal().sectionBg.name(), "#ffb629", pal().itemHover.name()));
        QObject::connect(b, &QPushButton::clicked, pg, [h, g, name, login] {
            h->dialog("Отправить подарок", QString("%1 %2 для %3 за ⭐ %4?").arg(g.emoji, g.name, name.toHtmlEscaped()).arg(g.stars),
                      {{"Отмена", {}, false}, {"Отправить", [h, g, login] {
                          PaymentRequest r;
                          r.kind = "gift"; r.item = g.id; r.recipient = login; r.currency = "XTR"; r.amountMinor = g.stars;
                          startPayment(h, r);
                      }, false}});
        });
        gl->addWidget(b, i / 3, i % 3);
        ++i;
    }
    pg->widget(grid);
    h->push(pg, {});
}

// ---------------------------------------------------------------- profile
void myProfile(PopupHost *h) {
    auto *pg = new Page(h);
    const Session &s = AppContext::i().session;
    auto *av = new QLabel;
    av->setPixmap(avatarPixmap(96, s.displayName, qHash(s.login)));
    pg->footerButton("Сменить фото профиля", [h] {
        const QString path = QFileDialog::getOpenFileName(nullptr, "Фото профиля", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (path.isEmpty()) return;
        if (Engine *eng = AppContext::i().engine) eng->setMyAvatar(path);
    });
    pg->footerButton("Удалить фото профиля", [] {
        if (Engine *eng = AppContext::i().engine) eng->clearMyAvatar();
    });
    av->setAlignment(Qt::AlignCenter);
    av->setContentsMargins(0, 16, 0, 6);
    pg->widget(av);
    pg->widget(title(s.displayName, 2));
    auto *st = new QLabel("в сети");
    st->setAlignment(Qt::AlignCenter);
    st->setContentsMargins(0, 0, 0, 14);
    st->setStyleSheet(QString("color:%1;").arg(pal().link.name()));
    pg->widget(st);
    pg->divider();
    pg->row("", "@" + s.login, "Имя пользователя");
    if (!s.bio.isEmpty()) pg->row("", s.bio, "О себе");
    pg->divider();
    auto *empty = new QLabel("Здесь будут показаны Ваши истории.");
    empty->setAlignment(Qt::AlignCenter);
    empty->setContentsMargins(0, 24, 0, 30);
    empty->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    pg->widget(empty);
    h->open(pg, {});
}

void autoDeleteTimer(PopupHost *h, int row) {
    static const struct { const char *label; int sec; } opts[] = {
        {"Отключено", 0}, {"1 день", 86400}, {"2 дня", 172800}, {"3 дня", 259200}, {"4 дня", 345600}, {"5 дней", 432000},
        {"6 дней", 518400}, {"1 неделя", 604800}, {"2 недели", 1209600}, {"3 недели", 1814400}, {"1 месяц", 2592000},
        {"2 месяца", 5184000}, {"3 месяца", 7776000}, {"6 месяцев", 15552000}, {"1 год", 31536000}};
    Chat *c = AppContext::i().chats->chat(row);
    if (!c) return;
    QStringList labels;
    int cur = 0;
    for (int i = 0; i < int(sizeof(opts) / sizeof(opts[0])); ++i) { labels << opts[i].label; if (opts[i].sec == c->autoDeleteSec) cur = i; }
    h->choice("Автоудаление сообщений", labels, cur, [row](int i) {
        if (Chat *ch = AppContext::i().chats->chat(row)) {
            ch->autoDeleteSec = opts[i].sec;
            AppContext::i().chats->purgeOlderThan(row, opts[i].sec);
            AppContext::i().chats->touch(row);
        }
    });
}

void chatProfile(PopupHost *h, int row) {
    ChatsModel *model = AppContext::i().chats;
    Chat *c = model->chat(row);
    if (!c) return;
    auto *pg = new Page(h);
    auto *av = new QLabel;
    av->setPixmap(avatarPixmap(96, c->title, c->id, c->kind == ChatKind::Saved ? "bookmark" : QString()));
    if (c->kind == ChatKind::Group || c->kind == ChatKind::Channel) {
        const qint64 chatId = c->id;
        pg->footerButton("Сменить аватар группы/канала", [h, chatId] {
            const QString path = QFileDialog::getOpenFileName(nullptr, "Аватар", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
            if (path.isEmpty()) return;
            if (Engine *eng = AppContext::i().engine) eng->setChatAvatar(chatId, path);
            else infoDialog(h, "Аватар", "Нет активного сеанса.");
        });
    }
    av->setAlignment(Qt::AlignCenter);
    av->setContentsMargins(0, 16, 0, 6);
    pg->widget(av);
    pg->widget(title(c->title + (c->verified ? "  ✔" : ""), 2));
    QString status;
    switch (c->kind) {
    case ChatKind::Channel: status = QString("%1 подписчиков").arg(c->members); break;
    case ChatKind::Group: status = QString("%1 участников").arg(c->members); break;
    case ChatKind::Bot: status = "бот"; break;
    case ChatKind::Saved: status = "личные заметки"; break;
    default: status = "был(а) недавно";
    }
    auto *st = new QLabel(status);
    st->setAlignment(Qt::AlignCenter);
    st->setContentsMargins(0, 0, 0, 12);
    st->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    pg->widget(st);

    auto *bar = new QWidget;
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(12, 0, 12, 12);
    auto tile = [&](const QString &icon, const QString &text, std::function<void()> cb) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setIcon(Icons::icon(icon, pal().text, 22));
        b->setIconSize({22, 22});
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(56);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setStyleSheet(QString("QToolButton { background:%1; border-radius:8px; font-size:11px; } QToolButton:hover { background:%2; }")
                             .arg(pal().sectionBg.name(), pal().itemHover.name()));
        QObject::connect(b, &QToolButton::clicked, b, [cb] { cb(); });
        bl->addWidget(b);
        return b;
    };
    if (c->kind == ChatKind::Private || c->kind == ChatKind::Bot)
        tile("chat", "Чат", [h, row] { h->closeAll(); if (AppContext::i().openChatRow) AppContext::i().openChatRow(row); });
    auto *mute = tile(c->muted ? "bell-off" : "bell", c->muted ? "Включить" : "Звук", [h, row, model] {
        model->setMuted(row, !model->chat(row)->muted);
        chatProfile(h, row);
    });
    (void)mute;
    if (c->kind == ChatKind::Private) tile("phone", "Звонок", [h] { infoDialog(h, "Звонки", "Голосовые и видеозвонки появятся после подключения сервера сигналинга."); });
    if (c->kind == ChatKind::Group || c->kind == ChatKind::Channel)
        tile("archive", c->archived ? "Из архива" : "В архив", [h, row, model] { model->setArchived(row, !model->chat(row)->archived); h->closeAll(); });
    auto *more = tile("dots", "Ещё", [] {});
    auto *menu = new QMenu(more);
    if (c->kind != ChatKind::Saved) menu->addAction(Icons::icon("gift", pal().textSecondary, 18), "Отправить подарок", [h, c] { giftShop(h, c->title, c->username); });
    menu->addAction(Icons::icon("timer", pal().textSecondary, 18), "Автоудаление", [h, row] { autoDeleteTimer(h, row); });
    menu->addAction(Icons::icon("archive", pal().textSecondary, 18), "Экспорт истории чата", [h] { infoDialog(h, "Экспорт", "Экспорт истории будет сохранять переписку в зашифрованный файл на этом компьютере (в разработке)."); });
    QMenu *folders = menu->addMenu(Icons::icon("folder", pal().textSecondary, 18), "Добавить в папку");
    const QStringList custom = Prefs::instance().get("folders/list").toStringList();
    for (const QString &f : custom) {
        const QString id = f.section('|', 0, 0), nm = f.section('|', 1);
        QAction *a = folders->addAction(nm);
        a->setCheckable(true);
        a->setChecked(c->folders.contains(id));
        QObject::connect(a, &QAction::triggered, a, [model, row, id](bool on) {
            Chat *ch = model->chat(row);
            if (on && !ch->folders.contains(id)) ch->folders << id; else if (!on) ch->folders.removeAll(id);
            model->touch(row);
        });
    }
    if (custom.isEmpty()) folders->addAction("Папок нет")->setEnabled(false);
    menu->addSeparator();
    QAction *del = menu->addAction(Icons::icon("trash", pal().danger, 18), c->kind == ChatKind::Channel ? "Покинуть канал" : c->kind == ChatKind::Group ? "Покинуть группу" : "Удалить чат");
    QObject::connect(del, &QAction::triggered, del, [h, row, model] {
        h->dialog("Удалить чат?", "Переписка будет удалена с этого компьютера.", {{"Отмена", {}, false}, {"Удалить", [h, row, model] { model->removeChat(row); h->closeAll(); }, true}});
    });
    more->setMenu(menu);
    more->setPopupMode(QToolButton::InstantPopup);
    pg->widget(bar);
    pg->divider();
    if (!c->username.isEmpty()) pg->row("", "@" + c->username, "Имя пользователя");
    if (!c->about.isEmpty()) pg->row("", c->about.left(60), "О себе");
    if (c->kind == ChatKind::Saved) pg->note("Сообщения в Избранном хранятся только на этом компьютере в зашифрованном виде.");
    h->open(pg, {});
}

// ---------------------------------------------------------------- settings
namespace {
void choiceRow(Page *pg, const QString &text, const QString &key, const QStringList &opts, int def) {
    auto *row = pg->row("", text, opts[Prefs::instance().get(key, def).toInt()]);
    QObject::connect(row, &ClickRow::clicked, pg, [pg, text, key, opts, def, row] {
        pg->host()->choice(text, opts, Prefs::instance().get(key, def).toInt(), [key, opts, row](int i) { Prefs::instance().set(key, i); row->setValue(opts[i]); });
    });
}

void accountPage(PopupHost *h) {
    auto *pg = new Page(h);
    Session &s = AppContext::i().session;
    auto *av = new QLabel;
    av->setPixmap(avatarPixmap(100, s.displayName, qHash(s.login)));
    av->setAlignment(Qt::AlignCenter);
    av->setContentsMargins(0, 8, 0, 8);
    pg->widget(av);
    auto *bio = new QLineEdit(s.bio);
    bio->setPlaceholderText("О себе");
    bio->setMaxLength(70);
    auto *bw = new QWidget;
    auto *bl = new QHBoxLayout(bw);
    bl->setContentsMargins(16, 4, 16, 8);
    bl->addWidget(bio);
    pg->widget(bw);
    QObject::connect(bio, &QLineEdit::editingFinished, bio, [bio] {
        Session &ss = AppContext::i().session;
        if (ss.bio == bio->text()) return;
        ss.bio = bio->text();
        Prefs::instance().set("profile/bio", ss.bio);
        AppContext::i().api->post("/api/profile/update", {{"bio", ss.bio}}, bio, [](const ApiResult &) {});
    });
    pg->note("Любые подробности, например: возраст, род занятий или город.");
    pg->row("account", "Имя", s.displayName, [h] {
        h->input("Имя", "Отображаемое имя", AppContext::i().session.displayName, "Сохранить", [](const QString &name) {
            if (name.trimmed().isEmpty()) return;
            AppContext::i().session.displayName = name.trimmed();
            if (Engine *eng = AppContext::i().engine)
                eng->updateProfile({{"display_name", name.trimmed()}});
            else
                AppContext::i().api->post("/api/profile/update", {{"display_name", name.trimmed()}}, nullptr, [](const ApiResult &) {});
        });
    });
    pg->row("", "Имя пользователя", "@" + s.login);
    pg->row("edit", "Цвет имени", Prefs::instance().get("profile/name_color", "#4a96f9").toString(), [h] {
        static const QStringList colors = {"#4a96f9", "#2fb78a", "#e0508a", "#a95ee0", "#f5a623", "#e74c3c", "#1abc9c", "#9b59b6"};
        h->choice("Цвет имени", colors, 0, [](int i) {
            static const QStringList colors = {"#4a96f9", "#2fb78a", "#e0508a", "#a95ee0", "#f5a623", "#e74c3c", "#1abc9c", "#9b59b6"};
            if (i >= 0 && i < colors.size()) Prefs::instance().set("profile/name_color", colors[i]);
        });
    });
    pg->row("calendar", "День рождения", Prefs::instance().get("profile/birthday", "не указан").toString(), [h] {
        h->input("День рождения", "ДД.ММ.ГГГГ", Prefs::instance().get("profile/birthday").toString(), "Сохранить", [](const QString &v) {
            Prefs::instance().set("profile/birthday", v.trimmed());
            if (Engine *eng = AppContext::i().engine)
                eng->updateProfile({{"birthday", v.trimmed()}});
        });
    });
    pg->note("С помощью имени пользователя другие люди смогут связаться с Вами, не зная Вашего пароля.");
    pg->row("edit", "Цвет имени", s.displayName.left(10));
    pg->row("gift", "День рождения", "Добавить");
    pg->divider();
    auto *out = pg->row("close", "Выйти из аккаунта", {}, [h] {
        h->dialog("Выйти из аккаунта?", "Локальные сообщения останутся на этом компьютере в зашифрованном виде.",
                  {{"Отмена", {}, false}, {"Выйти", [h] { if (AppContext::i().logout) AppContext::i().logout(); }, true}});
    });
    out->setDanger(true);
    h->push(pg, "Информация");
}

void notificationsPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->section("Общие настройки");
    pg->toggle("bell", "Уведомления на рабочем столе", "notif/desktop", true);
    pg->toggle("devices", "Анимация иконки на панели задач", "notif/taskbar", true);
    pg->toggle("volume", "Звук", "notif/sound", true);
    pg->section("Громкость");
    pg->slider(0, 100, Prefs::instance().get("notif/volume", 100).toInt(), "%", [](int v) { Prefs::instance().set("notif/volume", v); });
    pg->divider();
    pg->section("Уведомления из чатов");
    pg->toggle("person", "Личные чаты", "notif/private", true);
    pg->toggle("group", "Группы", "notif/groups", true);
    pg->toggle("channel", "Каналы", "notif/channels", true);
    pg->toggle("smile", "Реакции", "notif/reactions", true);
    pg->divider();
    pg->section("События");
    pg->toggle("person", "Контакт присоединился", "notif/joined", true);
    pg->toggle("pin", "Закреплённые сообщения", "notif/pinned", true);
    pg->divider();
    pg->section("Звонки");
    pg->toggle("phone", "Приём звонков на этом устройстве", "notif/calls", true);
    pg->divider();
    pg->section("Счётчик непрочитанных сообщений");
    pg->toggle("", "Учитывать чаты без звука", "notif/count_muted", true);
    pg->toggle("", "Учитывать чаты без звука в папках", "notif/count_muted_folders", true);
    pg->toggle("", "Считать сообщения вместо чатов", "notif/count_messages", true);
    h->push(pg, "Уведомления и звуки");
}

void privacyPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->section("Безопасность");
    pg->row("lock", "Сквозное шифрование", "Вкл.", [h] {
        infoDialog(h, "Сквозное шифрование", "Ключи шифрования создаются и хранятся только на Вашем устройстве. Сервер не видит содержимое переписки. Локальная база сообщений зашифрована ключом, полученным из Вашего пароля (Argon2id).");
    });
    auto *ad = pg->row("timer", "Автоудаление сообщений", "Выкл.");
    QObject::connect(ad, &ClickRow::clicked, pg, [h, ad] {
        static const struct { const char *l; int s; } o[] = {{"Выкл.", 0}, {"1 день", 86400}, {"1 неделя", 604800}, {"1 месяц", 2592000}, {"3 месяца", 7776000}, {"1 год", 31536000}};
        QStringList labels; int cur = 0; const int now = Prefs::instance().get("privacy/auto_delete", 0).toInt();
        for (int i = 0; i < 6; ++i) { labels << o[i].l; if (o[i].s == now) cur = i; }
        h->choice("Автоудаление сообщений", labels, cur, [ad](int i) { Prefs::instance().set("privacy/auto_delete", o[i].s); ad->setValue(o[i].l);
            ChatsModel *m = AppContext::i().chats; for (int r = 0; r < m->rowCount(); ++r) m->purgeOlderThan(r, o[i].s); });
    });
    pg->row("block", "Заблокированные пользователи", "0", [h] { infoDialog(h, "Заблокированные пользователи", "Вы никого не блокировали."); });
    pg->row("devices", "Активные сеансы", {}, [h] {
        auto *p2 = new Page(h);
        const DeviceInfo d = DeviceInfo::collect();
        p2->section("Это устройство");
        p2->row("devices", QString("%1 — %2").arg(MM_APP_NAME, d.os), "онлайн");
        p2->note(QString("Имя компьютера: %1. Версия %2.").arg(d.hostname, MM_VERSION_STR));
        p2->divider();
        auto *r = p2->row("close", "Завершить все остальные сеансы", {}, [h] {
            AppContext::i().api->post("/api/sessions/terminate_others", {}, h, [h](const ApiResult &res) {
                infoDialog(h, res.ok ? "Готово" : "Не удалось", res.ok ? "Остальные сеансы завершены." : res.error);
            });
        });
        r->setDanger(true);
        h->push(p2, "Активные сеансы");
    });
    pg->note("Управление сеансами на всех подключённых устройствах.");
    pg->section("Конфиденциальность");
    const QStringList who = {"Все", "Мои контакты", "Никто"};
    choiceRow(pg, "Время захода", "privacy/last_seen", who, 2);
    choiceRow(pg, "Фотографии профиля", "privacy/photos", who, 1);
    choiceRow(pg, "Пересылка сообщений", "privacy/forward", who, 2);
    choiceRow(pg, "Звонки", "privacy/calls", who, 1);
    choiceRow(pg, "Голосовые сообщения", "privacy/voice", who, 0);
    choiceRow(pg, "Сообщения", "privacy/messages", who, 0);
    choiceRow(pg, "День рождения", "privacy/birthday", who, 1);
    choiceRow(pg, "Подарки", "privacy/gifts", who, 0);
    choiceRow(pg, "О себе", "privacy/bio", who, 1);
    choiceRow(pg, "Приглашения", "privacy/invites", who, 1);
    pg->divider();
    pg->section("Частые контакты");
    pg->toggle("", "Подсказка людей при поиске", "privacy/suggest", true);
    pg->note("Показывать пользователей, которым Вы часто пишете, вверху раздела поиска.");
    pg->section("Удаление аккаунта");
    choiceRow(pg, "При неактивности…", "privacy/delete_after", {"1 месяц", "3 месяца", "6 месяцев", "12 месяцев"}, 3);
    h->push(pg, "Конфиденциальность");
}

void chatSettingsPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->section("Темы");
    auto *themes = new QWidget;
    auto *tl = new QHBoxLayout(themes);
    tl->setContentsMargins(18, 4, 18, 8);
    struct T { const char *name, *mode; QColor a, b; };
    const T ts[] = {{"Классика", "light", "#a9d98a", "#e7f7c9"}, {"Дневная", "light", "#78c0ee", "#bde3f8"},
                    {"Цветная", "dark", "#5b6e82", "#2b5278"}, {"Ночная", "dark", "#4e5a68", "#3a4553"}};
    for (const T &t : ts) {
        auto *b = new QPushButton(t.name);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(70);
        b->setStyleSheet(QString("QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:1 %2); border-radius:8px; padding-top:44px; color:%3; }")
                             .arg(t.a.name(), t.b.name(), pal().text.name()));
        QString mode = t.mode;
        QObject::connect(b, &QPushButton::clicked, b, [mode] { Prefs::instance().set("ui/theme", mode); ThemeManager::instance().setMode(mode); });
        tl->addWidget(b);
    }
    pg->widget(themes);
    auto *acc = new QWidget;
    auto *al = new QHBoxLayout(acc);
    al->setContentsMargins(18, 4, 18, 12);
    for (const char *col : {"#5288c1", "#40b5f2", "#4f9a56", "#c0628f", "#c2813a", "#9a79d2", "#c85a55", "#6d7f9a", "#c4a34a"}) {
        auto *b = new QPushButton;
        b->setFixedSize(26, 26);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QPushButton { background:%1; border-radius:13px; }").arg(col));
        QString colName = col;
        QObject::connect(b, &QPushButton::clicked, b, [colName] { Prefs::instance().set("ui/accent", colName); ThemeManager::instance().setAccent(QColor(colName)); });
        al->addWidget(b);
    }
    pg->widget(acc);
    pg->divider();
    pg->section("Настройки темы");
    pg->row("sliders", "Смена темы на ночную", "Системная", [h] { infoDialog(h, "Смена темы", "Выберите тему выше. Режим «Системная» можно задать в configs/client.toml ([ui] theme = \"system\")."); });
    pg->divider();
    pg->section("Обои для чатов");
    auto *wp = pg->row("photo", "Выбрать файл", {}, [] {
        const QString f = QFileDialog::getOpenFileName(nullptr, "Выбор файлов", QDir::homePath(), "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!f.isEmpty()) Prefs::instance().set("chat/wallpaper", f);
    });
    (void)wp;
    pg->row("close", "Сбросить обои", {}, [] { Prefs::instance().set("chat/wallpaper", QString()); });
    pg->check("Выравнивание для широких экранов", "chat/wide_align", true);
    pg->divider();
    pg->section("Стикеры и эмодзи");
    pg->check("Крупные эмодзи", "chat/big_emoji", true);
    pg->check("Автозамена эмодзи", "chat/emoji_replace", true);
    pg->check("Подсказка эмодзи", "chat/emoji_suggest", true);
    pg->check("Предлагать популярные стикеры", "chat/suggest_stickers", true);
    pg->check("Зациклить анимацию", "chat/loop_anim", true);
    pg->divider();
    pg->section("Сообщения");
    pg->radios({"Отправка по Enter", "Отправка по Ctrl+Enter"}, "chat/send_mode", 0);
    pg->spacing(6);
    pg->radios({"Ответ по двойному нажатию", "Реакция по двойному нажатию ❤"}, "chat/double_click", 0);
    pg->check("Кнопка ответа в сообщениях", "chat/reply_btn", true);
    pg->check("Кнопка реакции в сообщениях", "chat/react_btn", true);
    pg->check("Пролистывание до следующего канала", "chat/next_channel", true);
    pg->divider();
    pg->section("Материалы деликатного характера");
    pg->toggle("", "Показывать материалы 18+", "chat/show_18", false);
    pg->note("Не скрывать медиафайлы, предназначенные только для взрослых.");
    pg->row("", "Сочетания клавиш", {}, [h] {
        infoDialog(h, "Сочетания клавиш", "Enter — отправить<br>Shift+Enter — новая строка<br>Esc — закрыть окно<br>Ctrl+F — поиск по чатам");
    });
    h->push(pg, "Настройки чатов");
}

void foldersPage(PopupHost *h) {
    auto *pg = new Page(h);
    const QStringList list = Prefs::instance().get("folders/list").toStringList();
    pg->section("Мои папки");
    for (const QString &f : list) {
        const QString id = f.section('|', 0, 0), nm = f.section('|', 1);
        pg->row("folder", nm, {}, [h, id, nm] {
            h->dialog(nm, "Удалить папку? Чаты останутся на месте.", {{"Отмена", {}, false}, {"Удалить", [h, id] {
                QStringList l = Prefs::instance().get("folders/list").toStringList();
                l.erase(std::remove_if(l.begin(), l.end(), [&](const QString &x) { return x.section('|', 0, 0) == id; }), l.end());
                Prefs::instance().set("folders/list", l);
            }, true}});
        });
    }
    pg->row("plus", "Создать новую папку", {}, [h] {
        h->input("Новая папка", "Название папки", {}, "Создать", [](const QString &name) {
            QStringList l = Prefs::instance().get("folders/list").toStringList();
            l << QString("%1|%2").arg(QString::number(QDateTime::currentMSecsSinceEpoch(), 36), name);
            Prefs::instance().set("folders/list", l);
        });
    });
    pg->note("Папки показываются в левой колонке. Добавить чат в папку можно из его профиля («Ещё»).");
    h->push(pg, "Папки с чатами");
}

void advancedPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->section("Данные");
    pg->row("folder", "Открыть папку данных", {}, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppContext::i().dataDir)); });
    pg->row("trash", "Удалить локальные сообщения", {}, [h] {
        h->dialog("Удалить локальные сообщения?", "Все переписки будут стёрты с этого компьютера. Это нельзя отменить.",
                  {{"Отмена", {}, false}, {"Удалить", [] { AppContext::i().chats->clearAll(); }, true}});
    })->setDanger(true);
    pg->divider();
    pg->section("Приложение");
    pg->row("help", "Версия", MM_VERSION_STR);
    pg->row("globe", "Сервер", AppContext::i().api ? AppContext::i().api->base().toString() : QString());
    pg->row("link", "Сайт разработчика", MM_COMPANY, [] { QDesktopServices::openUrl(QUrl(MM_WEBSITE)); });
    pg->row("globe", "Исходный код (GitHub)", "Wharner-APP/MINI-max", [] { QDesktopServices::openUrl(QUrl(MM_GITHUB)); });
    pg->note(QString("© %1. Файл адреса сервера: configs/connect.ip").arg(MM_COPYRIGHT));
    h->push(pg, "Продвинутые настройки");
}

void batteryPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->section("Анимации");
    pg->toggle("battery", "Анимации интерфейса", "ui/animations", true);
    pg->toggle("sticker", "Анимированные стикеры и эмодзи", "ui/anim_stickers", true);
    pg->toggle("video", "Автовоспроизведение GIF и видео", "ui/autoplay", true);
    pg->note("Отключение анимаций снижает нагрузку на процессор и расход заряда.");
    h->push(pg, "Заряд батареи и анимация");
}

void soundPage(PopupHost *h) {
    auto *pg = new Page(h);
    pg->note("Выбор микрофона, динамиков и камеры появится вместе с голосовыми сообщениями и звонками.");
    h->push(pg, "Звук и камера");
}
}  // namespace

void settings(PopupHost *h) {
    auto *pg = new Page(h);
    const Session &s = AppContext::i().session;
    auto *head = new QWidget;
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(22, 8, 22, 12);
    auto *av = new QLabel;
    av->setPixmap(avatarPixmap(66, s.displayName, qHash(s.login)));
    hl->addWidget(av);
    auto *nm = new QLabel(QString("<b style='font-size:15px'>%1</b><br><span style='color:%2'>@%3</span>").arg(s.displayName.toHtmlEscaped(), pal().textSecondary.name(), s.login.toHtmlEscaped()));
    hl->addWidget(nm, 1);
    pg->widget(head);
    pg->divider();
    pg->row("account", "Мой аккаунт", {}, [h] { accountPage(h); });
    pg->row("bell", "Уведомления и звуки", {}, [h] { notificationsPage(h); });
    pg->row("lock", "Конфиденциальность", {}, [h] { privacyPage(h); });
    pg->row("chat", "Настройки чатов", {}, [h] { chatSettingsPage(h); });
    pg->row("folder", "Папки с чатами", {}, [h] { foldersPage(h); });
    pg->row("sliders", "Продвинутые настройки", {}, [h] { advancedPage(h); });
    pg->row("volume", "Звук и камера", {}, [h] { soundPage(h); });
    pg->row("battery", "Заряд батареи и анимация", {}, [h] { batteryPage(h); });
    pg->row("language", "Язык", "Русский", [h] { infoDialog(h, "Язык", "Сейчас доступен только русский язык."); });
    pg->divider();
    pg->toggle("eye", "Масштаб по умолчанию", "ui/default_scale", true);
    const int sc = Prefs::instance().get("ui/scale", 100).toInt();
    pg->slider(50, 200, sc, "%", [h](int v) {
        const int r = (v + 5) / 10 * 10;
        Prefs::instance().set("ui/scale", r);
        infoDialog(h, "Масштаб", QString("Масштаб %1% будет применён после перезапуска приложения.").arg(r));
    });
    pg->divider();
    pg->row("star", "MINI max Premium", {}, [h] { premium(h); });
    pg->row("star", "Мои звёзды", QString::number(AppContext::i().stars), [h] { stars(h); });
    pg->row("shop", "MINI max для бизнеса", {}, [h] { business(h); });
    pg->row("gift", "Отправить подарок", {}, [h] { giftPeople(h); });
    pg->divider();
    pg->row("help", "Вопросы о MINI max", {}, [] { QDesktopServices::openUrl(QUrl(MM_WEBSITE)); });
    pg->row("bulb", "Возможности MINI max", {}, [] { QDesktopServices::openUrl(QUrl(MM_WEBSITE)); });
    pg->row("chat", "Задать вопрос", {}, [] { QDesktopServices::openUrl(QUrl(MM_WEBSITE)); });
    h->open(pg, "Настройки");
}

void contacts(PopupHost *h) {
    auto *pg = new Page(h);
    pg->note("Введите логин в поле ниже или используйте поиск (Enter) на главном экране.");
    auto *add = new QLineEdit;
    add->setPlaceholderText("логин пользователя");
    pg->widget(add);
    pg->footerButton("Добавить контакт", [h, add] {
        QString login = add->text().trimmed();
        if (login.startsWith('@')) login = login.mid(1);
        if (login.isEmpty()) return;
        if (Engine *eng = AppContext::i().engine) eng->addContact(login);
        else AppContext::i().api->post("/api/contacts/add", {{"login", login}}, h, [h](const ApiResult &r) {
            infoDialog(h, "Контакты", r.ok ? "Добавлен" : r.error);
        });
    });
    h->open(pg, "Контакты");
}
void calls(PopupHost *h) {
    auto *pg = new Page(h);
    pg->note("Журнал звонков пуст. Голосовые и видеозвонки появятся после подключения сервера сигналинга.");
    h->open(pg, "Звонки");
}
void createGroup(PopupHost *h, bool channel) {
    h->input(channel ? "Создать канал" : "Создать группу", "Название", {}, "Создать", [h, channel](const QString &name) {
        if (name.trimmed().isEmpty()) return;
        if (Engine *eng = AppContext::i().engine) {
            eng->createChat(channel ? "channel" : "group", name.trimmed(), false);
        } else {
            AppContext::i().api->post("/api/chats/create", {{"kind", channel ? "channel" : "group"}, {"title", name}}, h, [h](const ApiResult &r) {
                infoDialog(h, r.ok ? "Готово" : "Не удалось создать", r.ok ? "Создано." : r.error);
            });
        }
    });
}

}  // namespace Pages
