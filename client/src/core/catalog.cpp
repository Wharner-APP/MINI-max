#include "core/catalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

Catalog Catalog::load(const QString &configDir) {
    QByteArray data;
    QFile ext(configDir + "/catalog.json");
    if (ext.open(QIODevice::ReadOnly)) data = ext.readAll();
    else {
        QFile res(":/catalog.json");
        if (res.open(QIODevice::ReadOnly)) data = res.readAll();
    }
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    Catalog c;
    c.currency = o["currency"].toString("RUB");
    c.businessMonthlyMinor = qint64(o["business_monthly_minor"].toDouble());
    for (const QJsonValue &v : o["plans"].toArray()) {
        const QJsonObject p = v.toObject();
        c.plans.push_back({p["id"].toString(), p["title"].toString(), p["months"].toInt(), qint64(p["price_minor"].toDouble()),
                           qint64(p["old_price_minor"].toDouble()), p["discount"].toInt()});
    }
    for (const QJsonValue &v : o["gifts"].toArray()) {
        const QJsonObject g = v.toObject();
        c.gifts.push_back({g["id"].toString(), g["emoji"].toString(), g["name"].toString(), g["stars"].toInt(), g["collectible"].toBool()});
    }
    for (const QJsonValue &v : o["star_packs"].toArray()) {
        const QJsonObject s = v.toObject();
        c.starPacks.push_back({s["stars"].toInt(), qint64(s["price_minor"].toDouble())});
    }
    return c;
}

QString Catalog::money(qint64 minor) const {
    return QLocale(QLocale::Russian).toString(minor / 100.0, 'f', 2) + " " + currency;
}
