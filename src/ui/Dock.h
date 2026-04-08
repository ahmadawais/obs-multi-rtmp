// SPDX-License-Identifier: GPL-2.0-or-later
//
// Dock — the main plugin widget registered with OBS.
//
// Owns the config model and the list of TargetRow widgets. All persistence
// goes through here.
#pragma once

#include <memory>
#include <vector>

#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <obs-frontend-api.h>

#include "../Config.h"

namespace mrtmp {

class TargetRow;
class OutputSession;

class Dock : public QWidget {
    Q_OBJECT
public:
    explicit Dock(QWidget* parent = nullptr);
    ~Dock() override;

    // Called from the OBS frontend event thread (the main UI thread in
    // practice). Forwards state changes to every row.
    void handleObsEvent(obs_frontend_event ev);

    // Config load/save entry points — called from the frontend callback and
    // from internal row mutations.
    void reloadFromProfile();
    void saveToProfile();

private slots:
    void addTarget();
    void startAll();
    void stopAll();
    void onRowsMoved();

private:
    TargetRow* addRowForTarget(const TargetId& id);
    void       removeRowForTarget(const TargetId& id);
    void       clearRows();

    // ----- state -----
    MultiOutputConfig       config_;
    QVBoxLayout*            rootLayout_    = nullptr;
    QListWidget*            list_          = nullptr;
    QScrollArea*            scroll_        = nullptr;
    std::vector<TargetRow*> rows_;
};

} // namespace mrtmp
