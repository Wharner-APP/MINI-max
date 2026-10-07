#pragma once
#include <QJsonObject>
#include <QObject>
#include <QVariant>

// Small JSON preference store (<dataDir>/prefs.json). Not secret data.
class Prefs : public QObject {
    Q_OBJECT
public:
    static Prefs &instance();
    void open(const QString &dataDir);
    QVariant get(const QString &key, const QVariant &def = {}) const;
    bool getBool(const QString &key, bool def) const { return get(key, def).toBool(); }
    void set(const QString &key, const QVariant &v);
signals:
    void changed(const QString &key);

private:
    Prefs() = default;
    void save() const;
    QString m_path;
    QJsonObject m_obj;
};
