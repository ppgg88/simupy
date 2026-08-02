#include "CustomBlockDialog.h"
#include "app/gui/NumberInput.h"

#include "app/gui/style/BlockIconRenderer.h"
#include "app/gui/style/Theme.h"
#include "model/BlockRegistry.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr int kColumnName = 0;
constexpr int kColumnLabel = 1;
constexpr int kColumnKind = 2;
constexpr int kColumnDefault = 3;
constexpr int kColumnTooltip = 4;

const char* const kNewLibraryToken = "\x01new";

struct KindEntry {
    const char* label;
    ParamSpec::Kind kind;
};

const KindEntry kKinds[] = {
    {"Number", ParamSpec::Kind::Real},
    {"Integer", ParamSpec::Kind::Integer},
    {"Vector", ParamSpec::Kind::Vector},
    {"Flag", ParamSpec::Kind::Boolean},
    {"Text", ParamSpec::Kind::Text},
};

QString kindLabel(ParamSpec::Kind kind) {
    for (const KindEntry& entry : kKinds)
        if (entry.kind == kind) return QString::fromLatin1(entry.label);
    return QString::fromLatin1(kKinds[0].label);
}

ParamSpec::Kind kindFromLabel(const QString& label) {
    for (const KindEntry& entry : kKinds)
        if (label == QLatin1String(entry.label)) return entry.kind;
    return ParamSpec::Kind::Real;
}

QString defaultToText(const ParamValue& value) {
    if (auto* b = std::get_if<bool>(&value))
        return *b ? QStringLiteral("true") : QStringLiteral("false");
    if (auto* d = std::get_if<double>(&value)) return QString::number(*d, 'g', 10);
    if (auto* s = std::get_if<std::string>(&value)) return QString::fromStdString(*s);
    if (auto* v = std::get_if<std::vector<double>>(&value)) {
        QStringList parts;
        for (double element : *v) parts << QString::number(element, 'g', 10);
        return parts.join(QStringLiteral(", "));
    }
    return {};
}

bool textToDefault(const QString& text, ParamSpec::Kind kind, ParamValue* out) {
    switch (kind) {
        case ParamSpec::Kind::Boolean: {
            const QString lowered = text.trimmed().toLower();
            *out = lowered == QLatin1String("true") ||
                   lowered == QLatin1String("1") ||
                   lowered == QLatin1String("yes");
            return true;
        }
        case ParamSpec::Kind::Text:
        case ParamSpec::Kind::Choice:
        case ParamSpec::Kind::PythonCode:
            *out = text.toStdString();
            return true;
        case ParamSpec::Kind::Vector: {
            std::vector<double> values;
            const QStringList parts =
                text.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                           Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                bool ok = false;
                const double value = part.toDouble(&ok);
                if (!ok) return false;
                values.push_back(value);
            }
            *out = std::move(values);
            return true;
        }
        case ParamSpec::Kind::Real:
        case ParamSpec::Kind::Integer:
        default: {
            bool ok = false;
            const double value = text.trimmed().isEmpty()
                                     ? 0.0
                                     : numbers::parse(text, &ok);
            if (!text.trimmed().isEmpty() && !ok) return false;
            *out = value;
            return true;
        }
    }
}

bool isUsableParameterName(const QString& name) {
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(name).hasMatch();
}

}

CustomBlockDialog::CustomBlockDialog(CustomBlockDef def, QWidget* parent)
    : QDialog(parent), def_(std::move(def)) {
    setWindowTitle(tr("Save as Custom Block"));
    setModal(true);
    resize(620, 560);

    pendingIcon_ = def_.icon;

    auto* tabs = new QTabWidget(this);

    auto* identity = new QWidget;
    buildIdentityPage(identity);
    tabs->addTab(identity, tr("Block"));

    auto* mask = new QWidget;
    buildMaskPage(mask);
    tabs->addTab(mask, tr("Parameters"));

    auto* icon = new QWidget;
    buildIconPage(icon);
    tabs->addTab(icon, tr("Icon"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                         QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &CustomBlockDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

void CustomBlockDialog::buildIdentityPage(QWidget* page) {
    auto* form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    library_ = new QComboBox;
    for (CustomLibrary* library : LibraryManager::instance().libraries())
        library_->addItem(QString::fromStdString(library->name),
                          QString::fromStdString(library->name));
    library_->addItem(tr("New library…"), QString::fromLatin1(kNewLibraryToken));
    if (!def_.libraryName.empty()) {
        const int index =
            library_->findData(QString::fromStdString(def_.libraryName));
        if (index >= 0) library_->setCurrentIndex(index);
    }
    form->addRow(tr("Library"), library_);

    newLibrary_ = new QLineEdit;
    newLibrary_->setPlaceholderText(tr("Name of the new library"));
    form->addRow(QString(), newLibrary_);

    auto syncNewLibrary = [this] {
        const bool creating =
            library_->currentData().toString() == QLatin1String(kNewLibraryToken);
        newLibrary_->setVisible(creating);
        if (creating) newLibrary_->setFocus();
    };
    connect(library_, &QComboBox::currentIndexChanged, this,
            [syncNewLibrary](int) { syncNewLibrary(); });
    syncNewLibrary();

    name_ = new QLineEdit(QString::fromStdString(def_.name));
    name_->setPlaceholderText(tr("PIAntiWindup"));
    name_->setToolTip(tr("The type name stored in model files. Keep it stable: "
                         "renaming it orphans the blocks already placed in "
                         "existing models."));
    form->addRow(tr("Type name"), name_);

    displayName_ = new QLineEdit(QString::fromStdString(def_.displayName));
    displayName_->setPlaceholderText(tr("PI (anti-windup)"));
    form->addRow(tr("Palette label"), displayName_);

    category_ = new QComboBox;
    category_->setEditable(true);
    QStringList categories;
    for (const std::string& category : BlockRegistry::instance().categories())
        categories << QString::fromStdString(category);
    categories.removeDuplicates();
    category_->addItems(categories);
    category_->setCurrentText(QString::fromStdString(def_.category));
    form->addRow(tr("Category"), category_);

    description_ = new QPlainTextEdit(QString::fromStdString(def_.description));
    description_->setPlaceholderText(
        tr("What it does, and anything the person dropping it on a canvas "
           "needs to know."));
    description_->setMaximumHeight(90);
    form->addRow(tr("Description"), description_);

    auto* size = new QWidget;
    auto* sizeRow = new QHBoxLayout(size);
    sizeRow->setContentsMargins(0, 0, 0, 0);
    width_ = new QDoubleSpinBox;
    width_->setRange(40, 600);
    width_->setValue(def_.defaultWidth);
    height_ = new QDoubleSpinBox;
    height_->setRange(30, 600);
    height_->setValue(def_.defaultHeight);
    sizeRow->addWidget(width_);
    sizeRow->addWidget(new QLabel(QStringLiteral("×")));
    sizeRow->addWidget(height_);
    sizeRow->addStretch(1);
    form->addRow(tr("Default size"), size);

    auto* kind = new QLabel(def_.kind == CustomBlockKind::Python
                                ? tr("Python block")
                                : tr("Subsystem"));
    kind->setEnabled(false);
    form->addRow(tr("Kind"), kind);
}

void CustomBlockDialog::buildMaskPage(QWidget* page) {
    auto* layout = new QVBoxLayout(page);

    auto* hint = new QLabel(
        def_.kind == CustomBlockKind::Python
            ? tr("Parameters declared here reach the script as "
                 "<code>self.params[\"name\"]</code>, and are edited on each "
                 "instance from the property panel.")
            : tr("Parameters declared here are edited on each instance from "
                 "the property panel. To use one, open the block and bind an "
                 "inner parameter to it with the <b>ƒx</b> button — the "
                 "expression is plain Python, so <code>kp</code> and "
                 "<code>[1, 2*zeta*wn, wn**2]</code> both work."));
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    layout->addWidget(hint);

    parameters_ = new QTableWidget(0, 5);
    parameters_->setHorizontalHeaderLabels(
        {tr("Name"), tr("Label"), tr("Type"), tr("Default"), tr("Tooltip")});
    parameters_->horizontalHeader()->setSectionResizeMode(kColumnTooltip,
                                                          QHeaderView::Stretch);
    parameters_->verticalHeader()->setVisible(false);
    parameters_->setSelectionBehavior(QAbstractItemView::SelectRows);
    parameters_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(parameters_, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add"));
    auto* remove = new QPushButton(tr("Remove"));
    auto* up = new QPushButton(tr("Move up"));
    auto* down = new QPushButton(tr("Move down"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(up);
    buttons->addWidget(down);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &CustomBlockDialog::addParameter);
    connect(remove, &QPushButton::clicked, this,
            &CustomBlockDialog::removeParameter);
    connect(up, &QPushButton::clicked, this, [this] { moveParameter(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveParameter(1); });

    loadParameters();
}

void CustomBlockDialog::buildIconPage(QWidget* page) {
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;

    iconKind_ = new QComboBox;
    iconKind_->addItem(tr("Default artwork"), int(BlockIcon::Kind::None));
    iconKind_->addItem(tr("Text or formula"), int(BlockIcon::Kind::Text));
    iconKind_->addItem(tr("Image file"), int(BlockIcon::Kind::Raster));
    const int stored = iconKind_->findData(
        int(pendingIcon_.kind == BlockIcon::Kind::Svg ? BlockIcon::Kind::Raster
                                                      : pendingIcon_.kind));
    iconKind_->setCurrentIndex(stored >= 0 ? stored : 0);
    form->addRow(tr("Icon"), iconKind_);

    iconText_ = new QLineEdit;
    if (pendingIcon_.kind == BlockIcon::Kind::Text)
        iconText_->setText(QString::fromStdString(pendingIcon_.data));
    iconText_->setPlaceholderText(tr("PI, 1/s², Σ …"));
    iconText_->setMaxLength(12);
    form->addRow(tr("Text"), iconText_);

    auto* fileRow = new QWidget;
    auto* fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    iconSource_ = new QLabel;
    iconSource_->setEnabled(false);
    auto* choose = new QPushButton(tr("Choose…"));
    auto* clear = new QPushButton(tr("Clear"));
    fileLayout->addWidget(iconSource_, 1);
    fileLayout->addWidget(choose);
    fileLayout->addWidget(clear);
    form->addRow(tr("File"), fileRow);

    connect(choose, &QPushButton::clicked, this,
            &CustomBlockDialog::chooseIconFile);
    connect(clear, &QPushButton::clicked, this, &CustomBlockDialog::clearIcon);

    layout->addLayout(form);

    auto* preview = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(preview);
    iconPreview_ = new QLabel;
    iconPreview_->setAlignment(Qt::AlignCenter);
    iconPreview_->setMinimumHeight(120);
    previewLayout->addWidget(iconPreview_);
    layout->addWidget(preview);

    if (!icons::svgSupported()) {
        auto* note = new QLabel(
            tr("SVG is unavailable in this build — install qt6-svg-dev and "
               "rebuild to use vector icons. PNG and JPEG work either way."));
        note->setWordWrap(true);
        note->setEnabled(false);
        layout->addWidget(note);
    }

    layout->addStretch(1);

    auto sync = [this] {
        const auto kind =
            BlockIcon::Kind(iconKind_->currentData().toInt());
        iconText_->setEnabled(kind == BlockIcon::Kind::Text);
        refreshIconPreview();
    };
    connect(iconKind_, &QComboBox::currentIndexChanged, this,
            [sync](int) { sync(); });
    connect(iconText_, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshIconPreview(); });
    sync();
}

void CustomBlockDialog::chooseIconFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose an icon"), QString(), icons::fileFilter());
    if (path.isEmpty()) return;

    try {
        pendingIcon_ = icons::fromFile(path);
        iconSource_->setText(QFileInfo(path).fileName());
        const int index = iconKind_->findData(int(BlockIcon::Kind::Raster));
        if (index >= 0) iconKind_->setCurrentIndex(index);
        refreshIconPreview();
    } catch (const ModelError& error) {
        QMessageBox::warning(this, tr("Icon"),
                             QString::fromStdString(error.what()));
    }
}

void CustomBlockDialog::clearIcon() {
    pendingIcon_ = BlockIcon{};
    iconSource_->clear();
    iconKind_->setCurrentIndex(iconKind_->findData(int(BlockIcon::Kind::None)));
    refreshIconPreview();
}

void CustomBlockDialog::refreshIconPreview() {
    const auto kind = BlockIcon::Kind(iconKind_->currentData().toInt());

    BlockIcon shown;
    if (kind == BlockIcon::Kind::Text) {
        shown.kind = BlockIcon::Kind::Text;
        shown.data = iconText_->text().toStdString();
    } else if (kind != BlockIcon::Kind::None &&
               pendingIcon_.kind != BlockIcon::Kind::Text) {
        shown = pendingIcon_;
    }

    const QSize size(int(width_ ? width_->value() : 100.0),
                     int(height_ ? height_->value() : 70.0));
    QPixmap pixmap(size * iconPreview_->devicePixelRatioF());
    pixmap.setDevicePixelRatio(iconPreview_->devicePixelRatioF());
    pixmap.fill(Qt::transparent);

    const theme::Palette& colors = theme::palette();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colors.blockStroke, 1.4));
    painter.setBrush(colors.blockFill);
    painter.drawRoundedRect(QRectF(1, 1, size.width() - 2, size.height() - 2),
                            4, 4);
    icons::paint(&painter, QRectF(6, 5, size.width() - 12, size.height() - 10),
                 shown, colors.blockGlyph);
    painter.end();

    iconPreview_->setPixmap(pixmap);
}

void CustomBlockDialog::loadParameters() {
    parameters_->setRowCount(0);
    for (const ParamSpec& spec : def_.params) {
        const int row = parameters_->rowCount();
        parameters_->insertRow(row);
        parameters_->setItem(
            row, kColumnName,
            new QTableWidgetItem(QString::fromStdString(spec.name)));
        parameters_->setItem(
            row, kColumnLabel,
            new QTableWidgetItem(QString::fromStdString(spec.label)));

        auto* kind = new QComboBox;
        for (const KindEntry& entry : kKinds)
            kind->addItem(QString::fromLatin1(entry.label));
        kind->setCurrentText(kindLabel(spec.kind));
        parameters_->setCellWidget(row, kColumnKind, kind);

        parameters_->setItem(row, kColumnDefault,
                             new QTableWidgetItem(defaultToText(spec.defaultValue)));
        parameters_->setItem(
            row, kColumnTooltip,
            new QTableWidgetItem(QString::fromStdString(spec.tooltip)));
    }
}

void CustomBlockDialog::addParameter() {
    const int row = parameters_->rowCount();
    parameters_->insertRow(row);
    parameters_->setItem(row, kColumnName, new QTableWidgetItem());
    parameters_->setItem(row, kColumnLabel, new QTableWidgetItem());

    auto* kind = new QComboBox;
    for (const KindEntry& entry : kKinds)
        kind->addItem(QString::fromLatin1(entry.label));
    parameters_->setCellWidget(row, kColumnKind, kind);

    parameters_->setItem(row, kColumnDefault, new QTableWidgetItem(QStringLiteral("0")));
    parameters_->setItem(row, kColumnTooltip, new QTableWidgetItem());
    parameters_->setCurrentCell(row, kColumnName);
    parameters_->editItem(parameters_->item(row, kColumnName));
}

void CustomBlockDialog::removeParameter() {
    const int row = parameters_->currentRow();
    if (row >= 0) parameters_->removeRow(row);
}

void CustomBlockDialog::moveParameter(int delta) {
    const int row = parameters_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= parameters_->rowCount()) return;

    auto readRow = [this](int r) {
        ParamSpec spec;
        spec.name = parameters_->item(r, kColumnName)->text().toStdString();
        spec.label = parameters_->item(r, kColumnLabel)->text().toStdString();
        if (auto* kind = qobject_cast<QComboBox*>(
                parameters_->cellWidget(r, kColumnKind)))
            spec.kind = kindFromLabel(kind->currentText());
        spec.tooltip = parameters_->item(r, kColumnTooltip)->text().toStdString();
        return std::make_pair(spec,
                              parameters_->item(r, kColumnDefault)->text());
    };
    auto writeRow = [this](int r, const std::pair<ParamSpec, QString>& data) {
        parameters_->item(r, kColumnName)
            ->setText(QString::fromStdString(data.first.name));
        parameters_->item(r, kColumnLabel)
            ->setText(QString::fromStdString(data.first.label));
        if (auto* kind = qobject_cast<QComboBox*>(
                parameters_->cellWidget(r, kColumnKind)))
            kind->setCurrentText(kindLabel(data.first.kind));
        parameters_->item(r, kColumnDefault)->setText(data.second);
        parameters_->item(r, kColumnTooltip)
            ->setText(QString::fromStdString(data.first.tooltip));
    };

    const auto moving = readRow(row);
    const auto displaced = readRow(target);
    writeRow(row, displaced);
    writeRow(target, moving);
    parameters_->setCurrentCell(target, kColumnName);
}

bool CustomBlockDialog::commitParameters(QString* problem) {
    std::vector<ParamSpec> specs;

    for (int row = 0; row < parameters_->rowCount(); ++row) {
        ParamSpec spec;
        spec.name = parameters_->item(row, kColumnName)->text().trimmed().toStdString();
        if (spec.name.empty()) continue;

        if (!isUsableParameterName(QString::fromStdString(spec.name))) {
            *problem = tr("'%1' cannot be a parameter name: use a letter or "
                          "underscore followed by letters, digits or "
                          "underscores, so an expression can refer to it.")
                           .arg(QString::fromStdString(spec.name));
            return false;
        }
        for (const ParamSpec& existing : specs)
            if (existing.name == spec.name) {
                *problem = tr("There are two parameters named '%1'.")
                               .arg(QString::fromStdString(spec.name));
                return false;
            }

        const QString label = parameters_->item(row, kColumnLabel)->text().trimmed();
        spec.label = label.isEmpty() ? spec.name : label.toStdString();

        if (auto* kind = qobject_cast<QComboBox*>(
                parameters_->cellWidget(row, kColumnKind)))
            spec.kind = kindFromLabel(kind->currentText());

        const QString defaultText =
            parameters_->item(row, kColumnDefault)->text().trimmed();
        if (!textToDefault(defaultText, spec.kind, &spec.defaultValue)) {
            *problem = tr("The default for '%1' is not a valid %2.")
                           .arg(QString::fromStdString(spec.name),
                                kindLabel(spec.kind).toLower());
            return false;
        }

        spec.tooltip =
            parameters_->item(row, kColumnTooltip)->text().trimmed().toStdString();
        specs.push_back(std::move(spec));
    }

    def_.params = std::move(specs);
    return true;
}

QString CustomBlockDialog::targetLibrary() const {
    const QString data = library_->currentData().toString();
    if (data == QLatin1String(kNewLibraryToken)) return newLibrary_->text().trimmed();
    return data;
}

void CustomBlockDialog::pinLibrary(const QString& name) {
    const int index = library_->findData(name);
    if (index >= 0) library_->setCurrentIndex(index);
    library_->setEnabled(false);
    newLibrary_->setVisible(false);
}

void CustomBlockDialog::accept() {
    const QString typeName = name_->text().trimmed();
    if (typeName.isEmpty()) {
        QMessageBox::warning(this, tr("Save as Custom Block"),
                             tr("The block needs a type name."));
        return;
    }
    if (!isUsableParameterName(typeName)) {
        QMessageBox::warning(
            this, tr("Save as Custom Block"),
            tr("'%1' cannot be a type name: use a letter or underscore "
               "followed by letters, digits or underscores.")
                .arg(typeName));
        return;
    }

    if (targetLibrary().isEmpty()) {
        QMessageBox::warning(this, tr("Save as Custom Block"),
                             tr("Name the library this block goes into."));
        return;
    }

    if (!LibraryManager::instance().isNameAvailable(
            typeName.toStdString(), targetLibrary().toStdString())) {
        QMessageBox::warning(
            this, tr("Save as Custom Block"),
            tr("'%1' is already the name of another block type. Blocks are "
               "identified by this name in every model file, so it has to be "
               "unique.")
                .arg(typeName));
        return;
    }

    QString problem;
    if (!commitParameters(&problem)) {
        QMessageBox::warning(this, tr("Parameters"), problem);
        return;
    }

    def_.name = typeName.toStdString();
    const QString displayed = displayName_->text().trimmed();
    def_.displayName = displayed.isEmpty() ? def_.name : displayed.toStdString();
    def_.category = category_->currentText().trimmed().toStdString();
    if (def_.category.empty()) def_.category = "Custom";
    def_.description = description_->toPlainText().trimmed().toStdString();
    def_.defaultWidth = width_->value();
    def_.defaultHeight = height_->value();

    const auto iconKind = BlockIcon::Kind(iconKind_->currentData().toInt());
    if (iconKind == BlockIcon::Kind::None) {
        def_.icon = BlockIcon{};
    } else if (iconKind == BlockIcon::Kind::Text) {
        def_.icon.kind = BlockIcon::Kind::Text;
        def_.icon.data = iconText_->text().trimmed().toStdString();
        if (def_.icon.data.empty()) def_.icon = BlockIcon{};
    } else {
        def_.icon = pendingIcon_.kind == BlockIcon::Kind::Text ? BlockIcon{}
                                                               : pendingIcon_;
    }

    QDialog::accept();
}

}
