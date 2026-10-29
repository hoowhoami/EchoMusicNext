// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QByteArray>
#include <QString>

namespace AudioAnalysisTool
{
// The immutable bytes returned here are the only bytes permitted for manifest/weight use.
QByteArray readModelSnapshot(const QString &model, const QString &file, QString *error);
} // namespace AudioAnalysisTool
