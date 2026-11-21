#pragma once
#include "PlaybackPayloadLease.h"

namespace EchoMusic
{
/// Pins the immutable versions in one published history manifest. Dataset
/// generations and admitted reads share this token; it never owns song JSON.
using HistoryPayloadSnapshot = PlaybackPayloadSnapshot;
using HistoryPayloadLease = PlaybackPayloadLease;
} // namespace EchoMusic
