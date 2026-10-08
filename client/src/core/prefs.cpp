#include "core/prefs.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

Prefs &Prefs::instance() {
    static Prefs p;
    return p;
}

void Prefs::open(const QString &dataDir) {
    m_path = dataDir + "/prefs.json";
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) m_obj = QJsonDocument::fromJson(f.readAll()).object();
}

QVariant Prefs::get(const QString &key, const QVariant &def) const {
    return m_obj.contains(key) ? m_obj.value(key).toVariant() : def;
}

void Prefs::set(const QString &key, const QVariant &v) {
    m_obj.insert(key, QJsonValue::fromVariant(v));
    save();
    emit changed(key);
}

void Prefs::save() const {
    if (m_path.isEmpty()) return;
    QSaveFile f(m_path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_obj).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
