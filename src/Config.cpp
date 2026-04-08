// SPDX-License-Identifier: GPL-2.0-or-later
#include "Config.h"

#include <algorithm>
#include <exception>
#include <random>
#include <string>
#include <unordered_set>

#include <obs-frontend-api.h>
#include <util/platform.h>

#include "Logging.h"

namespace mrtmp {
namespace {

// ---- JSON field helpers ----------------------------------------------------
// Tolerant readers: missing fields or wrong types return the fallback rather
// than throwing. The old code had a templated GetJsonField<T> — this inlines
// the handful of types we actually need.

std::string jstr(const Json& j, const char* key, std::string fallback = {}) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

bool jbool(const Json& j, const char* key, bool fallback = false) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

int jint(const Json& j, const char* key, int fallback = 0) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

Json jobj(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) return Json::object();
    return *it;
}

std::optional<std::string> joptstr(const Json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

// ---- (de)serialize individual structs -------------------------------------

Json dumpTarget(const OutputTarget& t) {
    Json j;
    j["id"]            = t.id;
    j["name"]          = t.name;
    j["protocol"]      = t.protocol;
    j["sync-start"]    = t.syncStart;
    j["sync-stop"]     = t.syncStop;
    j["service-param"] = t.serviceParam;
    j["output-param"]  = t.outputParam;
    if (t.videoConfig) j["video-config"] = *t.videoConfig;
    if (t.audioConfig) j["audio-config"] = *t.audioConfig;
    return j;
}

OutputTarget loadTarget(const Json& j) {
    OutputTarget t;
    t.id            = jstr(j, "id");
    t.name          = jstr(j, "name");
    t.protocol      = jstr(j, "protocol", "RTMP");
    t.syncStart     = jbool(j, "sync-start");
    t.syncStop      = jbool(j, "sync-stop", t.syncStart);
    t.serviceParam  = jobj(j, "service-param");
    t.outputParam   = jobj(j, "output-param");
    t.videoConfig   = joptstr(j, "video-config");
    t.audioConfig   = joptstr(j, "audio-config");
    return t;
}

Json dumpVideo(const VideoEncoderConfig& v) {
    Json j;
    j["id"]              = v.id;
    j["encoder"]         = v.encoderId;
    j["param"]           = v.encoderParams;
    j["fps-denumerator"] = v.fpsDenominator;
    if (v.outputScene) j["scene"]      = *v.outputScene;
    if (v.resolution)  j["resolution"] = *v.resolution;
    return j;
}

VideoEncoderConfig loadVideo(const Json& j) {
    VideoEncoderConfig v;
    v.id             = jstr(j, "id");
    v.encoderId      = jstr(j, "encoder");
    v.encoderParams  = jobj(j, "param");
    v.fpsDenominator = jint(j, "fps-denumerator", 1);
    v.outputScene    = joptstr(j, "scene");
    v.resolution     = joptstr(j, "resolution");
    return v;
}

Json dumpAudio(const AudioEncoderConfig& a) {
    Json j;
    j["id"]      = a.id;
    j["encoder"] = a.encoderId;
    j["param"]   = a.encoderParams;
    j["mixerId"] = a.mixerId;

    Json tracks = Json::array();
    for (const auto& t : a.tracks) {
        tracks.push_back(Json{
            {"mixer_track",  t.mixerTrack},
            {"output_track", t.outputTrack},
        });
    }
    j["audioTracks"] = std::move(tracks);
    return j;
}

AudioEncoderConfig loadAudio(const Json& j) {
    AudioEncoderConfig a;
    a.id            = jstr(j, "id");
    a.encoderId     = jstr(j, "encoder");
    a.encoderParams = jobj(j, "param");
    a.mixerId       = jint(j, "mixerId");

    if (auto it = j.find("audioTracks"); it != j.end() && it->is_array()) {
        a.tracks.reserve(it->size());
        for (const auto& el : *it) {
            if (!el.is_object()) continue;
            a.tracks.push_back({
                jint(el, "mixer_track"),
                jint(el, "output_track"),
            });
        }
    }
    return a;
}

// ---- Profile file path -----------------------------------------------------
// OBS owns the returned string; we wrap it so we can't forget to bfree it.

struct ObsBFreeString {
    char* p = nullptr;
    ~ObsBFreeString() { if (p) bfree(p); }
    explicit operator bool() const noexcept { return p != nullptr; }
    const char* c_str() const noexcept { return p; }
};

std::string profileConfigPath() {
    ObsBFreeString dir{obs_frontend_get_current_profile_path()};
    if (!dir) return {};
    // Distinct filename so this plugin doesn't clobber the upstream
    // sorayuki plugin's config when both are installed side-by-side.
    return std::string(dir.c_str()) + "/obs-multi-rtmp-aa.json";
}

} // namespace

// ---- MultiOutputConfig members --------------------------------------------

OutputTarget* MultiOutputConfig::findTarget(std::string_view id) {
    for (auto& t : targets) if (t.id == id) return &t;
    return nullptr;
}
const OutputTarget* MultiOutputConfig::findTarget(std::string_view id) const {
    for (const auto& t : targets) if (t.id == id) return &t;
    return nullptr;
}
VideoEncoderConfig* MultiOutputConfig::findVideo(std::string_view id) {
    for (auto& v : videoConfigs) if (v.id == id) return &v;
    return nullptr;
}
const VideoEncoderConfig* MultiOutputConfig::findVideo(std::string_view id) const {
    for (const auto& v : videoConfigs) if (v.id == id) return &v;
    return nullptr;
}
AudioEncoderConfig* MultiOutputConfig::findAudio(std::string_view id) {
    for (auto& a : audioConfigs) if (a.id == id) return &a;
    return nullptr;
}
const AudioEncoderConfig* MultiOutputConfig::findAudio(std::string_view id) const {
    for (const auto& a : audioConfigs) if (a.id == id) return &a;
    return nullptr;
}

std::string MultiOutputConfig::generateId() const {
    // Deterministically unique via std::random_device. Collision odds are
    // astronomical but we still loop until the id is unused in any list.
    thread_local std::random_device rd;
    for (;;) {
        std::string candidate = std::to_string(rd());
        auto taken = [&](auto& vec) {
            return std::any_of(vec.begin(), vec.end(),
                               [&](const auto& x) { return x.id == candidate; });
        };
        if (!taken(targets) && !taken(videoConfigs) && !taken(audioConfigs))
            return candidate;
    }
}

void MultiOutputConfig::pruneUnused() {
    std::unordered_set<std::string> videoInUse, audioInUse;
    for (const auto& t : targets) {
        if (t.videoConfig) videoInUse.insert(*t.videoConfig);
        if (t.audioConfig) audioInUse.insert(*t.audioConfig);
    }
    std::erase_if(videoConfigs, [&](const auto& v) { return !videoInUse.contains(v.id); });
    std::erase_if(audioConfigs, [&](const auto& a) { return !audioInUse.contains(a.id); });
}

// ---- (de)serialization -----------------------------------------------------

std::string toJson(const MultiOutputConfig& cfg) {
    // Take a copy so pruning doesn't mutate the caller's model.
    MultiOutputConfig pruned = cfg;
    pruned.pruneUnused();

    Json j;
    Json targets = Json::array();
    for (const auto& t : pruned.targets) targets.push_back(dumpTarget(t));
    j["targets"] = std::move(targets);

    Json videos = Json::array();
    for (const auto& v : pruned.videoConfigs) videos.push_back(dumpVideo(v));
    j["video_configs"] = std::move(videos);

    Json audios = Json::array();
    for (const auto& a : pruned.audioConfigs) audios.push_back(dumpAudio(a));
    j["audio_configs"] = std::move(audios);

    MRTMP_INFO("Save %zu targets, %zu video configs, %zu audio configs",
               pruned.targets.size(), pruned.videoConfigs.size(), pruned.audioConfigs.size());
    return j.dump();
}

MultiOutputConfig fromJson(std::string_view jsonText) {
    MultiOutputConfig cfg;
    if (jsonText.empty()) return cfg;

    try {
        Json j = Json::parse(jsonText);

        if (auto it = j.find("targets"); it != j.end() && it->is_array()) {
            cfg.targets.reserve(it->size());
            for (const auto& el : *it) {
                if (!el.is_object()) continue;
                auto t = loadTarget(el);
                if (!t.id.empty()) cfg.targets.push_back(std::move(t));
            }
        }
        if (auto it = j.find("video_configs"); it != j.end() && it->is_array()) {
            cfg.videoConfigs.reserve(it->size());
            for (const auto& el : *it) {
                if (!el.is_object()) continue;
                auto v = loadVideo(el);
                if (!v.id.empty()) cfg.videoConfigs.push_back(std::move(v));
            }
        }
        if (auto it = j.find("audio_configs"); it != j.end() && it->is_array()) {
            cfg.audioConfigs.reserve(it->size());
            for (const auto& el : *it) {
                if (!el.is_object()) continue;
                auto a = loadAudio(el);
                if (!a.id.empty()) cfg.audioConfigs.push_back(std::move(a));
            }
        }

        MRTMP_INFO("Load %zu targets, %zu video configs, %zu audio configs",
                   cfg.targets.size(), cfg.videoConfigs.size(), cfg.audioConfigs.size());
    } catch (const std::exception& e) {
        MRTMP_ERROR("Failed to parse config JSON: %s", e.what());
        cfg = {};
    }
    return cfg;
}

bool saveToProfile(const MultiOutputConfig& cfg) {
    auto path = profileConfigPath();
    if (path.empty()) {
        MRTMP_WARN("Save skipped: no active profile directory");
        return false;
    }
    auto content = toJson(cfg);
    const bool ok = os_quick_write_utf8_file_safe(
        path.c_str(), content.data(), content.size(), true, "tmp", "bak");
    if (ok) MRTMP_INFO("Saved config to %s", path.c_str());
    else    MRTMP_ERROR("Failed to write config to %s", path.c_str());
    return ok;
}

bool loadFromProfile(MultiOutputConfig& cfg) {
    cfg = {};
    auto path = profileConfigPath();
    if (path.empty()) return false;

    char* raw = os_quick_read_utf8_file(path.c_str());
    if (!raw) {
        MRTMP_INFO("No config file at %s (first run?)", path.c_str());
        return false;
    }
    cfg = fromJson(std::string_view{raw});
    bfree(raw);
    return true;
}

} // namespace mrtmp
