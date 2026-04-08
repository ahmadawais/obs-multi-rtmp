// SPDX-License-Identifier: GPL-2.0-or-later
#include "OutputSession.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <regex>
#include <utility>

#include <obs-frontend-api.h>

#include "Logging.h"
#include "Protocols.h"

namespace mrtmp {
namespace {

constexpr auto kSharedMainEncoder = std::string_view{""}; // nullopt video/audio
constexpr const char* kOutputName = "obs-multi-rtmp-output";

std::string videoEncoderName(std::string_view configId) {
    return std::string("obs-multi-rtmp-venc-") + std::string(configId);
}
std::string audioEncoderName(std::string_view configId, int track) {
    return std::string("obs-multi-rtmp-aenc-") + std::string(configId)
         + "-t" + std::to_string(track);
}

struct Resolution { int width; int height; };
std::optional<Resolution> parseResolution(const std::optional<std::string>& raw) {
    if (!raw) return std::nullopt;
    static const std::regex kPattern(R"(\s*(\d{1,5})\s*x\s*(\d{1,5})\s*)");
    std::smatch m;
    if (!std::regex_match(*raw, m, kPattern)) return std::nullopt;
    return Resolution{std::stoi(m[1]), std::stoi(m[2])};
}

} // namespace

// ---- ctor / dtor -----------------------------------------------------------

OutputSession::OutputSession(MultiOutputConfig& config, TargetId targetId, UiPoster poster)
    : config_(config)
    , targetId_(std::move(targetId))
    , poster_(std::move(poster))
{}

OutputSession::~OutputSession() {
    teardown();
}

void OutputSession::setListener(SessionListener listener) {
    listener_ = std::move(listener);
}

// ---- public control --------------------------------------------------------

bool OutputSession::isActive() const noexcept {
    return output_ && obs_output_active(output_.get());
}

bool OutputSession::start() {
    if (isActive()) return true;

    const OutputTarget* target = config_.findTarget(targetId_);
    if (!target) {
        MRTMP_ERROR("start(): target id not found: %s", targetId_.c_str());
        emit({SessionState::Stopped, "target not found", -1});
        return false;
    }

    // Always tear down first: OBS's output objects are single-shot in practice.
    teardown();

    if (!buildOutput(*target))    { emit({SessionState::Stopped, "failed to create output", -1});  return false; }
    if (!buildService(*target))   { emit({SessionState::Stopped, "failed to create service", -1}); teardown(); return false; }
    if (!buildEncoders(*target))  { emit({SessionState::Stopped, "failed to create encoder", -1}); teardown(); return false; }
    if (!attachVideoSource(*target)) { emit({SessionState::Stopped, "scene not found", -1});       teardown(); return false; }

    if (!obs_output_start(output_.get())) {
        const char* err = obs_output_get_last_error(output_.get());
        MRTMP_ERROR("obs_output_start failed: %s", err ? err : "(null)");
        emit({SessionState::Stopped, err ? err : "start failed", -1});
        teardown();
        return false;
    }
    return true;
}

void OutputSession::stop(bool force) {
    if (!output_ || !obs_output_active(output_.get())) return;
    if (force) obs_output_force_stop(output_.get());
    else       obs_output_stop(output_.get());
}

// ---- build helpers ---------------------------------------------------------

bool OutputSession::buildOutput(const OutputTarget& target) {
    const Protocol* proto = findProtocol(target.protocol);
    if (!proto) {
        MRTMP_WARN("Unknown protocol '%s', falling back to %.*s",
                   target.protocol.c_str(),
                   (int)defaultProtocol().key.size(), defaultProtocol().key.data());
        proto = &defaultProtocol();
    }

    UniqueData settings = ObsDataFromJson(target.outputParam.dump().c_str());
    // obs_output_create takes a non-null-terminated C string id, so copy.
    const std::string outId{proto->outputId};
    output_.reset(obs_output_create(outId.c_str(), kOutputName, settings.get(), nullptr));
    if (!output_) {
        MRTMP_ERROR("obs_output_create(%s) returned null", outId.c_str());
        return false;
    }

    // Inherit delay settings from the profile — match main streaming output.
    usingDelay_ = false;
    if (config_t* profile = obs_frontend_get_profile_config()) {
        const bool enable   = config_get_bool(profile, "Output", "DelayEnable");
        const bool preserve = config_get_bool(profile, "Output", "DelayPreserve");
        const int  secs     = (int)config_get_int(profile, "Output", "DelaySec");
        obs_output_set_delay(output_.get(),
                             enable ? secs : 0,
                             preserve ? OBS_OUTPUT_DELAY_PRESERVE : 0);
        usingDelay_ = enable && secs > 0;
    }

    connectSignals();
    return true;
}

bool OutputSession::buildService(const OutputTarget& target) {
    const Protocol* proto = findProtocol(target.protocol);
    if (!proto) proto = &defaultProtocol();

    UniqueData settings = ObsDataFromJson(target.serviceParam.dump().c_str());
    const std::string svcId{proto->serviceId};
    service_.reset(obs_service_create(svcId.c_str(), "obs-multi-rtmp-service",
                                      settings.get(), nullptr));
    if (!service_) return false;
    // obs_output_set_service does NOT take ownership — the session keeps it.
    obs_output_set_service(output_.get(), service_.get());
    return true;
}

obs_encoder_t* OutputSession::resolveVideoEncoder(const OutputTarget& target, bool& borrowed) {
    borrowed = false;
    if (!target.videoConfig) {
        // Share the main streaming output's video encoder.
        obs_output_t* main = obs_frontend_get_streaming_output();
        if (!main) return nullptr;
        obs_encoder_t* enc = obs_output_get_video_encoder(main);
        obs_output_release(main);
        borrowed = true;
        return enc;
    }

    const auto* vcfg = config_.findVideo(*target.videoConfig);
    if (!vcfg) {
        MRTMP_ERROR("Video encoder config '%s' not found — falling back to main",
                    target.videoConfig->c_str());
        return resolveVideoEncoder({.videoConfig = std::nullopt}, borrowed);
    }

    UniqueData settings = ObsDataFromJson(vcfg->encoderParams.dump().c_str());
    const std::string name = videoEncoderName(vcfg->id);
    ownedVideo_.reset(obs_video_encoder_create(
        vcfg->encoderId.c_str(), name.c_str(), settings.get(), nullptr));
    if (!ownedVideo_) return nullptr;

    if (auto res = parseResolution(vcfg->resolution)) {
        obs_encoder_set_gpu_scale_type(ownedVideo_.get(), OBS_SCALE_BICUBIC);
        obs_encoder_set_scaled_size(ownedVideo_.get(), res->width, res->height);
    }
    if (vcfg->fpsDenominator > 1) {
        obs_encoder_set_frame_rate_divisor(ownedVideo_.get(), vcfg->fpsDenominator);
    }
    return ownedVideo_.get();
}

obs_encoder_t* OutputSession::resolveAudioEncoder(const OutputTarget& target,
                                                  int trackIdx,
                                                  int mixerIdOverride,
                                                  bool& borrowed)
{
    borrowed = false;
    if (!target.audioConfig) {
        obs_output_t* main = obs_frontend_get_streaming_output();
        if (!main) return nullptr;
        obs_encoder_t* enc = obs_output_get_audio_encoder(main, 0);
        obs_output_release(main);
        borrowed = true;
        return enc;
    }

    const auto* acfg = config_.findAudio(*target.audioConfig);
    if (!acfg) {
        MRTMP_ERROR("Audio encoder config '%s' not found — falling back to main",
                    target.audioConfig->c_str());
        return resolveAudioEncoder({.audioConfig = std::nullopt}, trackIdx, mixerIdOverride, borrowed);
    }

    UniqueData settings = ObsDataFromJson(acfg->encoderParams.dump().c_str());
    const std::string name = audioEncoderName(acfg->id, trackIdx);
    const int mixerId = (mixerIdOverride >= 0) ? mixerIdOverride : acfg->mixerId;

    UniqueEncoder enc{obs_audio_encoder_create(
        acfg->encoderId.c_str(), name.c_str(), settings.get(), mixerId, nullptr)};
    if (!enc) return nullptr;

    obs_encoder_t* raw = enc.get();
    if (trackIdx == 0) ownedAudio_ = std::move(enc);
    else               ownedExtraAudio_.push_back(std::move(enc));
    return raw;
}

bool OutputSession::buildEncoders(const OutputTarget& target) {
    bool videoBorrowed = false, audioBorrowed = false;
    obs_encoder_t* venc = resolveVideoEncoder(target, videoBorrowed);
    obs_encoder_t* aenc = resolveAudioEncoder(target, 0, -1, audioBorrowed);
    if (!venc || !aenc) {
        MRTMP_ERROR("Encoder resolution failed (video=%p audio=%p)", (void*)venc, (void*)aenc);
        return false;
    }

    // obs_output_set_*_encoder adds a ref; we keep the Unique* wrappers for
    // the ones we created so destruction balances.
    obs_output_set_video_encoder(output_.get(), venc);
    obs_output_set_audio_encoder(output_.get(), aenc, 0);

    // Extra audio tracks (from AudioEncoderConfig::tracks).
    if (target.audioConfig) {
        if (const auto* acfg = config_.findAudio(*target.audioConfig)) {
            for (const auto& binding : acfg->tracks) {
                bool borrowed = false;
                obs_encoder_t* extra = resolveAudioEncoder(
                    target, binding.outputTrack, binding.mixerTrack, borrowed);
                if (extra) {
                    obs_output_set_audio_encoder(output_.get(), extra, binding.outputTrack);
                }
            }
        }
    }
    return true;
}

bool OutputSession::attachVideoSource(const OutputTarget& target) {
    obs_encoder_t* venc = obs_output_get_video_encoder(output_.get());
    if (!venc) return false;

    // Default: main program feed.
    if (!target.videoConfig) {
        obs_encoder_set_video(venc, obs_get_video());
    } else {
        const auto* vcfg = config_.findVideo(*target.videoConfig);
        if (vcfg && vcfg->outputScene) {
            UniqueSource scene{obs_get_source_by_name(vcfg->outputScene->c_str())};
            if (!scene) {
                MRTMP_ERROR("Output scene '%s' not found", vcfg->outputScene->c_str());
                return false;
            }
            sceneView_.reset(obs_view_create());
            obs_view_set_source(sceneView_.get(), 0, scene.get());
            obs_source_inc_active(scene.get());
            video_t* v = obs_view_add(sceneView_.get());
            obs_encoder_set_video(venc, v);
        } else {
            obs_encoder_set_video(venc, obs_get_video());
        }
    }

    // Audio: main audio bus is fine for every track.
    for (int i = 0; i < MAX_OUTPUT_AUDIO_MIXES; ++i) {
        if (obs_encoder_t* aenc = obs_output_get_audio_encoder(output_.get(), i))
            obs_encoder_set_audio(aenc, obs_get_audio());
    }
    return true;
}

// ---- teardown --------------------------------------------------------------

void OutputSession::teardown() {
    if (output_) {
        disconnectSignals();
        if (obs_output_active(output_.get()))
            obs_output_force_stop(output_.get());
        obs_output_set_service(output_.get(), nullptr);
    }

    // Drop scene view (decrement source active ref first).
    if (sceneView_) {
        UniqueSource src{obs_view_get_source(sceneView_.get(), 0)};
        if (src) obs_source_dec_active(src.get());
        obs_view_set_source(sceneView_.get(), 0, nullptr);
        obs_view_remove(sceneView_.get());
        sceneView_.reset();
    }

    // Release order matters: output → encoders → service.
    output_.reset();
    ownedVideo_.reset();
    ownedAudio_.reset();
    ownedExtraAudio_.clear();
    service_.reset();

    state_ = SessionState::Idle;
    usingDelay_ = false;
}

// ---- signal wiring ---------------------------------------------------------

void OutputSession::connectSignals() {
    signal_handler_t* sh = obs_output_get_signal_handler(output_.get());
    if (!sh) return;
    signal_handler_connect(sh, "starting",          &OutputSession::onStartingCb,    this);
    signal_handler_connect(sh, "start",             &OutputSession::onStartedCb,     this);
    signal_handler_connect(sh, "stopping",          &OutputSession::onStoppingCb,    this);
    signal_handler_connect(sh, "stop",              &OutputSession::onStoppedCb,     this);
    signal_handler_connect(sh, "reconnect",         &OutputSession::onReconnectCb,   this);
    signal_handler_connect(sh, "reconnect_success", &OutputSession::onReconnectedCb, this);
}

void OutputSession::disconnectSignals() {
    signal_handler_t* sh = obs_output_get_signal_handler(output_.get());
    if (!sh) return;
    signal_handler_disconnect(sh, "starting",          &OutputSession::onStartingCb,    this);
    signal_handler_disconnect(sh, "start",             &OutputSession::onStartedCb,     this);
    signal_handler_disconnect(sh, "stopping",          &OutputSession::onStoppingCb,    this);
    signal_handler_disconnect(sh, "stop",              &OutputSession::onStoppedCb,     this);
    signal_handler_disconnect(sh, "reconnect",         &OutputSession::onReconnectCb,   this);
    signal_handler_disconnect(sh, "reconnect_success", &OutputSession::onReconnectedCb, this);
}

// Static trampolines post to the UI thread immediately — we do NOT touch Qt
// or listener_ from the OBS encoder thread.
void OutputSession::onStartingCb(void* ctx, calldata_t*) {
    auto* self = static_cast<OutputSession*>(ctx);
    self->poster_([self]{ self->transition(SessionState::Starting); });
}
void OutputSession::onStartedCb(void* ctx, calldata_t*) {
    auto* self = static_cast<OutputSession*>(ctx);
    self->poster_([self]{
        self->lastSampleAt_ = std::chrono::steady_clock::now();
        self->lastBytes_ = self->lastFrames_ = 0;
        self->smoothedBps_ = self->smoothedFps_ = 0;
        self->transition(SessionState::Running);
    });
}
void OutputSession::onStoppingCb(void* ctx, calldata_t*) {
    auto* self = static_cast<OutputSession*>(ctx);
    self->poster_([self]{ self->transition(SessionState::Stopping); });
}
void OutputSession::onStoppedCb(void* ctx, calldata_t* cd) {
    auto* self = static_cast<OutputSession*>(ctx);
    const int code = (int)calldata_int(cd, "code");
    self->poster_([self, code]{
        SessionEvent ev{SessionState::Stopped, {}, code};
        switch (code) {
            case 0:  ev.message = "";                          break;
            case OBS_OUTPUT_BAD_PATH:        ev.message = "bad url";               break;
            case OBS_OUTPUT_CONNECT_FAILED:  ev.message = "connect failed";        break;
            case OBS_OUTPUT_INVALID_STREAM:  ev.message = "invalid stream";        break;
            case OBS_OUTPUT_ERROR:           ev.message = "output error";          break;
            case OBS_OUTPUT_DISCONNECTED:    ev.message = "disconnected";          break;
            case OBS_OUTPUT_UNSUPPORTED:     ev.message = "unsupported";           break;
            case OBS_OUTPUT_NO_SPACE:        ev.message = "no space";              break;
            case OBS_OUTPUT_ENCODE_ERROR:    ev.message = "encode error";          break;
            default:                         ev.message = "unknown error";         break;
        }
        self->state_ = SessionState::Stopped;
        if (self->listener_) self->listener_(ev);
    });
}
void OutputSession::onReconnectCb(void* ctx, calldata_t*) {
    auto* self = static_cast<OutputSession*>(ctx);
    self->poster_([self]{ self->transition(SessionState::Reconnecting); });
}
void OutputSession::onReconnectedCb(void* ctx, calldata_t*) {
    auto* self = static_cast<OutputSession*>(ctx);
    self->poster_([self]{ self->transition(SessionState::Running); });
}

void OutputSession::emit(SessionEvent ev) {
    if (listener_) listener_(ev);
}

void OutputSession::transition(SessionState next) {
    state_ = next;
    emit({next, {}, 0});
}

// ---- stats sampling --------------------------------------------------------

SessionStats OutputSession::sampleStats() {
    using clock = std::chrono::steady_clock;
    SessionStats s;
    if (!output_) return s;

    const auto now        = clock::now();
    const auto newBytes   = obs_output_get_total_bytes(output_.get());
    const auto newFrames  = (std::uint64_t)obs_output_get_total_frames(output_.get());

    const double dt = std::chrono::duration<double>(now - lastSampleAt_).count();
    if (dt > 0.01) {
        const double bps = (double)(newBytes  - lastBytes_)  * 8.0 / dt;
        const double fps = (double)(newFrames - lastFrames_) /       dt;
        // One-pole IIR ≈ 30% fresh / 70% history. Feels steady on screen
        // without obscuring real bitrate spikes.
        smoothedBps_ = smoothedBps_ == 0 ? bps : smoothedBps_ * 0.7 + bps * 0.3;
        smoothedFps_ = smoothedFps_ == 0 ? fps : smoothedFps_ * 0.7 + fps * 0.3;
        lastSampleAt_ = now;
        lastBytes_    = newBytes;
        lastFrames_   = newFrames;
    }

    s.totalBytes      = newBytes;
    s.totalFrames     = newFrames;
    s.bitsPerSecond   = smoothedBps_;
    s.framesPerSecond = smoothedFps_;
    return s;
}

} // namespace mrtmp
