#include "core/device_info.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QString cpuModel() {
#if defined(Q_OS_LINUX)
    QFile f("/proc/cpuinfo");
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &l : f.readAll().split('\n'))
            if (l.startsWith("model name")) return QString::fromUtf8(l.mid(l.indexOf(':') + 1)).trimmed();
    }
#elif defined(Q_OS_WIN)
    QSettings s("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
    return s.value("ProcessorNameString").toString().trimmed();
#elif defined(Q_OS_MACOS)
    QProcess p;
    p.start("sysctl", {"-n", "machdep.cpu.brand_string"});
    if (p.waitForFinished(2000)) return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#endif
    return QSysInfo::currentCpuArchitecture();
}

static qint64 ramMegabytes() {
#if defined(Q_OS_LINUX)
    QFile f("/proc/meminfo");
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &l : f.readAll().split('\n'))
            if (l.startsWith("MemTotal:")) return l.mid(9).trimmed().split(' ').value(0).toLongLong() / 1024;
    }
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return static_cast<qint64>(st.ullTotalPhys / (1024 * 1024));
#elif defined(Q_OS_MACOS)
    QProcess p;
    p.start("sysctl", {"-n", "hw.memsize"});
    if (p.waitForFinished(2000)) return QString::fromUtf8(p.readAllStandardOutput()).trimmed().toLongLong() / (1024 * 1024);
#endif
    return 0;
}

DeviceInfo DeviceInfo::collect() {
    DeviceInfo d;
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        if (ni.flags().testFlag(QNetworkInterface::IsLoopBack) || !ni.flags().testFlag(QNetworkInterface::IsUp)) continue;
        const QString mac = ni.hardwareAddress();
        if (!mac.isEmpty() && mac != "00:00:00:00:00:00" && !d.macs.contains(mac)) d.macs << mac;
        for (const QNetworkAddressEntry &e : ni.addressEntries()) {
            const QHostAddress a = e.ip();
            if (!a.isLoopback() && !a.isLinkLocal()) d.ips << a.toString();
        }
    }
    std::sort(d.macs.begin(), d.macs.end());
    d.cpu = cpuModel();
    d.os = QSysInfo::prettyProductName();
    d.hostname = QSysInfo::machineHostName();
    d.machineId = QString::fromLatin1(QSysInfo::machineUniqueId());
    d.ramMb = ramMegabytes();
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData((d.machineId + "|" + d.macs.join(",") + "|" + d.cpu).toUtf8());
    d.fingerprint = QString::fromLatin1(h.result().toHex());
    return d;
}

QJsonObject DeviceInfo::toJson() const {
    QJsonObject o;
    o["fingerprint"] = fingerprint;
    o["macs"] = QJsonArray::fromStringList(macs);
    o["ips"] = QJsonArray::fromStringList(ips);
    o["cpu"] = cpu;
    o["os"] = os;
    o["hostname"] = hostname;
    o["machine_id"] = machineId;
    o["ram_mb"] = static_cast<double>(ramMb);
    return o;
}
