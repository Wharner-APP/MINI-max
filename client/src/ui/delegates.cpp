#include <cmath>
#include "ui/delegates.h"

#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

#include "core/chats_model.h"
#include "core/messages_model.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

void ChatListDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    const Palette &c = pal();
    const bool sel = opt.state & QStyle::State_Selected, hover = opt.state & QStyle::State_MouseOver;
    const QRect r = opt.rect;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(r, sel ? c.itemActive : hover ? c.itemHover : c.sidebarBg);
    const auto kind = ChatKind(idx.data(ChatsModel::KindRole).toInt());
    const QString title = idx.data(ChatsModel::TitleRole).toString();
    const int unread = idx.data(ChatsModel::UnreadRole).toInt();
    const bool muted = idx.data(ChatsModel::MutedRole).toBool();
    const QRect av(r.left() + (m_compact ? 9 : 10), r.top() + 7, 48, 48);
    paintAvatar(p, av, title, idx.data(ChatsModel::IdRole).toLongLong(), kind == ChatKind::Saved ? "bookmark" : QString());

    QFont small = opt.font;
    small.setPointSizeF(opt.font.pointSizeF() - 1.5);
    auto badge = [&](const QRect &where) {
        const QString s = unread > 99999 ? "99K+" : QString::number(unread);
        p->setPen(Qt::NoPen);
        p->setBrush(sel ? c.textOnActive : (muted ? c.badgeMuted : c.badge));
        p->drawRoundedRect(where, where.height() / 2, where.height() / 2);
        p->setFont(small);
        p->setPen(sel ? c.itemActive : Qt::white);
        p->drawText(where, Qt::AlignCenter, s);
    };
    if (m_compact) {
        if (unread > 0) {
            const int w = qMax(20, QFontMetrics(small).horizontalAdvance(QString::number(unread)) + 12);
            badge(QRect(av.right() - w + 8, av.bottom() - 18, w, 20));
        }
        p->restore();
        return;
    }
    const int left = av.right() + 12, right = r.right() - 12;
    const QColor sec = sel ? c.textOnActive : c.textSecondary, main = sel ? c.textOnActive : c.text;
    QFont bold = opt.font;
    bold.setBold(true);
    const QString time = idx.data(ChatsModel::TimeRole).toString();
    int timeW = QFontMetrics(small).horizontalAdvance(time);
    int tx = right - timeW;
    if (idx.data(ChatsModel::LastOutgoingRole).toBool()) {  // tick before the time
        const int st = idx.data(ChatsModel::LastStatusRole).toInt();
        tx -= 18;
        p->setPen(QPen(sel ? c.textOnActive : c.link, 1.6, Qt::SolidLine, Qt::RoundCap));
        p->drawPolyline(QPolygonF({QPointF(tx + 1, r.top() + 21), QPointF(tx + 4.5, r.top() + 24.5), QPointF(tx + 10, r.top() + 17.5)}));
        if (st >= 2) p->drawPolyline(QPolygonF({QPointF(tx + 6, r.top() + 24), QPointF(tx + 7, r.top() + 25), QPointF(tx + 13, r.top() + 17.5)}));
    }
    p->setFont(small);
    p->setPen(sec);
    p->drawText(QRect(right - timeW, r.top() + 9, timeW, 22), Qt::AlignRight | Qt::AlignVCenter, time);
    int ix = left;
    const QString kicon = kind == ChatKind::Channel ? "channel" : kind == ChatKind::Group ? "group" : kind == ChatKind::Bot ? "bot" : QString();
    if (!kicon.isEmpty()) { p->drawPixmap(ix, r.top() + 14, Icons::pixmap(kicon, main, 14)); ix += 18; }
    p->setFont(bold);
    p->setPen(main);
    const bool verified = idx.data(ChatsModel::VerifiedRole).toBool();
    const int avail = tx - 8 - ix - (verified ? 18 : 0) - (muted ? 16 : 0);
    const QString et = QFontMetrics(bold).elidedText(title, Qt::ElideRight, avail);
    p->drawText(QRect(ix, r.top() + 9, avail + 4, 22), Qt::AlignLeft | Qt::AlignVCenter, et);
    int bx = ix + QFontMetrics(bold).horizontalAdvance(et) + 4;
    if (verified) { p->setBrush(sel ? c.textOnActive : c.link); p->setPen(Qt::NoPen); p->drawEllipse(QRectF(bx, r.top() + 14, 13, 13)); p->setPen(QPen(sel ? c.itemActive : Qt::white, 1.6)); p->drawPolyline(QPolygonF({QPointF(bx + 3.5, r.top() + 20.5), QPointF(bx + 5.8, r.top() + 22.8), QPointF(bx + 9.8, r.top() + 17.8)})); bx += 17; }
    if (muted) p->drawPixmap(bx, r.top() + 14, Icons::pixmap("bell-off", sec, 14));

    int badgeW = 0;
    if (unread > 0) {
        const QString s = unread > 99999 ? "99K+" : QString::number(unread);
        badgeW = qMax(22, QFontMetrics(small).horizontalAdvance(s) + 14);
        badge(QRect(right - badgeW, r.top() + 34, badgeW, 22));
        badgeW += 8;
    } else if (idx.data(ChatsModel::PinnedRole).toBool()) {
        p->drawPixmap(right - 16, r.top() + 38, Icons::pixmap("pin", sec, 16));
        badgeW = 24;
    }
    p->setFont(opt.font);
    p->setPen(sec);
    const QRect row2(left, r.top() + 34, right - left - badgeW, 22);
    p->drawText(row2, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(opt.font).elidedText(idx.data(ChatsModel::LastTextRole).toString(), Qt::ElideRight, row2.width()));
    p->restore();
}

// ---------------------------------------------------------------- messages
namespace {
constexpr int kSide = 14, kPad = 10, kGap = 4, kRadius = 12;

struct Wrapped { QTextLayout *layout; QVector<QPair<QPointF, QRectF>> dummy; int height = 0, widest = 0, lastW = 0; };

void wrap(QTextLayout &tl, int maxW, int *height, int *widest, int *lastW) {
    QTextOption o;
    o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    tl.setTextOption(o);
    tl.beginLayout();
    qreal y = 0, wmax = 0, last = 0;
    for (;;) {
        QTextLine line = tl.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(maxW);
        line.setPosition(QPointF(0, y));
        y += line.height();
        wmax = qMax(wmax, line.naturalTextWidth());
        last = line.naturalTextWidth();
    }
    tl.endLayout();
    *height = int(std::ceil(y));
    *widest = int(std::ceil(wmax));
    *lastW = int(std::ceil(last));
}
}  // namespace

struct MessageDelegate::Layout {
    QRect bubble, text, reply, sender, time, reactions;
    int dateH = 0, height = 0, textW = 0;
    bool timeInline = true;
    QString dateText, timeText, replyAuthor, replyText, senderName;
};

MessageDelegate::Layout MessageDelegate::layout(const QFont &font, const QModelIndex &idx) const {
    Layout L;
    const bool out = idx.data(MessagesModel::OutgoingRole).toBool();
    const QString text = idx.data(MessagesModel::TextRole).toString();
    const QString mediaKind = idx.data(MessagesModel::MediaKindRole).toString();
    L.timeText = (idx.data(MessagesModel::EditedRole).toBool() ? QStringLiteral("изменено ") : QString()) + idx.data(MessagesModel::TimeRole).toString();
    const int views = idx.data(MessagesModel::ViewsRole).toInt();
    if (views > 0) L.timeText = (views >= 1000 ? QString::number(views / 1000.0, 'f', 1) + "K" : QString::number(views)) + "  " + L.timeText;
    L.dateText = idx.data(MessagesModel::DateHeaderRole).toString();
    L.replyText = idx.data(MessagesModel::ReplyRole).toString();
    L.replyAuthor = idx.data(MessagesModel::ReplyAuthorRole).toString();
    if (idx.data(MessagesModel::ShowSenderRole).toBool()) L.senderName = idx.data(MessagesModel::SenderRole).toString();
    QFont tf = font;
    QFont contentFont = font;
    if (mediaKind == "sticker") contentFont.setPointSizeF(contentFont.pointSizeF() + 20);
    tf.setPointSizeF(contentFont.pointSizeF() - 2);
    const QFontMetrics fm(contentFont), tfm(tf);
    const int maxBubble = qMax(160, qMin(480, m_width - 2 * kSide - 40));
    const int inner = maxBubble - 2 * kPad;
    QTextLayout tl(text, contentFont);
    int th, widest, lastW;
    wrap(tl, inner, &th, &widest, &lastW);
    const int timeW = tfm.horizontalAdvance(L.timeText) + (out ? 22 : 0);
    L.timeInline = lastW + 10 + timeW <= inner;
    int contentW = qMax(widest, L.timeInline ? lastW + 10 + timeW : timeW);
    int y = kPad;
    if (!L.senderName.isEmpty()) { contentW = qMax(contentW, QFontMetrics(contentFont).horizontalAdvance(L.senderName)); y += fm.height() + 2; }
    const int replyTop = y;
    if (!L.replyText.isEmpty()) { contentW = qMax(contentW, qMin(inner, 150)); y += 40; }
    const int textTop = y;
    y += th;
    if (!L.timeInline) y += tfm.height();
    const auto reacts = idx.data(MessagesModel::ReactionsRole).toList();
    const int reactTop = y + 4;
    if (!reacts.isEmpty()) y += 30;
    y += kPad / 2 + (L.timeInline ? tfm.height() - fm.height() > 0 ? 0 : 0 : 0);
    const int w = qMin(inner, contentW) + 2 * kPad;
    L.dateH = L.dateText.isEmpty() ? 0 : 34;
    const int x = out ? m_width - kSide - w : kSide;
    L.bubble = QRect(x, L.dateH + kGap / 2, w, y);
    L.sender = QRect(x + kPad, L.bubble.top() + kPad, w - 2 * kPad, fm.height());
    L.reply = QRect(x + kPad, L.bubble.top() + replyTop, w - 2 * kPad, 36);
    L.text = QRect(x + kPad, L.bubble.top() + textTop, w - 2 * kPad, th);
    L.textW = inner;
    L.time = QRect(x + w - kPad - timeW, L.bubble.bottom() - tfm.height() - 3, timeW, tfm.height());
    L.reactions = QRect(x + kPad, L.bubble.top() + reactTop, w - 2 * kPad, 26);
    L.height = L.dateH + y + kGap;
    return L;
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const { return {m_width, layout(opt.font, idx).height}; }

void MessageDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    const Palette &c = pal();
    const bool out = idx.data(MessagesModel::OutgoingRole).toBool();
    const QString mediaKind = idx.data(MessagesModel::MediaKindRole).toString();
    const QString text = idx.data(MessagesModel::TextRole).toString();
    const Layout L = layout(opt.font, idx);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->translate(opt.rect.topLeft());
    if (L.dateH) {
        QFont df = opt.font;
        df.setPointSizeF(df.pointSizeF() - 1);
        p->setFont(df);
        const int tw = QFontMetrics(df).horizontalAdvance(L.dateText) + 22;
        const QRect pill((m_width - tw) / 2, 4, tw, 24);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 70));
        p->drawRoundedRect(pill, 12, 12);
        p->setPen(Qt::white);
        p->drawText(pill, Qt::AlignCenter, L.dateText);
    }
    p->setPen(Qt::NoPen);
    p->setBrush(out ? c.bubbleOut : c.bubbleIn);
    p->drawRoundedRect(L.bubble, kRadius, kRadius);
    p->setFont(opt.font);
    if (mediaKind == "sticker") {
        QFont sf = opt.font;
        sf.setPointSizeF(opt.font.pointSizeF() + 22);
        p->setFont(sf);
        p->setPen(c.text);
        p->drawText(L.text, Qt::AlignCenter, text);
    } else if (mediaKind == "gif") {
        QFont gf = opt.font;
        gf.setPointSizeF(opt.font.pointSizeF() + 2);
        gf.setBold(true);
        p->setFont(gf);
        p->setPen(c.link);
        p->drawText(L.text, Qt::AlignCenter, "GIF");
        p->setPen(c.textSecondary);
        p->drawText(L.text.adjusted(0, 26, 0, 0), Qt::AlignCenter, text.isEmpty() ? "Анимация" : text.left(32));
    } else if (mediaKind == "voice") {
        p->setPen(c.link);
        p->drawText(L.text, Qt::AlignCenter, "🎤  Голосовое сообщение");
    } else if (mediaKind == "round") {
        p->setPen(c.link);
        p->drawText(L.text, Qt::AlignCenter, "🎥  Видеосообщение");
    } else {
        QTextLayout tl(idx.data(MessagesModel::TextRole).toString(), opt.font);
        int h, w1, w2;
        wrap(tl, L.text.width(), &h, &w1, &w2);
        p->setPen(c.text);
        tl.draw(p, QPointF(L.text.left(), L.text.top()));
    }
    static const char *senderCols[] = {"#e17076", "#faa774", "#a695e7", "#7bc862", "#6ec9cb", "#65aadd", "#ee7aae"};
    if (!L.senderName.isEmpty()) {
        QFont b = opt.font;
        b.setBold(true);
        p->setFont(b);
        p->setPen(QColor(senderCols[idx.data(MessagesModel::SenderColorRole).toInt() % 7]));
        p->drawText(L.sender, Qt::AlignLeft | Qt::AlignVCenter, L.senderName);
        p->setFont(opt.font);
    }
    if (!L.replyText.isEmpty()) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 18));
        p->drawRoundedRect(L.reply, 4, 4);
        p->setBrush(c.link);
        p->drawRect(QRect(L.reply.left(), L.reply.top(), 3, L.reply.height()));
        QFont b = opt.font;
        b.setBold(true);
        p->setFont(b);
        p->setPen(c.link);
        p->drawText(L.reply.adjusted(10, 2, -4, -18), Qt::AlignLeft | Qt::AlignVCenter, L.replyAuthor);
        p->setFont(opt.font);
        p->setPen(c.text);
        p->drawText(L.reply.adjusted(10, 18, -4, -2), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(opt.font).elidedText(L.replyText, Qt::ElideRight, L.reply.width() - 14));
    }
    const auto reacts = idx.data(MessagesModel::ReactionsRole).toList();
    int rx = L.reactions.left();
    for (const QVariant &rv : reacts) {
        const QStringList r = rv.toStringList();
        const int rw = QFontMetrics(opt.font).horizontalAdvance(r[0] + " " + r[1]) + 16;
        const QRect pill(rx, L.reactions.top(), rw, 24);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 28));
        p->drawRoundedRect(pill, 12, 12);
        p->setPen(c.text);
        p->drawText(pill, Qt::AlignCenter, r[0] + " " + r[1]);
        rx += rw + 6;
    }
    QFont tf = opt.font;
    tf.setPointSizeF(opt.font.pointSizeF() - 2);
    p->setFont(tf);
    const QColor tc = out ? (ThemeManager::instance().isDark() ? QColor("#7da8d3") : QColor("#5da658")) : c.textSecondary;
    p->setPen(tc);
    QRect tr = L.time;
    if (out) {
        const int st = idx.data(MessagesModel::StatusRole).toInt();
        const int cx = tr.right() - 14, cy = tr.center().y();
        p->setPen(QPen(tc, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPolyline(QPolygonF({QPointF(cx, cy), QPointF(cx + 3, cy + 3), QPointF(cx + 8, cy - 3)}));
        if (st >= 2) p->drawPolyline(QPolygonF({QPointF(cx + 5, cy + 2.4), QPointF(cx + 6, cy + 3), QPointF(cx + 11, cy - 3)}));
        tr.setRight(tr.right() - 18);
        p->setPen(tc);
    }
    p->drawText(tr, Qt::AlignRight | Qt::AlignVCenter, L.timeText);
    p->restore();
}
