// SPDX-License-Identifier: GPL-2.0-or-later
#include "TargetRow.h"

#include <cmath>

#include <QApplication>
#include <QGridLayout>
#include <QMessageBox>

#include "../Logging.h"
#include "Dock.h"
#include "EditDialog.h"

namespace mrtmp {
namespace {

QString formatBitrate(double bps) {
    if (bps <= 0) return QStringLiteral("0 bps");
    static constexpr const char* kUnits[] = {"bps","Kbps","Mbps","Gbps","Tbps"};
    int idx = std::min<int>(std::size(kUnits) - 1, (int)(std::log10(bps) / 3));
    const double v = bps / std::pow(1000.0, idx);
    return QString::asprintf("%.1f %s", v, kUnits[idx]);
}

QString formatDuration(std::chrono::steady_clock::duration d) {
    using namespace std::chrono;
    auto total = duration_cast<seconds>(d).count();
    const auto hh = total / 3600;
    const auto mm = (total / 60) % 60;
    const auto ss = total % 60;
    return QString::asprintf("%02lld:%02lld:%02lld",
                             (long long)hh, (long long)mm, (long long)ss);
}

} // namespace

TargetRow::TargetRow(Dock* dock, MultiOutputConfig& config, TargetId id, QWidget* parent)
    : QWidget(parent)
    , dock_(dock)
    , config_(config)
    , targetId_(std::move(id))
{
    // ---- visual layout ----
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(8, 6, 8, 6);

    nameLabel_   = new QLabel(QStringLiteral("—"), this);
    statusLabel_ = new QLabel(QString{},           this);
    statusLabel_->setWordWrap(true);

    btnStart_  = new QPushButton(obs_module_text("Btn.Start"),  this);
    btnEdit_   = new QPushButton(obs_module_text("Btn.Edit"),   this);
    btnDelete_ = new QPushButton(obs_module_text("Btn.Delete"), this);

    grid->addWidget(nameLabel_,   0, 0, 1, 3);
    grid->addWidget(btnStart_,    1, 0);
    grid->addWidget(btnEdit_,     1, 1);
    grid->addWidget(btnDelete_,   1, 2);
    grid->addWidget(statusLabel_, 2, 0, 1, 3);

    // ---- state ----
    // OutputSession's UiPoster posts tasks back to the main UI thread via Qt.
    // Using invokeMethod on `this` with Qt::QueuedConnection guarantees thread
    // affinity without us needing to track a QThread pointer manually.
    auto poster = [this](std::function<void()> task) {
        QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
    };
    session_ = std::make_unique<OutputSession>(config_, targetId_, std::move(poster));
    session_->setListener([this](const SessionEvent& ev){ applySessionEvent(ev); });

    statsTimer_ = new QTimer(this);
    statsTimer_->setInterval(1000);
    connect(statsTimer_, &QTimer::timeout, this, &TargetRow::updateStats);

    connect(btnStart_, &QPushButton::clicked, this, &TargetRow::toggleStreaming);
    connect(btnEdit_,  &QPushButton::clicked, this, [this]{ editInDialog(); });
    // Delete button wiring is owned by the Dock (it needs a confirmation
    // dialog rooted at the dock, and it has to mutate config_.targets).

    refreshFromConfig();
}

TargetRow::~TargetRow() {
    // session_ dtor cleans up OBS output. Explicit ordering: make sure the
    // timer fires no more signals first.
    if (statsTimer_) statsTimer_->stop();
    session_.reset();
}

void TargetRow::refreshFromConfig() {
    if (const OutputTarget* t = config_.findTarget(targetId_)) {
        nameLabel_->setText(t->name.empty()
                                ? QString::fromUtf8(obs_module_text("NewStreaming"))
                                : QString::fromUtf8(t->name));
    }
}

void TargetRow::handleObsEvent(obs_frontend_event ev) {
    switch (ev) {
    case OBS_FRONTEND_EVENT_EXIT:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED:
        if (session_ && session_->isActive()) session_->stop(true);
        break;
    case OBS_FRONTEND_EVENT_STREAMING_STARTING:
        if (const OutputTarget* t = config_.findTarget(targetId_); t && t->syncStart) {
            if (!session_->isActive()) startStreaming();
        }
        break;
    case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
        if (const OutputTarget* t = config_.findTarget(targetId_); t && t->syncStop) {
            if (session_->isActive()) stopStreaming();
        }
        break;
    default: break;
    }
}

void TargetRow::toggleStreaming() {
    if (session_->isActive()) stopStreaming();
    else                      startStreaming();
}

void TargetRow::startStreaming() {
    startedAt_ = std::chrono::steady_clock::now();
    if (!session_->start()) return;      // listener will emit Stopped
    btnDelete_->setEnabled(false);
    btnStart_->setText(obs_module_text("Status.Stop"));
    setStatusText(obs_module_text("Status.Connecting"));
    statsTimer_->start();
}

void TargetRow::stopStreaming() {
    bool force = false;
    if (session_->usingDelay()) {
        auto r = QMessageBox::question(this,
                                       QStringLiteral("?"),
                                       obs_module_text("Ques.DropDelay"),
                                       QMessageBox::Yes | QMessageBox::No);
        force = (r == QMessageBox::Yes);
    }
    session_->stop(force);
}

bool TargetRow::editInDialog() {
    OutputTarget* t = config_.findTarget(targetId_);
    if (!t) return false;
    EditDialog dlg(config_, *t, this);
    if (dlg.exec() != QDialog::Accepted) return false;
    refreshFromConfig();
    if (dock_) dock_->saveToProfile();
    return true;
}

void TargetRow::updateStats() {
    if (!session_ || !session_->isActive()) return;
    const auto stats = session_->sampleStats();
    const auto duration = std::chrono::steady_clock::now() - startedAt_;
    const QString text = QStringLiteral("%1   %2   %3 fps")
                             .arg(formatDuration(duration))
                             .arg(formatBitrate(stats.bitsPerSecond))
                             .arg(QString::number(stats.framesPerSecond, 'f', 0));
    setStatusText(text);
}

void TargetRow::applySessionEvent(const SessionEvent& ev) {
    switch (ev.state) {
    case SessionState::Starting:
        setStatusText(obs_module_text("Status.Connecting"));
        break;
    case SessionState::Running:
        setStatusText(obs_module_text("Status.Streaming"));
        break;
    case SessionState::Reconnecting:
        statsTimer_->stop();
        setStatusText(obs_module_text("Status.Reconnecting"));
        break;
    case SessionState::Stopping:
        statsTimer_->stop();
        setStatusText(obs_module_text("Status.Stopping"));
        break;
    case SessionState::Stopped:
        statsTimer_->stop();
        btnStart_->setText(obs_module_text("Btn.Start"));
        btnDelete_->setEnabled(true);
        if (ev.stopCode == 0 || ev.message.empty()) setStatusText(QString{});
        else setStatusText(QString::fromUtf8(ev.message));
        break;
    default: break;
    }
}

void TargetRow::setStatusText(const QString& text) {
    statusLabel_->setText(text);
    statusLabel_->setToolTip(text);
}

} // namespace mrtmp
