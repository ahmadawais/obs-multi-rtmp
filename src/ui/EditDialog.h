// SPDX-License-Identifier: GPL-2.0-or-later
//
// EditDialog — add/edit a single OutputTarget. Modal.
//
// Replaces the old edit-widget.cpp (1163 LOC). Kept intentionally small by:
//   * delegating dynamic form generation to PropertiesView
//   * not re-implementing OBS's encoder properties UI manually
//   * sensible defaults (share main encoder) when user doesn't care
#pragma once

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>

#include "../Config.h"
#include "../ObsPtr.h"

namespace mrtmp {

class PropertiesView;

class EditDialog : public QDialog {
    Q_OBJECT
public:
    // Edits `target` in place; caller reads it back on accept().
    EditDialog(MultiOutputConfig& config, OutputTarget& target, QWidget* parent = nullptr);
    ~EditDialog() override;

private slots:
    void onProtocolChanged(int index);
    void onAccept();

private:
    void rebuildServiceProperties();

    MultiOutputConfig& config_;
    OutputTarget&      target_;

    QLineEdit*       nameEdit_         = nullptr;
    QComboBox*       protocolCombo_    = nullptr;
    QCheckBox*       syncStartCheck_   = nullptr;
    QCheckBox*       syncStopCheck_    = nullptr;

    // Service properties (dynamic per protocol).
    UniqueData       serviceSettings_;
    PropertiesView*  serviceView_      = nullptr;
    QWidget*         serviceContainer_ = nullptr;

    // Encoder config pickers (simple: inherit main, or custom encoder by id).
    QComboBox*       videoConfigCombo_ = nullptr;
    QComboBox*       audioConfigCombo_ = nullptr;
};

} // namespace mrtmp
