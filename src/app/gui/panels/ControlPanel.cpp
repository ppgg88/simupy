#include "ControlPanel.h"

#include "blocks/ControlBlocks.h"
#include "blocks/SubsystemBlock.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace simupy {
namespace {

constexpr int kSliderTicks = 1000;

int toTicks(double value, const ControlDescriptor& d) {
    const double span = d.maximum - d.minimum;
    if (span <= 0.0) return 0;
    return int(std::lround((value - d.minimum) / span * kSliderTicks));
}

double fromTicks(int ticks, const ControlDescriptor& d) {
    const double span = d.maximum - d.minimum;
    double value = d.minimum + span * ticks / double(kSliderTicks);
    if (d.step > 0.0)
        value = d.minimum + std::round((value - d.minimum) / d.step) * d.step;
    return std::clamp(value, d.minimum, d.maximum);
}

}

ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent) {
    content_ = new QWidget;
    form_ = new QVBoxLayout(content_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setSpacing(10);

    scroll_ = new QScrollArea(this);
    scroll_->setWidget(content_);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);

    empty_ = new QLabel(
        tr("No controls in this model.\n\n"
           "Add a <b>Controls → Slider</b>, <b>Toggle</b> or <b>PushButton</b> "
           "block and it appears here — ready to drive while the model runs."),
        this);
    empty_->setWordWrap(true);
    empty_->setTextFormat(Qt::RichText);
    empty_->setAlignment(Qt::AlignTop);
    empty_->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(empty_);
    layout->addWidget(scroll_, 1);

    setModel(nullptr);
}

void ControlPanel::clearWidgets() {
    QLayoutItem* item = nullptr;
    while ((item = form_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    refreshers_.clear();
}

void ControlPanel::collect(Model& model, const QString& prefix) {
    for (const BlockPtr& block : model.blocks()) {
        if (auto* subsystem = dynamic_cast<SubsystemBlock*>(block.get())) {
            collect(subsystem->contents(),
                    prefix + QString::fromStdString(block->name()) +
                        QLatin1Char('/'));
            continue;
        }
        if (auto* interactive = dynamic_cast<InteractiveBlock*>(block.get()))
            controls_.append(
                {interactive,
                 prefix + QString::fromStdString(block->name())});
    }
}

void ControlPanel::setModel(Model* root) {
    root_ = root;
    controls_.clear();
    clearWidgets();

    if (root_) collect(*root_, QString());

    empty_->setVisible(controls_.isEmpty());
    scroll_->setVisible(!controls_.isEmpty());
    if (controls_.isEmpty()) return;

    for (const Entry& entry : controls_) {
        QWidget* widget = buildWidget(entry);
        if (!widget) continue;

        auto* group = new QWidget;
        auto* column = new QVBoxLayout(group);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);

        auto* name = new QLabel(entry.path);
        QFont font = name->font();
        font.setPointSize(std::max(7, font.pointSize() - 1));
        name->setFont(font);
        name->setEnabled(false);

        column->addWidget(name);
        column->addWidget(widget);
        form_->addWidget(group);
    }
    form_->addStretch(1);
}

void ControlPanel::refreshValues() {
    for (const auto& refresh : refreshers_) refresh();
}

bool ControlPanel::commitValues() {
    bool changed = false;
    for (const Entry& entry : controls_) {
        const double live = entry.block->liveValue();
        if (entry.block->initialValue() != live) changed = true;
        entry.block->commitValue(live);
    }
    return changed;
}

QWidget* ControlPanel::buildWidget(const Entry& entry) {
    InteractiveBlock* block = entry.block;
    const ControlDescriptor descriptor = block->descriptor();

    block->reseedFromParams();

    switch (descriptor.kind) {
        case ControlDescriptor::Kind::Slider: {
            auto* container = new QWidget;
            auto* row = new QHBoxLayout(container);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);

            auto* slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, kSliderTicks);
            slider->setValue(toTicks(block->liveValue(), descriptor));

            auto* readout = new QDoubleSpinBox;
            readout->setRange(descriptor.minimum, descriptor.maximum);
            readout->setDecimals(4);
            readout->setSingleStep(descriptor.step > 0.0
                                       ? descriptor.step
                                       : (descriptor.maximum -
                                          descriptor.minimum) / 100.0);
            readout->setValue(block->liveValue());
            readout->setMaximumWidth(90);
            readout->setKeyboardTracking(false);

            row->addWidget(slider, 1);
            row->addWidget(readout);

            connect(slider, &QSlider::valueChanged, this,
                    [this, block, descriptor, readout](int ticks) {
                        const double value = fromTicks(ticks, descriptor);
                        block->setLiveValue(value);
                        QSignalBlocker blocker(readout);
                        readout->setValue(value);
                        emit controlChanged();
                    });
            connect(readout, &QDoubleSpinBox::valueChanged, this,
                    [this, block, descriptor, slider](double value) {
                        block->setLiveValue(value);
                        QSignalBlocker blocker(slider);
                        slider->setValue(toTicks(value, descriptor));
                        emit controlChanged();
                    });

            refreshers_.append([block, descriptor, slider, readout] {
                const double value = block->liveValue();
                QSignalBlocker a(slider);
                QSignalBlocker b(readout);
                slider->setValue(toTicks(value, descriptor));
                readout->setValue(value);
            });
            if (!descriptor.label.empty())
                container->setToolTip(QString::fromStdString(descriptor.label));
            return container;
        }

        case ControlDescriptor::Kind::Toggle: {
            auto* box = new QCheckBox(
                descriptor.label.empty()
                    ? tr("on")
                    : QString::fromStdString(descriptor.label));
            box->setChecked(block->liveValue() == descriptor.onValue);
            box->setToolTip(tr("Off = %1, On = %2")
                                .arg(descriptor.offValue)
                                .arg(descriptor.onValue));

            connect(box, &QCheckBox::toggled, this,
                    [this, block, descriptor](bool on) {
                        block->setLiveValue(on ? descriptor.onValue
                                               : descriptor.offValue);
                        emit controlChanged();
                    });

            refreshers_.append([block, descriptor, box] {
                QSignalBlocker blocker(box);
                box->setChecked(block->liveValue() == descriptor.onValue);
            });
            return box;
        }

        case ControlDescriptor::Kind::Button: {
            auto* button = new QPushButton(
                descriptor.label.empty()
                    ? tr("Press and hold")
                    : QString::fromStdString(descriptor.label));
            button->setToolTip(tr("Outputs %1 while held, %2 otherwise")
                                   .arg(descriptor.onValue)
                                   .arg(descriptor.offValue));

            connect(button, &QPushButton::pressed, this,
                    [this, block, descriptor] {
                        block->setLiveValue(descriptor.onValue);
                        emit controlChanged();
                    });
            connect(button, &QPushButton::released, this,
                    [this, block, descriptor] {
                        block->setLiveValue(descriptor.offValue);
                        emit controlChanged();
                    });

            refreshers_.append([] {});
            return button;
        }
    }
    return nullptr;
}

}
