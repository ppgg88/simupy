#include "PropertyPanel.h"

#include "app/gui/NumberInput.h"
#include "app/gui/style/Theme.h"
#include "model/BlockRegistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace simupy {
namespace {

QString vectorToText(const std::vector<double>& values) {
    QStringList parts;
    for (double value : values) parts << numbers::format(value);
    return parts.join(QStringLiteral(", "));
}

std::vector<double> textToVector(const QString& text, bool* ok) {
    std::vector<double> values;
    if (ok) *ok = true;

    const QStringList parts =
        text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                   Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool parsed = false;
        const double value = part.toDouble(&parsed);
        if (!parsed) {
            if (ok) *ok = false;
            return {};
        }
        values.push_back(value);
    }
    return values;
}

double realDefault(const ParamSpec& spec) {
    if (auto* d = std::get_if<double>(&spec.defaultValue)) return *d;
    if (auto* b = std::get_if<bool>(&spec.defaultValue)) return *b ? 1.0 : 0.0;
    return 0.0;
}

bool booleanDefault(const ParamSpec& spec) {
    if (auto* b = std::get_if<bool>(&spec.defaultValue)) return *b;
    if (auto* d = std::get_if<double>(&spec.defaultValue)) return *d != 0.0;
    return false;
}

std::string textDefault(const ParamSpec& spec) {
    if (auto* s = std::get_if<std::string>(&spec.defaultValue)) return *s;
    return {};
}

std::vector<double> vectorDefault(const ParamSpec& spec) {
    if (auto* v = std::get_if<std::vector<double>>(&spec.defaultValue)) return *v;
    if (auto* d = std::get_if<double>(&spec.defaultValue)) return {*d};
    return {};
}

}

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
    header_ = new QLabel(this);
    QFont headerFont = header_->font();
    headerFont.setBold(true);
    headerFont.setPointSize(headerFont.pointSize() + 1);
    header_->setFont(headerFont);

    description_ = new QLabel(this);
    description_->setWordWrap(true);
    description_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

    content_ = new QWidget;
    form_ = new QFormLayout(content_);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->setSpacing(8);

    scroll_ = new QScrollArea(this);
    scroll_->setWidget(content_);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(header_);
    layout->addWidget(description_);
    layout->addWidget(scroll_, 1);

    setBlock(nullptr, nullptr);
}

void PropertyPanel::setBlock(Model* model, Block* block) {
    model_ = model;
    block_ = block;
    rebuild();
}

void PropertyPanel::refresh() { rebuild(); }

void PropertyPanel::setMaskParameters(const QStringList& names) {
    if (maskParameters_ == names) return;
    maskParameters_ = names;
    rebuild();
}

QWidget* PropertyPanel::withExpressionToggle(const ParamSpec& spec,
                                             QWidget* editor) {
    if (maskParameters_.isEmpty() || !block_) return editor;
    if (spec.kind == ParamSpec::Kind::PythonCode) return editor;

    const std::string name = spec.name;
    const QString expression =
        QString::fromStdString(block_->paramExpression(name));

    auto* container = new QWidget;
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    auto* toggle = new QToolButton;
    toggle->setText(QStringLiteral("ƒx"));
    toggle->setCheckable(true);
    toggle->setChecked(!expression.isEmpty());
    toggle->setToolTip(tr("Compute this parameter from the enclosing block's "
                          "parameters: %1")
                           .arg(maskParameters_.join(QStringLiteral(", "))));

    auto* expressionEdit = new QLineEdit(expression);
    expressionEdit->setPlaceholderText(maskParameters_.first());
    QFont mono = expressionEdit->font();
    mono.setFamily(QStringLiteral("monospace"));
    expressionEdit->setFont(mono);

    row->addWidget(editor, 1);
    row->addWidget(expressionEdit, 1);
    row->addWidget(toggle);

    auto sync = [editor, expressionEdit](bool bound) {
        // One or the other: a typed value overwritten at the next run is a trap.
        editor->setVisible(!bound);
        expressionEdit->setVisible(bound);
    };
    sync(!expression.isEmpty());

    connect(toggle, &QToolButton::toggled, this,
            [this, name, sync, expressionEdit](bool bound) {
                sync(bound);
                if (!bound && block_) {
                    block_->setParamExpression(name, {});
                    expressionEdit->clear();
                    emit blockModified(QString::fromStdString(block_->id()));
                }
            });

    connect(expressionEdit, &QLineEdit::editingFinished, this,
            [this, name, expressionEdit] {
                if (!block_) return;
                block_->setParamExpression(name,
                                           expressionEdit->text().toStdString());
                emit blockModified(QString::fromStdString(block_->id()));
            });

    return container;
}

void PropertyPanel::commit(const std::string& name, ParamValue value) {
    if (!block_) return;
    block_->params().set(name, std::move(value));
    emit blockModified(QString::fromStdString(block_->id()));
}

void PropertyPanel::rebuild() {
    while (form_->rowCount() > 0) form_->removeRow(0);

    if (!block_) {
        header_->setText(tr("No selection"));
        description_->setText(
            tr("Select a block on the canvas to edit its parameters."));
        return;
    }

    const BlockType* type = BlockRegistry::instance().find(block_->typeName());
    header_->setText(QString::fromStdString(block_->name()));
    description_->setText(
        type ? QString::fromStdString(type->description) : QString());

    auto* nameEdit = new QLineEdit(QString::fromStdString(block_->name()));
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit] {
        if (!block_ || !model_) return;
        const std::string wanted = nameEdit->text().trimmed().toStdString();
        if (wanted.empty() || wanted == block_->name()) {
            nameEdit->setText(QString::fromStdString(block_->name()));
            return;
        }
        const std::string unique = model_->uniqueName(wanted, block_->id());
        block_->setName(unique);
        nameEdit->setText(QString::fromStdString(unique));
        header_->setText(QString::fromStdString(unique));
        emit blockModified(QString::fromStdString(block_->id()));
    });
    form_->addRow(tr("Name"), nameEdit);

    auto* typeLabel = new QLabel(QString::fromStdString(block_->typeName()));
    typeLabel->setEnabled(false);
    form_->addRow(tr("Type"), typeLabel);

    if (!type || type->params.empty()) {
        if (type && type->params.empty())
            form_->addRow(new QLabel(tr("This block has no parameters.")));
        return;
    }

    for (const ParamSpec& spec : type->params) {
        const std::string name = spec.name;
        const QString label = QString::fromStdString(spec.label);
        QWidget* editor = nullptr;

        switch (spec.kind) {
            case ParamSpec::Kind::Boolean: {
                auto* box = new QCheckBox;
                box->setChecked(block_->params().booleanOr(name, booleanDefault(spec)));
                connect(box, &QCheckBox::toggled, this,
                        [this, name](bool on) { commit(name, on); });
                editor = box;
                break;
            }

            case ParamSpec::Kind::Integer: {
                auto* spin = new QSpinBox;
                spin->setRange(static_cast<int>(spec.minimum),
                               static_cast<int>(std::min(spec.maximum, 1e9)));
                spin->setValue(block_->params().integerOr(name, int(realDefault(spec))));
                connect(spin, &QSpinBox::valueChanged, this,
                        [this, name](int value) {
                            commit(name, static_cast<double>(value));
                        });
                editor = spin;
                break;
            }

            case ParamSpec::Kind::Choice: {
                auto* combo = new QComboBox;
                for (const std::string& choice : spec.choices)
                    combo->addItem(QString::fromStdString(choice));
                const int index =
                    combo->findText(QString::fromStdString(
                        block_->params().textOr(name, textDefault(spec))));
                combo->setCurrentIndex(index >= 0 ? index : 0);
                connect(combo, &QComboBox::currentTextChanged, this,
                        [this, name](const QString& text) {
                            commit(name, text.toStdString());
                        });
                editor = combo;
                break;
            }

            case ParamSpec::Kind::Text: {
                auto* line = new QLineEdit(
                    QString::fromStdString(block_->params().textOr(name, textDefault(spec))));
                connect(line, &QLineEdit::editingFinished, this,
                        [this, name, line] {
                            commit(name, line->text().toStdString());
                        });
                editor = line;
                break;
            }

            case ParamSpec::Kind::Vector: {
                auto* line = new QLineEdit(
                    vectorToText(block_->params().vectorOr(name, vectorDefault(spec))));
                line->setPlaceholderText(tr("e.g. 1, 2.5, 3"));
                const std::vector<double> fallback = vectorDefault(spec);
                connect(line, &QLineEdit::editingFinished, this,
                        [this, name, line, fallback] {
                            bool ok = false;
                            std::vector<double> values =
                                textToVector(line->text(), &ok);
                            if (!ok) {
                                line->setText(vectorToText(
                                    block_->params().vectorOr(name, fallback)));
                                return;
                            }
                            commit(name, std::move(values));
                        });
                editor = line;
                break;
            }

            case ParamSpec::Kind::PythonCode: {
                auto* container = new QWidget;
                auto* row = new QHBoxLayout(container);
                row->setContentsMargins(0, 0, 0, 0);
                row->setSpacing(6);

                const QString source =
                    QString::fromStdString(block_->params().textOr(name, textDefault(spec)));
                auto* summary = new QLabel(
                    tr("%n line(s)", "", source.count(QLatin1Char('\n')) + 1));
                summary->setEnabled(false);

                auto* button = new QPushButton(tr("Edit…"));
                const std::string blockId = block_->id();
                connect(button, &QPushButton::clicked, this,
                        [this, blockId, name] {
                            emit codeEditRequested(
                                QString::fromStdString(blockId),
                                QString::fromStdString(name));
                        });

                row->addWidget(summary, 1);
                row->addWidget(button);
                editor = container;
                break;
            }

            case ParamSpec::Kind::Real:
            default: {
                auto* line = new QLineEdit(numbers::format(
                    block_->params().realOr(name, realDefault(spec))));
                numbers::constrain(line, spec.minimum, spec.maximum);
                const double fallback = realDefault(spec);
                connect(line, &QLineEdit::editingFinished, this,
                        [this, name, line, fallback] {
                            bool ok = false;
                            const double value = numbers::parse(line->text(),
                                                                &ok);
                            if (!ok) {
                                line->setText(numbers::format(
                                    block_->params().realOr(name, fallback)));
                                return;
                            }
                            // Redrawn: a typed comma comes back as a point.
                            line->setText(numbers::format(value));
                            commit(name, value);
                        });
                editor = line;
                break;
            }
        }

        if (!editor) continue;
        if (!spec.tooltip.empty())
            editor->setToolTip(QString::fromStdString(spec.tooltip));
        form_->addRow(label, withExpressionToggle(spec, editor));
    }
}

}
