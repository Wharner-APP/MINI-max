#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Identifiers sent to the server at registration (disclosed to the user on the registration form).
struct DeviceInfo {
    QStringList macs, ips;
    QString cpu, os, hostname, machineId;
    qint64 ramMb = 0;
    QString fingerprint;  // sha256 over stable identifiers
    static DeviceInfo collect();
    QJsonObject toJson() const;
};
