// SPDX-License-Identifier: GPL-2.0-or-later
#include "EditDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "../Logging.h"
#include "../Protocols.h"
#include "PropertiesView.h"

namespace mrtmp {

EditDialog::EditDialog(MultiOutputConfig& config, OutputTarget& target, QWidget* parent)
    : QDialog(parent), config_(config), target_(target)
{
    setWindowTitle(obs_module_text("EditDialog.Title"));
    setModal(true);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);

    // --- basics ---
    auto* basicsBox = new QGroupBox(obs_module_text("EditDialog.Basics"), this);
    auto* basicsForm = new QFormLayout(basicsBox);

    nameEdit_ = new QLineEdit(QString::fromUtf8(target_.name), basicsBox);
    basicsForm->addRow(obs_module_text("EditDialog.Name"), nameEdit_);

    protocolCombo_ = new QComboBox(basicsBox);
    int currentIdx = 0;
    int i = 0;
    for (const auto& p : allProtocols()) {
        protocolCombo_->addItem(QString::fromUtf8(std::string(p.label).c_str()),
                                QString::fromUtf8(std::string(p.key).c_str()));
        if (std::string_view{target_.protocol} == p.key) currentIdx = i;
        ++i;
    }
    protocolCombo_->setCurrentIndex(currentIdx);
    basicsForm->addRow(obs_module_text("EditDialog.Protocol"), protocolCombo_);

    syncStartCheck_ = new QCheckBox(obs_module_text("EditDialog.SyncStart"), basicsBox);
    syncStartCheck_->setChecked(target_.syncStart);
    basicsForm->addRow(QString{}, syncStartCheck_);

    syncStopCheck_ = new QCheckBox(obs_module_text("EditDialog.SyncStop"), basicsBox);
    syncStopCheck_->setChecked(target_.syncStop);
    basicsForm->addRow(QString{}, syncStopCheck_);

    root->addWidget(basicsBox);

    // --- service properties (dynamic per protocol) ---
    auto* serviceBox = new QGroupBox(obs_module_text("EditDialog.Service"), this);
    auto* serviceLayout = new QVBoxLayout(serviceBox);
    serviceContainer_ = new QWidget(serviceBox);
    serviceLayout->addWidget(serviceContainer_);
    root->addWidget(serviceBox);
    rebuildServiceProperties();

    // --- encoder selection (simple v1: inherit-main / named config) ---
    auto* encBox = new QGroupBox(obs_module_text("EditDialog.Encoders"), this);
    auto* encForm = new QFormLayout(encBox);

    videoConfigCombo_ = new QComboBox(encBox);
    videoConfigCombo_->addItem(obs_module_text("EditDialog.InheritMain"), QString{});
    for (const auto& v : config_.videoConfigs) {
        videoConfigCombo_->addItem(QString::fromUtf8(v.id),
                                   QString::fromUtf8(v.id));
    }
    if (target_.videoConfig) {
        int idx = videoConfigCombo_->findData(QString::fromUtf8(*target_.videoConfig));
        if (idx >= 0) videoConfigCombo_->setCurrentIndex(idx);
    }
    encForm->addRow(obs_module_text("EditDialog.VideoEncoder"), videoConfigCombo_);

    audioConfigCombo_ = new QComboBox(encBox);
    audioConfigCombo_->addItem(obs_module_text("EditDialog.InheritMain"), QString{});
    for (const auto& a : config_.audioConfigs) {
        audioConfigCombo_->addItem(QString::fromUtf8(a.id),
                                   QString::fromUtf8(a.id));
    }
    if (target_.audioConfig) {
        int idx = audioConfigCombo_->findData(QString::fromUtf8(*target_.audioConfig));
        if (idx >= 0) audioConfigCombo_->setCurrentIndex(idx);
    }
    encForm->addRow(obs_module_text("EditDialog.AudioEncoder"), audioConfigCombo_);

    root->addWidget(encBox);

    // --- buttons ---
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &EditDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EditDialog::reject);

    connect(protocolCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditDialog::onProtocolChanged);
}

EditDialog::~EditDialog() = default;

void EditDialog::onProtocolChanged(int /*index*/) {
    // User changed protocol — throw away previous service params and re-seed
    // with defaults from the new service type.
    target_.serviceParam = Json::object();
    rebuildServiceProperties();
}

void EditDialog::rebuildServiceProperties() {
    const QString key = protocolCombo_->currentData().toString();
    const Protocol* proto = findProtocol(key.toStdString());
    if (!proto) proto = &defaultProtocol();

    // Reseed settings from the config's stored params, fall back to defaults
    // provided by the service type.
    serviceSettings_ = ObsDataFromJson(target_.serviceParam.dump().c_str());

    UniqueProperties props{obs_get_service_properties(std::string(proto->serviceId).c_str())};
    if (!props) {
        MRTMP_WARN("obs_get_service_properties(%.*s) returned null",
                   (int)proto->serviceId.size(), proto->serviceId.data());
        return;
    }

    // Swap in a fresh PropertiesView.
    auto* layout = qobject_cast<QVBoxLayout*>(serviceContainer_->layout());
    if (!layout) {
        layout = new QVBoxLayout(serviceContainer_);
        layout->setContentsMargins(0, 0, 0, 0);
    }
    if (serviceView_) {
        layout->removeWidget(serviceView_);
        serviceView_->deleteLater();
    }
    serviceView_ = new PropertiesView(std::move(props), serviceSettings_.get(), serviceContainer_);
    layout->addWidget(serviceView_);
}

void EditDialog::onAccept() {
    if (serviceView_) serviceView_->flush();

    target_.name      = nameEdit_->text().toUtf8().constData();
    target_.protocol  = protocolCombo_->currentData().toString().toUtf8().constData();
    target_.syncStart = syncStartCheck_->isChecked();
    target_.syncStop  = syncStopCheck_->isChecked();

    // Convert the mutated obs_data_t back into JSON.
    const char* asJson = obs_data_get_json(serviceSettings_.get());
    target_.serviceParam = asJson ? Json::parse(asJson, nullptr, false) : Json::object();
    if (target_.serviceParam.is_discarded()) target_.serviceParam = Json::object();

    auto toOpt = [](QComboBox* cb) -> std::optional<std::string> {
        QString v = cb->currentData().toString();
        if (v.isEmpty()) return std::nullopt;
        return v.toStdString();
    };
    target_.videoConfig = toOpt(videoConfigCombo_);
    target_.audioConfig = toOpt(audioConfigCombo_);

    accept();
}

} // namespace mrtmp
