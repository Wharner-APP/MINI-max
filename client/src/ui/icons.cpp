#include "ui/icons.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace {
const QHash<QString, QString> &table() {
    static const QHash<QString, QString> t = {
        {"menu", R"(<path d="M4 7h16M4 12h16M4 17h16"/>)"},
        {"search", R"(<circle cx="11" cy="11" r="6.5"/><path d="M16 16l4.5 4.5"/>)"},
        {"phone", R"(<path d="M6 3.5h3l1.5 4-2 1.2a10 10 0 005.8 5.8l1.2-2 4 1.5v3a2 2 0 01-2 2C10.5 19 5 13.5 4 5.5a2 2 0 012-2z"/>)"},
        {"panel", R"(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><path d="M15 5v14"/>)"},
        {"dots", R"(<g fill="currentColor" stroke="none"><circle cx="12" cy="5.5" r="1.7"/><circle cx="12" cy="12" r="1.7"/><circle cx="12" cy="18.5" r="1.7"/></g>)"},
        {"close", R"(<path d="M6 6l12 12M18 6L6 18"/>)"},
        {"back", R"(<path d="M19 12H5M11 6l-6 6 6 6"/>)"},
        {"chevron", R"(<path d="M9 6l6 6-6 6"/>)"},
        {"send", R"(<path fill="currentColor" stroke="none" d="M3.4 20.4l17.5-7.5a1 1 0 000-1.8L3.4 3.6a.8.8 0 00-1.1.9L3.5 10l9 2-9 2-1.2 5.5a.8.8 0 001.1.9z"/>)"},
        {"attach", R"(<path d="M20 11.5l-8 8a5 5 0 01-7-7l8.5-8.5a3.3 3.3 0 014.7 4.7L9.5 17a1.7 1.7 0 01-2.4-2.4l7.5-7.5"/>)"},
        {"smile", R"(<circle cx="12" cy="12" r="9"/><path d="M8.5 14.5a4.5 4.5 0 007 0M9 10h.01M15 10h.01"/>)"},
        {"mic", R"(<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0014 0M12 18v3"/>)"},
        {"timer", R"(<circle cx="12" cy="13" r="7.5"/><path d="M12 9v4l2.5 1.5M9.5 3.5h5"/>)"},
        {"chats", R"(<path d="M4 6.5a3 3 0 013-3h8a3 3 0 013 3v5a3 3 0 01-3 3h-5l-4 3.5v-3.8a3 3 0 01-2-2.7z"/><path d="M19 9.5a3 3 0 012 2.7v3.5a3 3 0 01-2 2.7v2l-3-2h-4"/>)"},
        {"group", R"(<circle cx="9" cy="8.5" r="3.2"/><path d="M3.5 19a5.5 5.5 0 0111 0"/><circle cx="17" cy="9.5" r="2.4"/><path d="M16 14.2a4.5 4.5 0 015.5 4.3"/>)"},
        {"person", R"(<circle cx="12" cy="8.5" r="3.7"/><path d="M5 20a7 7 0 0114 0"/>)"},
        {"account", R"(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="10" r="3"/><path d="M6.5 18a6.5 6.5 0 0111 0"/>)"},
        {"bot", R"(<rect x="5" y="8" width="14" height="10" rx="3"/><path d="M12 8V4.5M9 13h.01M15 13h.01"/>)"},
        {"channel", R"(<path d="M4 10v4h3l7 4V6L7 10z"/><path d="M17.5 9.5a3.5 3.5 0 010 5"/>)"},
        {"folder", R"(<path d="M3.5 7.5a2 2 0 012-2h4l2 2.5h7a2 2 0 012 2v7.5a2 2 0 01-2 2h-13a2 2 0 01-2-2z"/>)"},
        {"sliders", R"(<path d="M4 8h9M17 8h3M4 16h3M11 16h9"/><circle cx="15" cy="8" r="2"/><circle cx="9" cy="16" r="2"/>)"},
        {"bell", R"(<path d="M6 16.5V11a6 6 0 0112 0v5.5l1.5 1.5h-15zM10 20.5a2 2 0 004 0"/>)"},
        {"bell-off", R"(<path d="M6 16.5V11a6 6 0 0110-4.4M18 11v5.5l1.5 1.5H8M10 20.5a2 2 0 004 0M4 4l16 16"/>)"},
        {"lock", R"(<rect x="5.5" y="10.5" width="13" height="9.5" rx="2.2"/><path d="M8.5 10.5V8a3.5 3.5 0 017 0v2.5"/>)"},
        {"chat", R"(<path d="M12 4a8 8 0 00-6.8 12.2L4 20l3.9-1.1A8 8 0 1012 4z"/>)"},
        {"volume", R"(<path d="M4 10v4h3.5L12 18V6L7.5 10z"/><path d="M15.5 9a4 4 0 010 6M18 6.5a8 8 0 010 11"/>)"},
        {"battery", R"(<rect x="3.5" y="7.5" width="16" height="9" rx="2"/><path d="M21.5 11v2M12 9.5l-2 3h3l-1 2"/>)"},
        {"language", R"(<path d="M4 6h9M8.5 4v2M6 6c.5 3 2.5 5.5 5.5 7M12 6c-.5 3-3 6-6.5 8M13 20l4-9 4 9M14.5 17h5"/>)"},
        {"eye", R"(<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.8"/>)"},
        {"eye-off", R"(<path d="M2.5 12S6 5.5 12 5.5c1.5 0 2.8.4 4 1M21.5 12S18 18.5 12 18.5c-1.5 0-2.8-.4-4-1M4 4l16 16"/>)"},
        {"star", R"(<path fill="currentColor" stroke="none" d="M12 3l2.7 5.7 6.2.8-4.6 4.3 1.2 6.2L12 17l-5.5 3 1.2-6.2L3.1 9.5l6.2-.8z"/>)"},
        {"gift", R"(<rect x="4.5" y="10" width="15" height="10" rx="1.5"/><path d="M3.5 10h17V7h-17zM12 7v13M12 7c-1-3.5-5-3.5-4.5-.5M12 7c1-3.5 5-3.5 4.5-.5"/>)"},
        {"shop", R"(<path d="M4.5 9.5l1-4.5h13l1 4.5a2.5 2.5 0 01-5 0 2.5 2.5 0 01-5 0 2.5 2.5 0 01-5 0M6 12.5V20h12v-7.5"/>)"},
        {"help", R"(<circle cx="12" cy="12" r="9"/><path d="M9.6 9.5a2.5 2.5 0 114 2c-.9.6-1.6 1.1-1.6 2.3M12 17h.01"/>)"},
        {"bulb", R"(<path d="M9 17.5h6M10 20.5h4M12 3.5a6 6 0 00-3.5 10.8c.7.6 1 1.2 1 2.2h5c0-1 .3-1.6 1-2.2A6 6 0 0012 3.5z"/>)"},
        {"plus", R"(<path d="M12 5v14M5 12h14"/>)"},
        {"edit", R"(<path d="M4 20l1-4L16.5 4.5a2 2 0 013 3L8 19z"/>)"},
        {"trash", R"(<path d="M5 7h14M10 4h4M7 7l.8 12a1.5 1.5 0 001.5 1.4h5.4a1.5 1.5 0 001.5-1.4L17 7M10 11v6M14 11v6"/>)"},
        {"block", R"(<circle cx="12" cy="12" r="9"/><path d="M5.6 5.6l12.8 12.8"/>)"},
        {"archive", R"(<rect x="4" y="5" width="16" height="4" rx="1"/><path d="M5.5 9v9a1.5 1.5 0 001.5 1.5h10a1.5 1.5 0 001.5-1.5V9M12 11.5v5M9.5 14.5l2.5 2.5 2.5-2.5"/>)"},
        {"bookmark", R"(<path d="M7 4h10v16l-5-3.5L7 20z"/>)"},
        {"moon", R"(<path d="M19.5 14.5A8 8 0 019.5 4.5a8 8 0 1010 10z"/>)"},
        {"gear", R"(<circle cx="12" cy="12" r="3"/><path d="M12 3v2.5M12 18.5V21M3 12h2.5M18.5 12H21M5.6 5.6l1.8 1.8M16.6 16.6l1.8 1.8M5.6 18.4l1.8-1.8M16.6 7.4l1.8-1.8"/>)"},
        {"link", R"(<path d="M10 14a4 4 0 005.7 0l3-3a4 4 0 00-5.7-5.7l-1 1M14 10a4 4 0 00-5.7 0l-3 3a4 4 0 005.7 5.7l1-1"/>)"},
        {"photo", R"(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><circle cx="9" cy="10" r="1.6"/><path d="M4 17l5-4.5 4 3.5 3-2.5 4 3.5"/>)"},
        {"video", R"(<rect x="3.5" y="6" width="12" height="12" rx="2.5"/><path d="M15.5 10.5l5-3v9l-5-3z"/>)"},
        {"file", R"(<path d="M6.5 3.5h7L18.5 8.5v10a2 2 0 01-2 2h-10a2 2 0 01-2-2v-13a2 2 0 012-2z"/><path d="M13.5 3.5v5h5"/>)"},
        {"headphones", R"(<path d="M4.5 15v-3a7.5 7.5 0 0115 0v3"/><rect x="3.5" y="14" width="4" height="6" rx="1.5"/><rect x="16.5" y="14" width="4" height="6" rx="1.5"/>)"},
        {"gif", R"(<rect x="3.5" y="6.5" width="17" height="11" rx="2.5"/><path d="M10 10.5H8a1.5 1.5 0 000 3h1.5v-1M12.5 10.5v3M15 13.5v-3h2.5M15 12h2"/>)"},
        {"check", R"(<path d="M5 12.5l4.5 4.5L19 7.5"/>)"},
        {"key", R"(<circle cx="8" cy="15.5" r="3.5"/><path d="M10.5 13l8-8M16 7.5l2 2M14 9.5l1.5 1.5"/>)"},
        {"shield", R"(<path d="M12 3.5l7 2.5v5.5c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"/><path d="M9 12l2.2 2.2L15.5 10"/>)"},
        {"mail", R"(<rect x="3.5" y="5.5" width="17" height="13" rx="2.5"/><path d="M4 7l8 6 8-6"/>)"},
        {"globe", R"(<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18"/>)"},
        {"devices", R"(<rect x="3.5" y="5" width="12" height="9" rx="1.5"/><rect x="14" y="10" width="6.5" height="9" rx="1.5"/><path d="M6.5 18h5"/>)"},
        {"qr", R"(<rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><path d="M14 14h2.5v2.5M20 14v.01M14 20h2.5M20 17v3"/>)"},
        {"pin", R"(<path d="M14.5 4l5.5 5.5-3 .5-3.5 3.5.5 4-1 1-4-4-5 5M13 6.5l-3 .5-1 1 7 7 1-1 .5-3z"/>)"},
        {"trending", R"(<path d="M4 18l6-6 4 4 6-8M15 8h5v5"/>)"},
        {"sticker", R"(<path d="M5 6a2 2 0 012-2h10a2 2 0 012 2v7l-6 6H7a2 2 0 01-2-2z"/><path d="M13 19v-4a2 2 0 012-2h4M9 10h.01M14 10h.01"/>)"},
        {"clock", R"(<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>)"},
        {"reply", R"(<path d="M10 7L4 12l6 5M4 12h10a6 6 0 016 6"/>)"},
        {"forward", R"(<path d="M14 7l6 5-6 5M20 12H10a6 6 0 00-6 6"/>)"},
        {"copy", R"(<rect x="8.5" y="8.5" width="11" height="11" rx="2"/><path d="M15.5 8.5v-2a2 2 0 00-2-2h-7a2 2 0 00-2 2v7a2 2 0 002 2h2"/>)"},
    };
    return t;
}
}  // namespace

namespace Icons {
QPixmap pixmap(const QString &name, const QColor &color, int size) {
    static QHash<QString, QPixmap> cache;
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const QString key = name + color.name(QColor::HexArgb) + QString::number(size) + QString::number(dpr);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    const QString inner = table().value(name);
    if (inner.isEmpty()) return {};
    QByteArray svg = QString(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">%1</svg>)").arg(inner).toUtf8();
    svg.replace("currentColor", color.name().toUtf8());
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    QSvgRenderer(svg).render(&p, QRectF(0, 0, size, size));
    cache.insert(key, pm);
    return pm;
}
QIcon icon(const QString &name, const QColor &color, int size) { return QIcon(pixmap(name, color, size)); }
}  // namespace Icons
