// SPDX-License-Identifier: GPL-2.0-or-later
//
// Config model for obs-multi-rtmp.
//
// Plain value types — no shared_ptr, no inheritance. Moveable, copyable,
// trivially reasoned about. Persisted as JSON alongside the OBS profile.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace mrtmp {

using Json = nlohmann::json;

// Strong-ish id type. Keeps semantics obvious at call sites without paying
// for a full newtype wrapper.
using TargetId = std::string;
using EncoderConfigId = std::string;

struct AudioTrackBinding {
    int mixerTrack  = 0;   // OBS mixer track index (0..5)
    int outputTrack = 0;   // index within this output
};

struct VideoEncoderConfig {
    EncoderConfigId id;
    std::string encoderId;                 // e.g. "obs_x264"
    int fpsDenominator = 1;
    Json encoderParams;                    // forwarded verbatim to obs_data
    std::optional<std::string> outputScene;
    std::optional<std::string> resolution; // "WIDTHxHEIGHT"
};

struct AudioEncoderConfig {
    EncoderConfigId id;
    std::string encoderId;                 // e.g. "ffmpeg_aac"
    Json encoderParams;
    int mixerId = 0;
    std::vector<AudioTrackBinding> tracks;
};

struct OutputTarget {
    TargetId id;
    std::string name;
    std::string protocol = "RTMP";         // see Protocols.h
    bool syncStart = false;
    bool syncStop  = false;

    Json serviceParam;  // rtmp_custom / whip_custom settings (url, key, ...)
    Json outputParam;   // obs_output_create settings

    std::optional<EncoderConfigId> videoConfig;  // nullopt → share main encoder
    std::optional<EncoderConfigId> audioConfig;
};

struct MultiOutputConfig {
    std::vector<OutputTarget>        targets;
    std::vector<VideoEncoderConfig>  videoConfigs;
    std::vector<AudioEncoderConfig>  audioConfigs;

    // Lookup helpers. Return nullptr if not found; never throw.
    OutputTarget*              findTarget(std::string_view id);
    VideoEncoderConfig*        findVideo(std::string_view id);
    AudioEncoderConfig*        findAudio(std::string_view id);
    const OutputTarget*        findTarget(std::string_view id) const;
    const VideoEncoderConfig*  findVideo(std::string_view id)  const;
    const AudioEncoderConfig*  findAudio(std::string_view id)  const;

    // Generate a new id unique within this config (across all three lists).
    [[nodiscard]] std::string generateId() const;

    // Drop video/audio encoder configs no target references.
    void pruneUnused();
};

// Serialize / deserialize. Errors are logged, never thrown.
[[nodiscard]] std::string toJson(const MultiOutputConfig& cfg);
[[nodiscard]] MultiOutputConfig fromJson(std::string_view jsonText);

// Persist to / load from `<profile dir>/obs-multi-rtmp.json`.
// Returns false on I/O failure. Save writes atomically via OBS's tmp/bak dance.
bool saveToProfile(const MultiOutputConfig& cfg);
bool loadFromProfile(MultiOutputConfig& cfg);

} // namespace mrtmp
