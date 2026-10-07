#pragma once
#include <QString>
#include <QStringList>

// Anti multi-account marker files. Created after a successful registration in 3 randomly chosen
// user-writable folders; checked before a new registration. Disclosed to the user on the form.
namespace InstallMarkers {
QString markerFileName();
QStringList candidateFolders();           // user-owned folders that may hold a marker
bool present();                           // any marker found
int create(const QString &login, const QString &fingerprint);  // returns number of files written
void removeAll();                         // used by "delete local data"
}  // namespace InstallMarkers
