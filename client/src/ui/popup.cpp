#include "ui/popup.h"

#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include "core/prefs.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

PopupHost::PopupHost(QWidget *window) : QWidget(window) {
    setFocusPolicy(Qt::StrongFocus);
    window->installEventFilter(this);
    m_card = new QFrame(this);
    m_card->setObjectName("popupCard");
    m_header = new QWidget;
    m_header->setFixedHeight(56);
    m_back = new QToolButton;
    m_back->setIconSize({22, 22});
    m_back->setFixedSize(36, 36);
    m_close = new QToolButton;
    m_close->setIconSize({22, 22});
    m_close->setFixedSize(36, 36);
    m_title = new QLabel;
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.5);
    m_title->setFont(f);
    auto *hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(12, 0, 12, 0);
    hl->addWidget(m_back);
    hl->addWidget(m_title, 1);
    hl->addWidget(m_close);
    m_stack = new QStackedWidget;
    auto *cl = new QVBoxLayout(m_card);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(m_header);
    cl->addWidget(m_stack, 1);
    connect(m_back, &QToolButton::clicked, this, &PopupHost::pop);
    connect(m_close, &QToolButton::clicked, this, &PopupHost::closeAll);
    hide();
}

void PopupHost::showEvent(QShowEvent *) {
    setGeometry(parentWidget()->rect());
    raise();
    setFocus();
    m_dim = 0;
    auto *an = new QVariantAnimation(this);
    an->setDuration(140);
    an->setStartValue(0.0);
    an->setEndValue(1.0);
    connect(an, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { m_dim = v.toReal(); update(); });
    an->start(QAbstractAnimation::DeleteWhenStopped);
}

bool PopupHost::eventFilter(QObject *o, QEvent *e) {
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) { setGeometry(parentWidget()->rect()); relayout(); }
    return QWidget::eventFilter(o, e);
}

void PopupHost::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, int(150 * m_dim)));
}

void PopupHost::mousePressEvent(QMouseEvent *e) {
    if (!m_card->geometry().contains(e->pos())) closeAll();
}

void PopupHost::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { if (m_entries.size() > 1) pop(); else closeAll(); }
    else QWidget::keyPressEvent(e);
}

void PopupHost::show(QWidget *page, const QString &title, int width, bool headless, bool replace) {
    if (replace) {
        while (!m_entries.isEmpty()) { QWidget *w = m_entries.takeLast().page; m_stack->removeWidget(w); w->deleteLater(); }
    }
    m_width = width;
    m_entries.push_back({page, title, headless});
    m_stack->addWidget(page);
    m_stack->setCurrentWidget(page);
    const Palette &c = pal();
    m_card->setStyleSheet(QString("#popupCard { background: %1; border-radius: 10px; } #popupCard QWidget { background: %1; }").arg(c.popupBg.name()));
    m_back->setIcon(Icons::icon("back", c.textSecondary));
    m_close->setIcon(Icons::icon("close", c.textSecondary));
    this->QWidget::show();
    relayout();
}

void PopupHost::open(QWidget *page, const QString &title, int width) { show(page, title, width, false, true); }
void PopupHost::push(QWidget *page, const QString &title) { show(page, title, m_width, false, false); }

void PopupHost::pop() {
    if (m_entries.size() <= 1) { closeAll(); return; }
    QWidget *w = m_entries.takeLast().page;
    m_stack->removeWidget(w);
    w->deleteLater();
    m_stack->setCurrentWidget(m_entries.last().page);
    relayout();
}

void PopupHost::closeAll() {
    while (!m_entries.isEmpty()) { QWidget *w = m_entries.takeLast().page; m_stack->removeWidget(w); w->deleteLater(); }
    hide();
    emit closed();
    if (parentWidget()) parentWidget()->setFocus();
}

void PopupHost::relayout() {
    if (m_entries.isEmpty()) return;
    const Entry &en = m_entries.last();
    m_header->setVisible(!en.headless);
    m_title->setText(en.title);
    m_back->setVisible(m_entries.size() > 1);
    m_close->setVisible(!en.headless);
    const int maxH = qMax(200, height() - 60);
    const int want = en.page->sizeHint().height() + (en.headless ? 0 : 56);
    const int h = qBound(120, want, maxH);
    const int w = qMin(m_width, width() - 20);
    m_card->setGeometry((width() - w) / 2, qMax(20, (height() - h) / 2), w, h);
    m_card->show();
}

void PopupHost::dialog(const QString &title, const QString &text, const QVector<Button> &buttons, int width) {
    auto *pg = new QWidget;
    auto *l = new QVBoxLayout(pg);
    l->setContentsMargins(24, 20, 24, 14);
    l->setSpacing(10);
    auto *t = new QLabel(title);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.5);
    t->setFont(f);
    l->addWidget(t);
    if (!text.isEmpty()) {
        auto *tx = new QLabel(text);
        tx->setWordWrap(true);
        tx->setOpenExternalLinks(true);
        l->addWidget(tx);
    }
    auto *bl = new QHBoxLayout;
    bl->addStretch(1);
    for (const Button &b : buttons) {
        auto *btn = new QPushButton(b.text);
        btn->setObjectName(b.danger ? "danger" : "flat");
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, act = b.action] { closeAll(); if (act) act(); });
        bl->addWidget(btn);
    }
    l->addSpacing(6);
    l->addLayout(bl);
    pg->setMinimumHeight(pg->sizeHint().height());
    show(pg, {}, width, true, true);
}

void PopupHost::choice(const QString &title, const QStringList &options, int current, std::function<void(int)> cb) {
    auto *pg = new QWidget;
    auto *l = new QVBoxLayout(pg);
    l->setContentsMargins(24, 20, 24, 14);
    l->setSpacing(12);
    auto *t = new QLabel(title);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.5);
    t->setFont(f);
    l->addWidget(t);
    auto *sel = new int(current);
    for (int i = 0; i < options.size(); ++i) {
        auto *r = new QRadioButton(options[i]);
        r->setChecked(i == current);
        connect(r, &QRadioButton::toggled, this, [sel, i](bool on) { if (on) *sel = i; });
        l->addWidget(r);
    }
    auto *bl = new QHBoxLayout;
    bl->addStretch(1);
    auto *cancel = new QPushButton("Отмена"), *ok = new QPushButton("Сохранить");
    for (auto *b : {cancel, ok}) { b->setObjectName("flat"); b->setCursor(Qt::PointingHandCursor); bl->addWidget(b); }
    connect(cancel, &QPushButton::clicked, this, &PopupHost::closeAll);
    connect(ok, &QPushButton::clicked, this, [this, sel, cb] { const int v = *sel; delete sel; closeAll(); if (cb) cb(v); });
    l->addSpacing(6);
    l->addLayout(bl);
    show(pg, {}, 360, true, true);
}

void PopupHost::input(const QString &title, const QString &placeholder, const QString &initial, const QString &okText, std::function<void(const QString &)> cb) {
    auto *pg = new QWidget;
    auto *l = new QVBoxLayout(pg);
    l->setContentsMargins(24, 20, 24, 14);
    l->setSpacing(12);
    auto *t = new QLabel(title);
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1.5);
    t->setFont(f);
    l->addWidget(t);
    auto *ed = new QLineEdit(initial);
    ed->setPlaceholderText(placeholder);
    l->addWidget(ed);
    auto *bl = new QHBoxLayout;
    bl->addStretch(1);
    auto *cancel = new QPushButton("Отмена"), *ok = new QPushButton(okText);
    for (auto *b : {cancel, ok}) { b->setObjectName("flat"); b->setCursor(Qt::PointingHandCursor); bl->addWidget(b); }
    connect(cancel, &QPushButton::clicked, this, &PopupHost::closeAll);
    auto accept = [this, ed, cb] { const QString v = ed->text().trimmed(); if (v.isEmpty()) return; closeAll(); if (cb) cb(v); };
    connect(ok, &QPushButton::clicked, this, accept);
    connect(ed, &QLineEdit::returnPressed, this, accept);
    l->addLayout(bl);
    show(pg, {}, 360, true, true);
    ed->setFocus();
}

// ------------------------------------------------------------------------------------------------
Page::Page(PopupHost *host) : QWidget(), m_host(host) {
    m_outer = new QVBoxLayout(this);
    m_outer->setContentsMargins(0, 0, 0, 0);
    m_outer->setSpacing(0);
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(0, 0, 0, 8);
    m_body->setSpacing(0);
    m_scroll->setWidget(m_content);
    m_outer->addWidget(m_scroll, 1);
}

QSize Page::sizeHint() const {
    const int fh = m_footer ? m_footer->sizeHint().height() : 0;
    return {392, m_content->sizeHint().height() + fh};
}

void Page::section(const QString &text) {
    auto *l = new QLabel(text);
    QFont f = font();
    f.setBold(true);
    l->setFont(f);
    l->setContentsMargins(22, 14, 22, 8);
    l->setStyleSheet(QString("color:%1;").arg(pal().link.name()));
    m_body->addWidget(l);
}

void Page::divider() {
    auto *d = new QFrame;
    d->setFixedHeight(8);
    d->setStyleSheet(QString("background:%1;").arg(pal().divider.name()));
    m_body->addWidget(d);
}

void Page::note(const QString &text) {
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setOpenExternalLinks(true);
    l->setContentsMargins(22, 10, 22, 12);
    l->setStyleSheet(QString("background:%1; color:%2;").arg(pal().sectionBg.name(), pal().textSecondary.name()));
    m_body->addWidget(l);
}

void Page::spacing(int h) { m_body->addSpacing(h); }

ClickRow *Page::row(const QString &icon, const QString &text, const QString &value, std::function<void()> cb) {
    auto *r = new ClickRow(icon, text, value);
    if (cb) connect(r, &ClickRow::clicked, this, [cb] { cb(); });
    m_body->addWidget(r);
    return r;
}

Switch *Page::toggle(const QString &icon, const QString &text, const QString &key, bool def, std::function<void(bool)> extra) {
    auto *r = new ClickRow(icon, text, {});
    auto *s = new Switch;
    s->setChecked(Prefs::instance().getBool(key, def));
    r->setSwitch(s);
    connect(s, &QAbstractButton::toggled, this, [key, extra](bool on) { Prefs::instance().set(key, on); if (extra) extra(on); });
    m_body->addWidget(r);
    return s;
}

QCheckBox *Page::check(const QString &text, const QString &key, bool def, std::function<void(bool)> extra) {
    auto *c = new QCheckBox(text);
    c->setChecked(Prefs::instance().getBool(key, def));
    c->setContentsMargins(22, 0, 0, 0);
    c->setStyleSheet("QCheckBox { padding: 9px 22px; }");
    connect(c, &QCheckBox::toggled, this, [key, extra](bool on) { Prefs::instance().set(key, on); if (extra) extra(on); });
    m_body->addWidget(c);
    return c;
}

void Page::radios(const QStringList &labels, const QString &key, int def, std::function<void(int)> extra) {
    const int cur = Prefs::instance().get(key, def).toInt();
    for (int i = 0; i < labels.size(); ++i) {
        auto *r = new QRadioButton(labels[i]);
        r->setChecked(i == cur);
        r->setStyleSheet("QRadioButton { padding: 8px 22px; }");
        connect(r, &QRadioButton::toggled, this, [key, i, extra](bool on) { if (on) { Prefs::instance().set(key, i); if (extra) extra(i); } });
        m_body->addWidget(r);
    }
}

QSlider *Page::slider(int min, int max, int value, const QString &suffix, std::function<void(int)> onChange) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(22, 4, 22, 10);
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(value);
    auto *lab = new QLabel(QString::number(value) + suffix);
    lab->setMinimumWidth(48);
    lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lab->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    l->addWidget(s, 1);
    l->addWidget(lab);
    connect(s, &QSlider::valueChanged, this, [lab, suffix](int v) { lab->setText(QString::number(v) + suffix); });
    connect(s, &QSlider::sliderReleased, this, [s, onChange] { if (onChange) onChange(s->value()); });
    connect(s, &QSlider::actionTriggered, this, [s, onChange](int) { QMetaObject::invokeMethod(s, [s, onChange] { if (onChange) onChange(s->value()); }, Qt::QueuedConnection); });
    m_body->addWidget(w);
    return s;
}

void Page::widget(QWidget *w) { m_body->addWidget(w); }

void Page::footerButton(const QString &text, std::function<void()> cb, bool gradient) {
    m_footer = new QWidget;
    auto *l = new QVBoxLayout(m_footer);
    l->setContentsMargins(12, 8, 12, 12);
    auto *b = new QPushButton(text);
    b->setObjectName("primary");
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(44);
    if (gradient)
        b->setStyleSheet("QPushButton#primary { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #6b93ff, stop:0.5 #976fff, stop:1 #e46ea6); color: white; border-radius: 8px; font-weight: bold; }");
    connect(b, &QPushButton::clicked, this, [cb] { cb(); });
    l->addWidget(b);
    m_outer->addWidget(m_footer);
}
