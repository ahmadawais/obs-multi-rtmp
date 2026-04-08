// SPDX-License-Identifier: GPL-2.0-or-later
#include "Dock.h"

#include <algorithm>
#include <unordered_map>

#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>

#include "../Logging.h"
#include "EditDialog.h"
#include "TargetRow.h"

namespace mrtmp {

Dock::Dock(QWidget* parent) : QWidget(parent) {
    setWindowTitle(obs_module_text("Title"));

    // ---- Layout skeleton: scroll area → container → rows ----
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    outer->addWidget(scroll_);

    auto* container = new QWidget(scroll_);
    rootLayout_ = new QVBoxLayout(container);
    rootLayout_->setAlignment(Qt::AlignTop);
    scroll_->setWidget(container);

    // ---- Top buttons ----
    auto* addBtn = new QPushButton(obs_module_text("Btn.NewTarget"), container);
    connect(addBtn, &QPushButton::clicked, this, &Dock::addTarget);
    rootLayout_->addWidget(addBtn);

    auto* allRow = new QWidget(container);
    auto* allLayout = new QHBoxLayout(allRow);
    allLayout->setContentsMargins(0, 0, 0, 0);
    auto* startAllBtn = new QPushButton(obs_module_text("Btn.StartAll"), allRow);
    auto* stopAllBtn  = new QPushButton(obs_module_text("Btn.StopAll"),  allRow);
    allLayout->addWidget(startAllBtn);
    allLayout->addWidget(stopAllBtn);
    rootLayout_->addWidget(allRow);
    connect(startAllBtn, &QPushButton::clicked, this, &Dock::startAll);
    connect(stopAllBtn,  &QPushButton::clicked, this, &Dock::stopAll);

    // ---- Target list (reorderable via drag) ----
    list_ = new QListWidget(container);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDropIndicatorShown(true);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setStyleSheet(
        "QListWidget { border: none; background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"
        "QListWidget::item:hover { background: rgba(127,127,127,0.1); }"
    );
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this, &Dock::onRowsMoved);
    rootLayout_->addWidget(list_);

    reloadFromProfile();
}

Dock::~Dock() = default;

// ---- config load/save ------------------------------------------------------

void Dock::reloadFromProfile() {
    clearRows();
    config_ = {};
    if (!loadFromProfile(config_)) {
        MRTMP_INFO("Starting with empty config");
    }
    for (const auto& t : config_.targets) addRowForTarget(t.id);
}

void Dock::saveToProfile() {
    (void)mrtmp::saveToProfile(config_);
}

// ---- row management --------------------------------------------------------

TargetRow* Dock::addRowForTarget(const TargetId& id) {
    auto* row = new TargetRow(this, config_, id, list_->viewport());

    auto* item = new QListWidgetItem();
    item->setData(Qt::UserRole, QString::fromStdString(id));
    item->setSizeHint(row->sizeHint());
    list_->addItem(item);
    list_->setItemWidget(item, row);
    rows_.push_back(row);

    connect(row->deleteButton(), &QPushButton::clicked, this, [this, id]{
        auto r = QMessageBox::question(this,
                                       obs_module_text("Question.Title"),
                                       obs_module_text("Question.Delete"));
        if (r != QMessageBox::Yes) return;
        removeRowForTarget(id);
        std::erase_if(config_.targets, [&](const OutputTarget& t){ return t.id == id; });
        saveToProfile();
    });

    return row;
}

void Dock::removeRowForTarget(const TargetId& id) {
    // Remove from rows_ vector first so any later callbacks don't double-free.
    auto it = std::find_if(rows_.begin(), rows_.end(),
                           [&](TargetRow* r){ return r->targetId() == id; });
    if (it == rows_.end()) return;
    TargetRow* row = *it;
    rows_.erase(it);

    const QString qid = QString::fromStdString(id);
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        if (!item || item->data(Qt::UserRole).toString() != qid) continue;
        QListWidgetItem* taken = list_->takeItem(i);
        delete taken;
        break;
    }
    row->deleteLater();
}

void Dock::clearRows() {
    list_->clear();
    for (TargetRow* r : rows_) r->deleteLater();
    rows_.clear();
}

// ---- slots -----------------------------------------------------------------

void Dock::addTarget() {
    OutputTarget t;
    t.id = config_.generateId();
    t.name = "New target";
    config_.targets.push_back(t);

    EditDialog dlg(config_, config_.targets.back(), this);
    if (dlg.exec() == QDialog::Accepted) {
        addRowForTarget(config_.targets.back().id);
        saveToProfile();
    } else {
        config_.targets.pop_back();
    }
}

void Dock::startAll() { for (TargetRow* r : rows_) r->startStreaming(); }
void Dock::stopAll()  { for (TargetRow* r : rows_) r->stopStreaming();  }

void Dock::onRowsMoved() {
    // Reorder config_.targets to match the new row order. We keep targets not
    // represented in the list (defensive — should be none) at the tail.
    std::unordered_map<std::string, OutputTarget> byId;
    byId.reserve(config_.targets.size());
    for (auto& t : config_.targets) byId.emplace(t.id, std::move(t));
    std::vector<OutputTarget> reordered;
    reordered.reserve(byId.size());

    for (int i = 0; i < list_->count(); ++i) {
        const auto id = list_->item(i)->data(Qt::UserRole).toString().toStdString();
        auto it = byId.find(id);
        if (it == byId.end()) continue;
        reordered.push_back(std::move(it->second));
        byId.erase(it);
    }
    for (auto& [_, t] : byId) reordered.push_back(std::move(t));
    config_.targets = std::move(reordered);
    saveToProfile();
}

// ---- OBS event fan-out -----------------------------------------------------

void Dock::handleObsEvent(obs_frontend_event ev) {
    for (TargetRow* r : rows_) r->handleObsEvent(ev);
    if (ev == OBS_FRONTEND_EVENT_EXIT) saveToProfile();
    else if (ev == OBS_FRONTEND_EVENT_PROFILE_CHANGED) reloadFromProfile();
}

} // namespace mrtmp
