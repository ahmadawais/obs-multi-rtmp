// SPDX-License-Identifier: GPL-2.0-or-later
#include "Protocols.h"

namespace mrtmp {
namespace {

// Order here is the order they appear in the protocol picker.
constexpr Protocol kProtocols[] = {
    {"RTMP",     "RTMP",          "rtmp_output",         "rtmp_custom"},
    {"SRT_RIST", "SRT / RIST",    "ffmpeg_mpegts_muxer", "rtmp_custom"},
    {"WHIP",     "WebRTC (WHIP)", "whip_output",         "whip_custom"},
};

} // namespace

std::span<const Protocol> allProtocols() noexcept {
    return {kProtocols, std::size(kProtocols)};
}

const Protocol* findProtocol(std::string_view key) noexcept {
    for (const auto& p : kProtocols) if (p.key == key) return &p;
    return nullptr;
}

const Protocol& defaultProtocol() noexcept {
    return kProtocols[0]; // RTMP
}

} // namespace mrtmp
