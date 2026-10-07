#include "ui/theme.h"

#include <QGuiApplication>
#include <QPalette>

ThemeManager::ThemeManager() { rebuild(); }
ThemeManager &ThemeManager::instance() { static ThemeManager t; return t; }

void ThemeManager::setMode(const QString &mode) { m_mode = mode; rebuild(); emit changed(); }
void ThemeManager::setAccent(const QColor &c) { m_accent = c; rebuild(); emit changed(); }

void ThemeManager::rebuild() {
    m_dark = m_mode == "system" ? QGuiApplication::palette().color(QPalette::Window).lightness() < 128 : m_mode != "light";
    if (m_dark) {
        m_pal = {"#17212b", "#0e1621", "#17212b", "#0e1621", "#17212b", "#202b36", "#2b5278", "#182533", "#2b5278",
                 "#5288c1", "#6ab3f3", "#f5f5f5", "#6d7f8f", "#ffffff", "#0e1621", "#242f3d", "#4a8bc9", "#3e546a",
                 "#17212b", "#1d2a38", "#ec3942"};
    } else {
        m_pal = {"#ffffff", "#f1f3f5", "#ffffff", "#e6ebee", "#ffffff", "#f1f1f1", "#419fd9", "#ffffff", "#effdde",
                 "#40a7e3", "#168acd", "#000000", "#8e979f", "#ffffff", "#e6e6e6", "#f1f3f4", "#40a7e3", "#c6c9cc",
                 "#ffffff", "#f1f3f4", "#e53935"};
    }
    if (m_accent.isValid()) {
        m_pal.accent = m_accent;
        m_pal.link = m_accent.lighter(m_dark ? 125 : 100);
        m_pal.badge = m_accent;
        if (m_dark) { m_pal.bubbleOut = m_accent.darker(190); m_pal.itemActive = m_accent.darker(190); }
    }
}

QString ThemeManager::styleSheet() const {
    const Palette &p = m_pal;
    QString s = QStringLiteral(R"(
QWidget { background: @win@; color: @text@; }
QToolTip { background: @popup@; color: @text@; border: 1px solid @div@; padding: 4px; }
QLabel { background: transparent; }
QLineEdit { background: @input@; border: 1px solid transparent; border-radius: 18px; padding: 8px 14px; selection-background-color: @accent@; }
QLineEdit:focus { border: 1px solid @accent@; }
QPlainTextEdit { background: @win@; border: none; padding: 6px 4px; selection-background-color: @accent@; }
QToolButton { background: transparent; border: none; border-radius: 18px; padding: 6px; }
QToolButton:hover { background: @hover@; }
QPushButton#primary { background: @accent@; color: white; border: none; border-radius: 8px; padding: 11px 18px; font-weight: bold; }
QPushButton#primary:hover { background: @accentL@; }
QPushButton#primary:disabled { background: @muted@; color: @sec@; }
QPushButton#flat { background: transparent; color: @link@; border: none; padding: 8px 12px; font-weight: bold; }
QPushButton#flat:hover { background: @hover@; border-radius: 6px; }
QPushButton#danger { background: transparent; color: @danger@; border: none; padding: 8px 12px; font-weight: bold; }
QMenu { background: @popup@; border: 1px solid @div@; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 7px 24px 7px 14px; border-radius: 4px; }
QMenu::item:selected { background: @hover@; }
QMenu::separator { height: 1px; background: @div@; margin: 4px 8px; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: @muted@; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollArea { border: none; }
QSlider::groove:horizontal { height: 3px; background: @muted@; border-radius: 1px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: 1px; }
QSlider::handle:horizontal { background: @win@; border: 2px solid @accent@; width: 10px; height: 10px; margin: -6px 0; border-radius: 7px; }
QCheckBox, QRadioButton { spacing: 12px; background: transparent; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 2px solid @sec@; background: transparent; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QRadioButton::indicator { width: 16px; height: 16px; border-radius: 9px; border: 2px solid @sec@; background: transparent; }
QRadioButton::indicator:checked { border: 5px solid @accent@; background: @win@; }
QDialog, QInputDialog, QMessageBox { background: @popup@; }
)");
    const QList<QPair<QString, QColor>> map = {{"@win@", p.windowBg}, {"@text@", p.text}, {"@popup@", p.popupBg}, {"@div@", p.divider}, {"@input@", p.inputBg},
        {"@accentL@", p.accent.lighter(112)}, {"@accent@", p.accent}, {"@hover@", p.itemHover}, {"@muted@", p.badgeMuted}, {"@sec@", p.textSecondary},
        {"@link@", p.link}, {"@danger@", p.danger}};
    for (const auto &kv : map) s.replace(kv.first, kv.second.name());
    return s;
}
