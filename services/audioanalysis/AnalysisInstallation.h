// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "AnalysisBroker.h"

namespace EchoMusic::AudioAnalysis
{
// Build-tree locations are usable only by an executable within that build tree.
// Package candidates must resolve within the real application directory's parent prefix.
// Internal links are allowed. External installations never fall back to a development cache.
Config resolveInstallation(const QString &applicationDirectory, const QString &buildRoot,
                           const Config &buildTree);
} // namespace EchoMusic::AudioAnalysis
