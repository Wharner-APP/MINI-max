#pragma once
#include <QString>
#include <QVector>

// Premium plans, star packs and gifts. Defaults come from the built-in :/catalog.json;
// configs/catalog.json (if present) overrides them; the server can replace them later.
struct PremiumPlan { QString id, title; int months = 0; qint64 priceMinor = 0, oldPriceMinor = 0; int discount = 0; };
struct GiftItem { QString id, emoji, name; int stars = 0; bool collectible = false; };
struct StarPack { int stars = 0; qint64 priceMinor = 0; };
struct Catalog {
    QString currency = "RUB";
    QVector<PremiumPlan> plans;
    QVector<GiftItem> gifts;
    QVector<StarPack> starPacks;
    qint64 businessMonthlyMinor = 0;
    static Catalog load(const QString &configDir);
    QString money(qint64 minor) const;     // "124,58 RUB"
};
