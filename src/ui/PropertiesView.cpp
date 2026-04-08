// SPDX-License-Identifier: GPL-2.0-or-later
#include "PropertiesView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mrtmp {

PropertiesView::PropertiesView(UniqueProperties properties, obs_data_t* settings, QWidget* parent)
    : QWidget(parent), properties_(std::move(properties)), settings_(settings)
{
    buildForm();
}

PropertiesView::~PropertiesView() = default;

void PropertiesView::flush() {
    for (auto& f : flushers_) f();
}

void PropertiesView::buildForm() {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    for (obs_property_t* p = obs_properties_first(properties_.get()); p; obs_property_next(&p)) {
        if (!obs_property_visible(p)) continue;
        addProperty(p, form);
    }
}

void PropertiesView::addProperty(obs_property_t* prop, QFormLayout* form) {
    const char*    name  = obs_property_name(prop);
    const char*    label = obs_property_description(prop);
    const auto     type  = obs_property_get_type(prop);

    const QString labelText = QString::fromUtf8(label ? label : (name ? name : ""));

    switch (type) {
    case OBS_PROPERTY_BOOL: {
        auto* cb = new QCheckBox(this);
        cb->setChecked(obs_data_get_bool(settings_, name));
        form->addRow(labelText, cb);
        flushers_.emplace_back([this, cb, name]{
            obs_data_set_bool(settings_, name, cb->isChecked());
        });
        QObject::connect(cb, &QCheckBox::toggled, this, [this, name](bool v){
            obs_data_set_bool(settings_, name, v);
        });
        break;
    }
    case OBS_PROPERTY_INT: {
        auto* sp = new QSpinBox(this);
        sp->setRange((int)obs_property_int_min(prop), (int)obs_property_int_max(prop));
        sp->setSingleStep((int)obs_property_int_step(prop));
        sp->setValue((int)obs_data_get_int(settings_, name));
        form->addRow(labelText, sp);
        flushers_.emplace_back([this, sp, name]{
            obs_data_set_int(settings_, name, sp->value());
        });
        QObject::connect(sp, QOverload<int>::of(&QSpinBox::valueChanged),
                         this, [this, name](int v){ obs_data_set_int(settings_, name, v); });
        break;
    }
    case OBS_PROPERTY_FLOAT: {
        auto* sp = new QDoubleSpinBox(this);
        sp->setRange(obs_property_float_min(prop), obs_property_float_max(prop));
        sp->setSingleStep(obs_property_float_step(prop));
        sp->setValue(obs_data_get_double(settings_, name));
        form->addRow(labelText, sp);
        flushers_.emplace_back([this, sp, name]{
            obs_data_set_double(settings_, name, sp->value());
        });
        QObject::connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this, [this, name](double v){ obs_data_set_double(settings_, name, v); });
        break;
    }
    case OBS_PROPERTY_TEXT: {
        auto* le = new QLineEdit(this);
        le->setText(QString::fromUtf8(obs_data_get_string(settings_, name)));
        if (obs_property_text_type(prop) == OBS_TEXT_PASSWORD)
            le->setEchoMode(QLineEdit::Password);
        form->addRow(labelText, le);
        flushers_.emplace_back([this, le, name]{
            obs_data_set_string(settings_, name, le->text().toUtf8().constData());
        });
        QObject::connect(le, &QLineEdit::textChanged, this, [this, name](const QString& t){
            obs_data_set_string(settings_, name, t.toUtf8().constData());
        });
        break;
    }
    case OBS_PROPERTY_LIST: {
        auto* cb = new QComboBox(this);
        const auto listType = obs_property_list_type(prop);
        const auto fmt      = obs_property_list_format(prop);
        const size_t count  = obs_property_list_item_count(prop);
        int currentIdx = -1;

        for (size_t i = 0; i < count; ++i) {
            QString itemLabel = QString::fromUtf8(obs_property_list_item_name(prop, i));
            QVariant data;
            switch (fmt) {
            case OBS_COMBO_FORMAT_STRING:
                data = QString::fromUtf8(obs_property_list_item_string(prop, i)); break;
            case OBS_COMBO_FORMAT_INT:
                data = (qlonglong)obs_property_list_item_int(prop, i); break;
            case OBS_COMBO_FORMAT_FLOAT:
                data = obs_property_list_item_float(prop, i); break;
            default: break;
            }
            cb->addItem(itemLabel, data);
        }

        switch (fmt) {
        case OBS_COMBO_FORMAT_STRING: {
            QString cur = QString::fromUtf8(obs_data_get_string(settings_, name));
            currentIdx = cb->findData(cur);
            break;
        }
        case OBS_COMBO_FORMAT_INT: {
            currentIdx = cb->findData((qlonglong)obs_data_get_int(settings_, name));
            break;
        }
        case OBS_COMBO_FORMAT_FLOAT: {
            currentIdx = cb->findData(obs_data_get_double(settings_, name));
            break;
        }
        default: break;
        }
        if (currentIdx >= 0) cb->setCurrentIndex(currentIdx);
        if (listType == OBS_COMBO_TYPE_EDITABLE) cb->setEditable(true);

        form->addRow(labelText, cb);
        flushers_.emplace_back([this, cb, name, fmt]{
            const QVariant v = cb->currentData();
            switch (fmt) {
            case OBS_COMBO_FORMAT_STRING:
                obs_data_set_string(settings_, name, v.toString().toUtf8().constData()); break;
            case OBS_COMBO_FORMAT_INT:
                obs_data_set_int(settings_, name, v.toLongLong()); break;
            case OBS_COMBO_FORMAT_FLOAT:
                obs_data_set_double(settings_, name, v.toDouble()); break;
            default: break;
            }
        });
        break;
    }
    default:
        // Unsupported property type — render a read-only label so the user
        // at least knows something exists here, rather than silently dropping it.
        form->addRow(labelText, new QLabel(QStringLiteral("(unsupported)"), this));
        break;
    }
}

} // namespace mrtmp
