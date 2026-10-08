#include "ui/widgets.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "ui/icons.h"
#include "core/prefs.h"
#include "ui/theme.h"

QColor avatarColor(qint64 id) {
    static const char *cols[] = {"#e17076", "#faa774", "#a695e7", "#7bc862", "#6ec9cb", "#65aadd", "#ee7aae"};
    return QColor(cols[qAbs(id) % 7]);
}

void paintAvatar(QPainter *p, const QRect &r, const QString &title, qint64 id, const QString &icon) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, avatarColor(id).lighter(112));
    g.setColorAt(1, avatarColor(id).darker(108));
    p->setPen(Qt::NoPen);
    p->setBrush(g);
    p->drawEllipse(r);
    if (!icon.isEmpty()) {
        const int s = r.height() * 55 / 100;
        p->drawPixmap(r.center().x() - s / 2, r.center().y() - s / 2, Icons::pixmap(icon, Qt::white, s));
    } else {
        QString initials;
        for (const QString &w : title.split(' ', Qt::SkipEmptyParts)) {
            initials += w.at(0).toUpper();
            if (initials.size() == 2) break;
        }
        QFont f = p->font();
        f.setBold(true);
        f.setPixelSize(qMax(8, r.height() * 38 / 100));
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, initials);
    }
    p->restore();
}

QPixmap avatarPixmap(int size, const QString &title, qint64 id, const QString &icon) {
    const qreal dpr = 2.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintAvatar(&p, QRect(0, 0, size, size), title, id, icon);
    return pm;
}

Switch::Switch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(40, 22);
    m_anim.setDuration(140);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { m_pos = v.toReal(); update(); });
}

void Switch::checkStateSet() {
    m_anim.stop();
    m_anim.setStartValue(m_pos);
    m_anim.setEndValue(isChecked() ? 1.0 : 0.0);
    m_anim.start();
}

void Switch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette &c = pal();
    const QRectF track(1, 4, 38, 14);
    QColor on = c.accent, off = c.badgeMuted;
    p.setPen(Qt::NoPen);
    QColor mix(int(off.red() + (on.red() - off.red()) * m_pos), int(off.green() + (on.green() - off.green()) * m_pos),
               int(off.blue() + (on.blue() - off.blue()) * m_pos));
    p.setBrush(mix.darker(isChecked() ? 140 : 100));
    p.drawRoundedRect(track, 7, 7);
    const qreal x = 1 + 18 * m_pos;
    p.setBrush(c.windowBg);
    p.setPen(QPen(mix, 2));
    p.drawEllipse(QRectF(x + 1, 2, 17, 17));
}

ClickRow::ClickRow(const QString &icon, const QString &text, const QString &value, QWidget *parent)
    : QWidget(parent), m_icon(icon), m_text(text), m_value(value) {
    setFixedHeight(m_h);
    setCursor(Qt::PointingHandCursor);
}

void ClickRow::setSwitch(Switch *s) {
    m_switch = s;
    s->setParent(this);
    s->show();
    connect(s, &QAbstractButton::toggled, this, [this] { update(); });
    resizeEvent(nullptr);
}

void ClickRow::resizeEvent(QResizeEvent *) {
    if (m_switch) m_switch->move(width() - 54, (height() - m_switch->height()) / 2);
}

void ClickRow::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || !rect().contains(e->pos())) return;
    if (m_switch) m_switch->toggle();
    emit clicked();
}

void ClickRow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const Palette &c = pal();
    if (m_hover) p.fillRect(rect(), c.itemHover);
    int x = 22;
    if (!m_icon.isEmpty()) {
        p.drawPixmap(x, (height() - 22) / 2, Icons::pixmap(m_icon, m_danger ? c.danger : c.textSecondary, 22));
        x += 38;
    }
    p.setPen(m_danger ? c.danger : c.text);
    QFontMetrics fm(font());
    int right = width() - 18 - (m_switch ? 46 : 0);
    if (!m_value.isEmpty()) {
        p.setPen(c.link);
        const int vw = fm.horizontalAdvance(m_value);
        p.drawText(QRect(right - vw, 0, vw, height()), Qt::AlignVCenter | Qt::AlignRight, m_value);
        right -= vw + 12;
        p.setPen(m_danger ? c.danger : c.text);
    }
    p.drawText(QRect(x, 0, right - x, height()), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(m_text, Qt::ElideRight, right - x));
}

QPixmap profileAvatarPixmap(int size, const QString &title, qint64 id) {
    const QString path = Prefs::instance().get("profile/avatar_path").toString();
    if (!path.isEmpty()) {
        QPixmap pm;
        if (pm.load(path) && !pm.isNull())
            return pm.scaled(size * 2, size * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    return avatarPixmap(size, title, id);
}
