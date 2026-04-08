// SPDX-License-Identifier: GPL-2.0-or-later
//
// TargetRow — one row in the dock's list. Owns its own OutputSession.
#pragma once

#include <memory>

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <obs-frontend-api.h>

#include "../Config.h"
#include "../OutputSession.h"

namespace mrtmp {

class Dock;

class TargetRow : public QWidget {
    Q_OBJECT
public:
    TargetRow(Dock* dock, MultiOutputConfig& config, TargetId id, QWidget* parent = nullptr);
    ~TargetRow() override;

    [[nodiscard]] const TargetId& targetId() const noexcept { return targetId_; }
    [[nodiscard]] QPushButton* deleteButton() const noexcept { return btnDelete_; }

    // Dock forwards OBS frontend events (for sync start/stop + safe shutdown).
    void handleObsEvent(obs_frontend_event ev);

    void startStreaming();
    void stopStreaming();
    bool editInDialog();     // shows modal edit dialog, returns true on accept
    void refreshFromConfig();

private slots:
    void toggleStreaming();
    void updateStats();

private:
    void applySessionEvent(const SessionEvent& ev);
    void setStatusText(const QString& text);

    Dock*                           dock_;
    MultiOutputConfig&              config_;
    TargetId                        targetId_;
    std::unique_ptr<OutputSession>  session_;

    QLabel*      nameLabel_   = nullptr;
    QLabel*      statusLabel_ = nullptr;
    QPushButton* btnStart_    = nullptr;
    QPushButton* btnEdit_     = nullptr;
    QPushButton* btnDelete_   = nullptr;
    QTimer*      statsTimer_  = nullptr;

    std::chrono::steady_clock::time_point startedAt_{};
};

} // namespace mrtmp
