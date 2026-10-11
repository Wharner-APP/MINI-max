#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace Icons {
QPixmap pixmap(const QString &name, const QColor &color, int size);
QIcon icon(const QString &name, const QColor &color, int size = 24);
}
