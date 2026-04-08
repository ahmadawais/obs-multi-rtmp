// SPDX-License-Identifier: GPL-2.0-or-later
// Thin wrapper around OBS's blog() with a consistent plugin tag.
#pragma once

#include <obs-module.h>

#define MRTMP_TAG "[obs-multi-rtmp] "

#define MRTMP_LOG(level, fmt, ...) blog((level), MRTMP_TAG fmt, ##__VA_ARGS__)
#define MRTMP_INFO(fmt, ...)  MRTMP_LOG(LOG_INFO,    fmt, ##__VA_ARGS__)
#define MRTMP_WARN(fmt, ...)  MRTMP_LOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define MRTMP_ERROR(fmt, ...) MRTMP_LOG(LOG_ERROR,   fmt, ##__VA_ARGS__)
#define MRTMP_DEBUG(fmt, ...) MRTMP_LOG(LOG_DEBUG,   fmt, ##__VA_ARGS__)
