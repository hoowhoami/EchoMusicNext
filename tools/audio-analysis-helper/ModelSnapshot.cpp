// SPDX-License-Identifier: AGPL-3.0-only
#include "ModelSnapshot.h"
#include <QCryptographicHash>
#include <QFile>

namespace AudioAnalysisTool
{
QByteArray readModelSnapshot(const QString &model, const QString &name, QString *error)
{
    const bool beat = model == "beat_this";
    if (!beat && model != "htdemucs") {
        *error = "ModelUnavailable: unknown fixed model";
        return {};
    }
    const qint64 bytes = beat ? 41102271 : 91838176;
    const QByteArray hash =
        beat ? "6365ae5ba2e42922772da01fbfd0bde8dd20b7362a679018e78a5f251fe8a967"
             : "27d5331a74a4e23b18172b3d59d0378e698fd79bc0d0fed3316da77e3a37d8d1";
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly) || file.size() != bytes) {
        *error = "ModelUnavailable: pinned model size/file mismatch";
        return {};
    }
    const auto snapshot = file.read(bytes + 1);
    if (snapshot.size() != bytes ||
        QCryptographicHash::hash(snapshot, QCryptographicHash::Sha256).toHex() != hash) {
        *error = "ModelInvalid: actual snapshot SHA256 mismatch";
        return {};
    }
    error->clear();
    return snapshot;
}
} // namespace AudioAnalysisTool
