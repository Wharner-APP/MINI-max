#pragma once
#include <QByteArray>
#include <QString>

#include "core/context.h"

class ChatsModel;

// Local, device-bound storage. Nothing here is sent to the server.
namespace SessionStore {
// Session file = JSON sealed with a key derived from this machine's id ("stay signed in").
bool save(const QString &dir, const Session &s, const QByteArray &localKey);
bool load(const QString &dir, Session *s, QByteArray *localKey);
void clear(const QString &dir);
// Chats file = JSON sealed with the password-derived local key.
bool saveChats(const QString &dir, const ChatsModel &m, const QByteArray &localKey);
bool loadChats(const QString &dir, ChatsModel *m, const QByteArray &localKey);
}
