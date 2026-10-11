#pragma once
#include <QString>

namespace MmPaths {
// Root of the portable installation (folder with the executables; the parent of *.app on macOS).
QString baseDir();
// <base>/configs, with fallbacks for running from a build tree.
QString configDir();
// Per-user writable data directory (messages, session). `override` wins when not empty.
QString dataDir(const QString &override = {});
}  // namespace MmPaths
