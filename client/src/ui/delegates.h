#pragma once
#include <QStyledItemDelegate>

class ChatListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setCompact(bool c) { m_compact = c; }
    bool compact() const { return m_compact; }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {10, 62}; }
private:
    bool m_compact = false;
};

class MessageDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setViewportWidth(int w) { m_width = w; }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
private:
    struct Layout;
    Layout layout(const QFont &font, const QModelIndex &idx) const;
    int m_width = 600;
};
