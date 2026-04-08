// SPDX-License-Identifier: GPL-2.0-or-later
//
// Plugin entry point — registers the dock with OBS Studio.
#include <QMainWindow>
#include <QMetaObject>

#include <obs-frontend-api.h>
#include <obs-module.h>

#include "Logging.h"
#include "ui/Dock.h"

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "dev"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-multi-rtmp", "en-US")
OBS_MODULE_AUTHOR("SoraYuki + rewrite")

namespace {

void onFrontendEvent(obs_frontend_event event, void* data) {
    auto* dock = static_cast<mrtmp::Dock*>(data);
    // The frontend event callback already runs on the Qt main thread, so we
    // can touch the dock directly without marshalling.
    if (dock) dock->handleObsEvent(event);
}

} // namespace

bool obs_module_load() {
    auto* mainwin = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    if (!mainwin) {
        MRTMP_ERROR("obs_frontend_get_main_window returned null");
        return false;
    }

    auto* dock = new mrtmp::Dock();
    dock->setObjectName("obs-multi-rtmp-dock");

    if (!obs_frontend_add_dock_by_id("obs-multi-rtmp-dock",
                                     obs_module_text("Title"),
                                     dock)) {
        MRTMP_ERROR("obs_frontend_add_dock_by_id failed");
        delete dock;
        return false;
    }

    obs_frontend_add_event_callback(&onFrontendEvent, dock);
    MRTMP_INFO("loaded v%s (rewrite)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload() {
    MRTMP_INFO("unloaded");
}

const char* obs_module_description() {
    return "Stream to multiple RTMP / SRT / WHIP endpoints simultaneously.";
}
