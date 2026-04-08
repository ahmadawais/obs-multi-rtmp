// SPDX-License-Identifier: GPL-2.0-or-later
//
// Protocol registry. Maps our config's "protocol" string onto the pair of
// OBS ids needed to create an output and its backing service.
#pragma once

#include <span>
#include <string_view>

namespace mrtmp {

struct Protocol {
    std::string_view key;        // stable id stored in JSON, e.g. "RTMP"
    std::string_view label;      // human-readable, shown in UI
    std::string_view outputId;   // obs_output_create id
    std::string_view serviceId;  // obs_service_create id
};

// All protocols known to the plugin, in display order.
std::span<const Protocol> allProtocols() noexcept;

// Find a protocol by key. Returns nullptr if unknown.
const Protocol* findProtocol(std::string_view key) noexcept;

// Fallback used when a config references an unknown protocol.
const Protocol& defaultProtocol() noexcept;

} // namespace mrtmp
