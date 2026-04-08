// SPDX-License-Identifier: GPL-2.0-or-later
//
// PropertiesView — renders an obs_properties_t into a Qt form, editing an
// obs_data_t in place. Supports the property types we actually need for
// service + encoder configuration (strings, numbers, bools, lists, paths).
//
// This is a drastically slimmed-down replacement for the old
// obs-properties-widget.cpp (417 LOC → ~200 LOC) — it only supports what the
// edit dialog uses and drops property modification callbacks that the old
// code rarely exercised.
#pragma once

#include <memory>
#include <vector>

#include <QFormLayout>
#include <QWidget>

#include <obs.h>

#include "../ObsPtr.h"

namespace mrtmp {

class PropertiesView : public QWidget {
    Q_OBJECT
public:
    // `properties` is consumed — the view takes ownership. `settings` is
    // shared with the caller and mutated in place as the user edits.
    PropertiesView(UniqueProperties properties,
                   obs_data_t* settings,
                   QWidget* parent = nullptr);
    ~PropertiesView() override;

    // Push the current widget state back into the settings obs_data_t.
    // The form is reactive for most widgets, but call this before reading
    // settings to be safe.
    void flush();

private:
    void buildForm();
    void addProperty(obs_property_t* prop, QFormLayout* form);

    UniqueProperties properties_;
    obs_data_t*      settings_;  // not owned
    std::vector<std::function<void()>> flushers_;
};

} // namespace mrtmp
