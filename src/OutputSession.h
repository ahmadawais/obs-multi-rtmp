// SPDX-License-Identifier: GPL-2.0-or-later
//
// OutputSession — one logical streaming target.
//
// Owns the obs_output_t, its obs_service_t, any per-target encoders and the
// optional per-target obs_view_t scene feed. All OBS callbacks are marshalled
// back onto the UI thread via a user-supplied posting function, so the UI
// layer never has to touch signal_handler_connect.
//
// State machine:
//     Idle → Starting → Running ↔ Reconnecting
//          ↘                     ↗
//            Stopping → Stopped → Idle
//
// start() and stop() are safe to call repeatedly; they are no-ops in the
// wrong state.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <obs.h>

#include "Config.h"
#include "ObsPtr.h"

namespace mrtmp {

enum class SessionState {
    Idle,
    Starting,
    Running,
    Reconnecting,
    Stopping,
    Stopped,
};

struct SessionStats {
    std::chrono::steady_clock::time_point startedAt{};
    std::uint64_t totalBytes  = 0;
    std::uint64_t totalFrames = 0;
    double bitsPerSecond = 0.0;   // exponentially smoothed
    double framesPerSecond = 0.0;
};

// Event payload pushed to the UI. `message` is already localized.
struct SessionEvent {
    SessionState state;
    std::string message;
    int stopCode = 0;    // only meaningful when state == Stopped
};

using SessionListener = std::function<void(const SessionEvent&)>;
using UiPoster        = std::function<void(std::function<void()>)>;

class OutputSession {
public:
    // `config` must outlive the session — it is the authoritative store and
    // is consulted at start() time for the latest settings. `poster` is used
    // to hop OBS callbacks onto the UI thread.
    OutputSession(MultiOutputConfig& config, TargetId targetId, UiPoster poster);
    ~OutputSession();

    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;

    void setListener(SessionListener listener);

    // Attempt to start streaming. Returns false and emits a Stopped event on
    // synchronous failure (e.g. missing encoder). Async success/failure comes
    // through the listener.
    bool start();

    // Graceful stop. If `force` is true, uses obs_output_force_stop, dropping
    // any encoder delay buffer.
    void stop(bool force = false);

    [[nodiscard]] bool         isActive() const noexcept;
    [[nodiscard]] SessionState state()    const noexcept { return state_; }
    [[nodiscard]] SessionStats sampleStats();        // call periodically from UI
    [[nodiscard]] std::string_view targetId() const noexcept { return targetId_; }
    [[nodiscard]] bool         usingDelay()  const noexcept { return usingDelay_; }

private:
    // --- construction / teardown helpers ---
    bool buildOutput(const OutputTarget& target);
    bool buildService(const OutputTarget& target);
    bool buildEncoders(const OutputTarget& target);
    bool attachVideoSource(const OutputTarget& target);
    void teardown();

    // --- encoder resolution ---
    // Returns either a freshly created encoder we own, or a borrowed encoder
    // from the main streaming/recording output.
    obs_encoder_t* resolveVideoEncoder(const OutputTarget& target, bool& borrowed);
    obs_encoder_t* resolveAudioEncoder(const OutputTarget& target,
                                       int trackIdx,
                                       int mixerIdOverride,
                                       bool& borrowed);

    // --- signal callbacks (static → member dispatch) ---
    void connectSignals();
    void disconnectSignals();
    static void onStartingCb   (void* ctx, calldata_t*);
    static void onStartedCb    (void* ctx, calldata_t*);
    static void onStoppingCb   (void* ctx, calldata_t*);
    static void onStoppedCb    (void* ctx, calldata_t*);
    static void onReconnectCb  (void* ctx, calldata_t*);
    static void onReconnectedCb(void* ctx, calldata_t*);

    // NOTE: deliberately NOT named `emit` — Qt defines that as an empty
    // macro, which would mangle this declaration whenever a translation
    // unit includes a Qt header before this one.
    void emitEvent(SessionEvent ev);
    void transition(SessionState next);

    // --- members ---
    MultiOutputConfig& config_;
    TargetId           targetId_;
    UiPoster           poster_;
    SessionListener    listener_;

    // These are intentionally owned raw by us (via Unique*) so destruction
    // order is explicit: output first, then service/view, then encoders.
    UniqueOutput  output_;
    UniqueService service_;
    UniqueView    sceneView_;

    // Encoders we created ourselves (must be released on teardown).
    UniqueEncoder ownedVideo_;
    UniqueEncoder ownedAudio_;                     // primary track
    std::vector<UniqueEncoder> ownedExtraAudio_;   // additional tracks

    SessionState state_ = SessionState::Idle;
    bool usingDelay_    = false;

    // Stats sampling state.
    std::chrono::steady_clock::time_point lastSampleAt_{};
    std::uint64_t lastBytes_  = 0;
    std::uint64_t lastFrames_ = 0;
    double smoothedBps_ = 0.0;
    double smoothedFps_ = 0.0;
};

} // namespace mrtmp
