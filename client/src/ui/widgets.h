#pragma once
#include <QAbstractButton>
#include <QVariantAnimation>
#include <QWidget>
#include <QtGlobal>
#include <functional>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
using MmEnterEvent = QEnterEvent;
#else
using MmEnterEvent = QEvent;
#endif

QColor avatarColor(qint64 id);
void paintAvatar(QPainter *p, const QRect &r, const QString &title, qint64 id, const QString &icon = {});
QPixmap avatarPixmap(int size, const QString &title, qint64 id, const QString &icon = {});

// Telegram-style toggle switch.
class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {40, 22}; }
protected:
    void paintEvent(QPaintEvent *) override;
    void checkStateSet() override;
    void nextCheckState() override { setChecked(!isChecked()); }
private:
    QVariantAnimation m_anim;
    qreal m_pos = 0;
};

// One settings row: [icon] text ........ value [>] (optional switch)
class ClickRow : public QWidget {
    Q_OBJECT
public:
    ClickRow(const QString &icon, const QString &text, const QString &value, QWidget *parent = nullptr);
    void setValue(const QString &v) { m_value = v; update(); }
    void setDanger(bool d) { m_danger = d; update(); }
    void setSwitch(Switch *s);
    QSize sizeHint() const override { return {300, m_h}; }
    void setRowHeight(int h) { m_h = h; setFixedHeight(h); }
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(MmEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *) override;
private:
    QString m_icon, m_text, m_value;
    Switch *m_switch = nullptr;
    bool m_hover = false, m_danger = false;
    int m_h = 40;
};
