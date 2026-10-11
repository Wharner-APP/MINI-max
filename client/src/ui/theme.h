#pragma once
#include <QColor>
#include <QObject>

// Palettes follow Telegram Desktop's "Night" and "Day" themes.
struct Palette {
    QColor windowBg, railBg, sidebarBg, chatBg, headerBg, itemHover, itemActive, bubbleIn, bubbleOut;
    QColor accent, link, text, textSecondary, textOnActive, divider, inputBg, badge, badgeMuted, popupBg, sectionBg, danger;
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager &instance();
    void setMode(const QString &mode);        // dark | light | system
    void setAccent(const QColor &c);          // custom accent colour (invalid = default)
    bool isDark() const { return m_dark; }
    const Palette &palette() const { return m_pal; }
    QString styleSheet() const;
signals:
    void changed();
private:
    ThemeManager();
    void rebuild();
    bool m_dark = true;
    QString m_mode = "dark";
    QColor m_accent;
    Palette m_pal;
};
inline const Palette &pal() { return ThemeManager::instance().palette(); }
