// SPDX-License-Identifier: GPL-2.0-or-later
//
// RAII smart pointers for OBS C handles.
//
// OBS ships obs.hpp with OBSOutputAutoRelease etc., but that header mixes
// reference-counting with release semantics in ways that are easy to misuse.
// These wrappers are explicit std::unique_ptr aliases with custom deleters —
// one owner, deterministic destruction, zero hidden retains.
//
// Rules:
//   * Construct by taking ownership of a freshly created handle (obs_*_create).
//   * Never assign a borrowed handle (e.g. obs_output_get_service) into these.
//   * Use .get() to pass to OBS APIs that do not take ownership.
//   * Use .release() to hand ownership to OBS APIs that *do* take ownership.

#pragma once

#include <memory>

#include <obs.h>

namespace mrtmp {

struct ObsOutputDeleter   { void operator()(obs_output_t*   p) const noexcept { if (p) obs_output_release(p); } };
struct ObsServiceDeleter  { void operator()(obs_service_t*  p) const noexcept { if (p) obs_service_release(p); } };
struct ObsEncoderDeleter  { void operator()(obs_encoder_t*  p) const noexcept { if (p) obs_encoder_release(p); } };
struct ObsDataDeleter     { void operator()(obs_data_t*     p) const noexcept { if (p) obs_data_release(p); } };
struct ObsSourceDeleter   { void operator()(obs_source_t*   p) const noexcept { if (p) obs_source_release(p); } };
struct ObsPropertiesDeleter { void operator()(obs_properties_t* p) const noexcept { if (p) obs_properties_destroy(p); } };
struct ObsViewDeleter     { void operator()(obs_view_t*     p) const noexcept { if (p) obs_view_destroy(p); } };

using UniqueOutput     = std::unique_ptr<obs_output_t,     ObsOutputDeleter>;
using UniqueService    = std::unique_ptr<obs_service_t,    ObsServiceDeleter>;
using UniqueEncoder    = std::unique_ptr<obs_encoder_t,    ObsEncoderDeleter>;
using UniqueData       = std::unique_ptr<obs_data_t,       ObsDataDeleter>;
using UniqueSource     = std::unique_ptr<obs_source_t,     ObsSourceDeleter>;
using UniqueProperties = std::unique_ptr<obs_properties_t, ObsPropertiesDeleter>;
using UniqueView       = std::unique_ptr<obs_view_t,       ObsViewDeleter>;

// Helper: make obs_data_t from a JSON string. Returns empty handle on failure
// but never crashes on null/invalid input.
inline UniqueData ObsDataFromJson(const char* json) {
    if (!json || !*json) {
        return UniqueData{obs_data_create()};
    }
    return UniqueData{obs_data_create_from_json(json)};
}

} // namespace mrtmp
