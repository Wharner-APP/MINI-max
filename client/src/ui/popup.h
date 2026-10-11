#pragma once
#include <QFrame>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVector>
#include <functional>

class Switch;
class ClickRow;
class QVBoxLayout;
class QSlider;
class QCheckBox;
class QScrollArea;

// Modal overlay with a centred card (Telegram-style popup). Pages can be stacked (back arrow).
class PopupHost : public QWidget {
    Q_OBJECT
public:
    explicit PopupHost(QWidget *window);
    void open(QWidget *page, const QString &title, int width = 392);   // replaces the stack
    void push(QWidget *page, const QString &title);
    void pop();
    void closeAll();
    bool isOpen() const { return !m_entries.isEmpty(); }

    struct Button { QString text; std::function<void()> action; bool danger = false; };
    void dialog(const QString &title, const QString &text, const QVector<Button> &buttons, int width = 360);
    void choice(const QString &title, const QStringList &options, int current, std::function<void(int)> cb);
    void input(const QString &title, const QString &placeholder, const QString &initial, const QString &okText, std::function<void(const QString &)> cb);

signals:
    void closed();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;
    void showEvent(QShowEvent *) override;
private:
    struct Entry { QWidget *page; QString title; bool headless; };
    void show(QWidget *page, const QString &title, int width, bool headless, bool replace);
    void relayout();
    QFrame *m_card;
    QWidget *m_header;
    QStackedWidget *m_stack;
    QLabel *m_title;
    QToolButton *m_back, *m_close;
    QVector<Entry> m_entries;
    qreal m_dim = 0;
    int m_width = 392;
};

// Scrollable settings-style page with helpers to add rows.
class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(PopupHost *host);
    PopupHost *host() const { return m_host; }
    void section(const QString &text);
    void divider();                        // thick section separator
    void note(const QString &text);        // grey info block
    void spacing(int h);
    ClickRow *row(const QString &icon, const QString &text, const QString &value = {}, std::function<void()> cb = {});
    Switch *toggle(const QString &icon, const QString &text, const QString &prefKey, bool def, std::function<void(bool)> extra = {});
    QCheckBox *check(const QString &text, const QString &prefKey, bool def, std::function<void(bool)> extra = {});
    void radios(const QStringList &labels, const QString &prefKey, int def, std::function<void(int)> extra = {});
    QSlider *slider(int min, int max, int value, const QString &suffix, std::function<void(int)> onChange);
    void widget(QWidget *w);
    void footerButton(const QString &text, std::function<void()> cb, bool gradient = false);
    QSize sizeHint() const override;
private:
    PopupHost *m_host;
    QScrollArea *m_scroll;
    QWidget *m_content;
    QVBoxLayout *m_body, *m_outer;
    QWidget *m_footer = nullptr;
};
