#include "LibraryManagerDialog.h"

#include "app/gui/style/BlockIconRenderer.h"
#include "CustomBlockDialog.h"
#include "app/gui/style/Theme.h"
#include "io/CustomBlock.h"
#include "io/LibrarySerializer.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include "PackageRequirementsDialog.h"
#include "scripting/PythonPackages.h"
#include "scripting/PythonEngine.h"
#include <QApplication>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr int kLibraryRole = Qt::UserRole + 1;
constexpr int kBlockRole = Qt::UserRole + 2;

QString libraryFilter() {
    return QObject::tr("SimuPy block libraries (*.spylib)");
}

}

LibraryManagerDialog::LibraryManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Block Libraries"));
    setModal(true);
    resize(680, 480);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Library"), tr("Category"), tr("Kind")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);

    details_ = new QLabel(this);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setMinimumHeight(64);
    details_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto* actions = new QVBoxLayout;
    auto* importButton = new QPushButton(tr("Install…"));
    importButton->setToolTip(
        tr("Copy a .spylib file into your library folder and load it."));
    auto* createButton = new QPushButton(tr("New library…"));
    exportButton_ = new QPushButton(tr("Export…"));
    exportButton_->setToolTip(
        tr("Write a copy of the library out, to send to someone else."));
    editButton_ = new QPushButton(tr("Edit block…"));
    removeButton_ = new QPushButton(tr("Remove"));
    declareButton_ = new QPushButton(tr("Python packages…"));
    declareButton_->setToolTip(
        tr("Declare what this library imports, or read it off the sources."));
    packagesButton_ = new QPushButton(tr("Install packages"));
    packagesButton_->setToolTip(
        tr("Fetch the Python packages this library needs into SimuPy's own "
           "folder, leaving the system's Python untouched."));
    auto* folderButton = new QPushButton(tr("Open folder"));

    actions->addWidget(importButton);
    actions->addWidget(createButton);
    actions->addSpacing(12);
    actions->addWidget(editButton_);
    actions->addWidget(exportButton_);
    actions->addWidget(removeButton_);
    actions->addSpacing(12);
    actions->addWidget(declareButton_);
    actions->addWidget(packagesButton_);
    actions->addStretch(1);
    actions->addWidget(folderButton);

    auto* body = new QHBoxLayout;
    body->addWidget(tree_, 1);
    body->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(details_);
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this,
            &LibraryManagerDialog::importLibrary);
    connect(createButton, &QPushButton::clicked, this,
            &LibraryManagerDialog::createLibrary);
    connect(exportButton_, &QPushButton::clicked, this,
            &LibraryManagerDialog::exportSelected);
    connect(removeButton_, &QPushButton::clicked, this,
            &LibraryManagerDialog::removeSelected);
    connect(editButton_, &QPushButton::clicked, this,
            &LibraryManagerDialog::editSelected);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this,
            &LibraryManagerDialog::onSelectionChanged);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { editSelected(); });
    connect(declareButton_, &QPushButton::clicked, this,
            &LibraryManagerDialog::editPackages);
    connect(packagesButton_, &QPushButton::clicked, this,
            &LibraryManagerDialog::installPackages);
    connect(folderButton, &QPushButton::clicked, this, [this] {
        const QString directory =
            QString::fromStdString(LibraryManager::instance().userDirectory());
        QDir().mkpath(directory);
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });

    reload();
}

void LibraryManagerDialog::reload() {
    tree_->clear();
    const theme::Palette& colors = theme::palette();

    for (CustomLibrary* library : LibraryManager::instance().libraries()) {
        auto* group = new QTreeWidgetItem(tree_);
        group->setText(0, QString::fromStdString(library->name));
        group->setText(1, tr("%n block(s)", "", int(library->blocks.size())));
        group->setText(2, tr("revision %1").arg(library->revision));
        group->setData(0, kLibraryRole, QString::fromStdString(library->name));
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);

        for (const CustomBlockDef& def : library->blocks) {
            auto* item = new QTreeWidgetItem(group);
            item->setText(0, QString::fromStdString(def.displayName));
            item->setText(1, QString::fromStdString(def.category));
            item->setText(2, def.kind == CustomBlockKind::Python
                                 ? tr("Python")
                                 : tr("Subsystem"));
            item->setData(0, kLibraryRole,
                          QString::fromStdString(library->name));
            item->setData(0, kBlockRole, QString::fromStdString(def.name));
            item->setIcon(0, icons::toIcon(def.icon, colors.blockGlyph));
        }
    }
    tree_->expandAll();
    onSelectionChanged();
}

QString LibraryManagerDialog::selectedLibrary() const {
    const QTreeWidgetItem* item = tree_->currentItem();
    return item ? item->data(0, kLibraryRole).toString() : QString();
}

QString LibraryManagerDialog::selectedBlock() const {
    const QTreeWidgetItem* item = tree_->currentItem();
    return item ? item->data(0, kBlockRole).toString() : QString();
}

std::vector<PackageRequirement> LibraryManagerDialog::missingPackages(
    const CustomLibrary& library) const {
    std::vector<PackageRequirement> missing;
    for (const PackageRequirement& need : library.requires_)
        if (!PythonPackages::instance().status(need.module).installed)
            missing.push_back(need);
    return missing;
}

QString LibraryManagerDialog::packageReport(const CustomLibrary& library) const {
    if (library.requires_.empty()) return {};

    QStringList rows;
    for (const PackageRequirement& need : library.requires_) {
        const PackageStatus status =
            PythonPackages::instance().status(need.module);

        QString row = QStringLiteral("<code>%1</code>")
                          .arg(QString::fromStdString(need.module));
        if (status.installed) {
            row += status.version.empty()
                       ? tr(" — installed")
                       : tr(" — installed, %1")
                             .arg(QString::fromStdString(status.version));
        } else {
            row += tr(" — <b>missing</b>, pip name <code>%1</code>")
                       .arg(QString::fromStdString(need.installName()));
        }
        if (!need.purpose.empty())
            row += QStringLiteral(" <i>(%1)</i>")
                       .arg(QString::fromStdString(need.purpose));
        rows << row;
    }
    return tr("<br><br>Python packages:<br>") + rows.join(QStringLiteral("<br>"));
}

void LibraryManagerDialog::editPackages() {
    CustomLibrary* library =
        LibraryManager::instance().library(selectedLibrary().toStdString());
    if (!library) return;

    std::vector<std::string> sources;
    for (const CustomBlockDef& def : library->blocks) {
        if (!def.code.empty()) sources.push_back(def.code);
        if (!def.parameterScript.empty())
            sources.push_back(def.parameterScript);
    }

    PackageRequirementsDialog dialog(QString::fromStdString(library->name),
                                     library->requires_, std::move(sources),
                                     this);
    if (dialog.exec() != QDialog::Accepted) return;

    library->requires_ = dialog.requirements();
    try {
        LibraryManager::instance().save(*library);
    } catch (const ModelError& error) {
        QMessageBox::warning(this, tr("Python packages"),
                             QString::fromStdString(error.what()));
        return;
    }
    onSelectionChanged();
    emit librariesChanged();
}

void LibraryManagerDialog::installPackages() {
    const QString libraryName = selectedLibrary();
    CustomLibrary* library =
        LibraryManager::instance().library(libraryName.toStdString());
    if (!library) return;

    const std::vector<PackageRequirement> missing = missingPackages(*library);
    if (missing.empty()) return;

    QStringList names;
    for (const PackageRequirement& need : missing)
        names << QString::fromStdString(need.installName());

    const QString where =
        QString::fromStdString(PythonPackages::instance().directory());
    if (QMessageBox::question(
            this, tr("Install Python packages"),
            tr("Fetch %1 from PyPI into\n\n%2\n\nThe system's Python is not "
               "touched. This needs a network connection.")
                .arg(names.join(QStringLiteral(", ")), where),
            QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList failures;
    for (const PackageRequirement& need : missing) {
        const std::string problem = PythonPackages::instance().install(
            need.installName(), [](const std::string& text) {
                PythonEngine::instance().emitOutput(text, false);
            });
        if (!problem.empty())
            failures << QString::fromStdString(problem);
    }
    QApplication::restoreOverrideCursor();

    if (failures.isEmpty())
        QMessageBox::information(this, tr("Install Python packages"),
                                 tr("Installed: %1").arg(names.join(", ")));
    else
        QMessageBox::warning(this, tr("Install Python packages"),
                             failures.join(QStringLiteral("\n")));

    onSelectionChanged();
}

void LibraryManagerDialog::onSelectionChanged() {
    const QString libraryName = selectedLibrary();
    const QString blockName = selectedBlock();

    exportButton_->setEnabled(!libraryName.isEmpty());
    removeButton_->setEnabled(!libraryName.isEmpty());
    editButton_->setEnabled(!blockName.isEmpty());
    removeButton_->setText(blockName.isEmpty() ? tr("Remove library")
                                               : tr("Remove block"));

    packagesButton_->setEnabled(false);
    declareButton_->setEnabled(!libraryName.isEmpty());

    CustomLibrary* library =
        LibraryManager::instance().library(libraryName.toStdString());
    if (!library) {
        details_->setText(
            tr("Libraries are loaded from %1 and from any directory listed in "
               "SIMUPY_LIBRARY_PATH.")
                .arg(QString::fromStdString(
                    LibraryManager::instance().userDirectory())));
        return;
    }

    if (blockName.isEmpty()) {
        QString text = QStringLiteral("<b>%1</b>").arg(
            QString::fromStdString(library->name));
        if (!library->author.empty())
            text += tr(" — by %1").arg(QString::fromStdString(library->author));
        if (!library->description.empty())
            text += QStringLiteral("<br>") +
                    QString::fromStdString(library->description);
        text += QStringLiteral("<br><small>%1</small>")
                    .arg(QString::fromStdString(library->path));
        text += packageReport(*library);
        details_->setText(text);
        packagesButton_->setEnabled(!missingPackages(*library).empty());
        return;
    }

    const CustomBlockDef* def = library->find(blockName.toStdString());
    if (!def) return;

    QString text = QStringLiteral("<b>%1</b> <code>%2</code>")
                       .arg(QString::fromStdString(def->displayName),
                            QString::fromStdString(def->name));
    if (!def->description.empty())
        text += QStringLiteral("<br>") +
                QString::fromStdString(def->description);
    if (!def->params.empty()) {
        QStringList names;
        for (const ParamSpec& spec : def->params)
            names << QString::fromStdString(spec.name);
        text += tr("<br>Parameters: %1").arg(names.join(QStringLiteral(", ")));
    }
    details_->setText(text);
}

void LibraryManagerDialog::importLibrary() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Install a block library"), QString(), libraryFilter());
    if (path.isEmpty()) return;

    try {
        CustomLibrary& library = LibraryManager::instance().import(path.toStdString());
        reload();
        emit librariesChanged();
        QMessageBox::information(
            this, tr("Block Libraries"),
            tr("Installed '%1' — %n block(s) added to the palette.", "",
               int(library.blocks.size()))
                .arg(QString::fromStdString(library.name)));
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Block Libraries"),
                              QString::fromStdString(error.what()));
    }
}

void LibraryManagerDialog::createLibrary() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New library"), tr("Library name"), QLineEdit::Normal,
        tr("My Blocks"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    try {
        LibraryManager::instance().create(name.trimmed().toStdString());
        reload();
        emit librariesChanged();
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Block Libraries"),
                              QString::fromStdString(error.what()));
    }
}

void LibraryManagerDialog::exportSelected() {
    CustomLibrary* library =
        LibraryManager::instance().library(selectedLibrary().toStdString());
    if (!library) return;

    const QString suggested =
        QDir::homePath() + QLatin1Char('/') +
        QString::fromStdString(library->name).replace(QLatin1Char(' '),
                                                      QLatin1Char('-')) +
        QStringLiteral(".spylib");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export block library"), suggested, libraryFilter());
    if (path.isEmpty()) return;

    try {
        LibrarySerializer::save(*library, path.toStdString());
        QMessageBox::information(
            this, tr("Block Libraries"),
            tr("Wrote '%1'. Send that one file — the diagrams, the Python "
               "sources and the icons are all inside it.")
                .arg(QFileInfo(path).fileName()));
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Block Libraries"),
                              QString::fromStdString(error.what()));
    }
}

void LibraryManagerDialog::removeSelected() {
    LibraryManager& manager = LibraryManager::instance();
    CustomLibrary* library = manager.library(selectedLibrary().toStdString());
    if (!library) return;

    const QString blockName = selectedBlock();

    if (!blockName.isEmpty()) {
        if (QMessageBox::question(
                this, tr("Remove block"),
                tr("Remove '%1' from '%2'?\n\nModels that already use it keep "
                   "working — a saved model carries its own copy — but the "
                   "block leaves the palette and loses its icon and "
                   "parameter labels.")
                    .arg(blockName,
                         QString::fromStdString(library->name))) !=
            QMessageBox::Yes)
            return;

        try {
            manager.removeBlock(*library, blockName.toStdString());
        } catch (const ModelError& error) {
            QMessageBox::critical(this, tr("Block Libraries"),
                                  QString::fromStdString(error.what()));
            return;
        }
        reload();
        emit librariesChanged();
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Remove library"),
        tr("Remove '%1' and its %n block(s) from the palette?\n\nChoose Yes to "
           "delete %2 as well, or No to only unload it.", "",
           int(library->blocks.size()))
            .arg(QString::fromStdString(library->name),
                 QString::fromStdString(library->path)),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return;

    manager.remove(library->name, answer == QMessageBox::Yes);
    reload();
    emit librariesChanged();
}

void LibraryManagerDialog::editSelected() {
    LibraryManager& manager = LibraryManager::instance();
    CustomLibrary* library = manager.library(selectedLibrary().toStdString());
    const QString blockName = selectedBlock();
    if (!library || blockName.isEmpty()) return;

    const CustomBlockDef* stored = library->find(blockName.toStdString());
    if (!stored) return;

    CustomBlockDialog dialog(*stored, this);
    dialog.setWindowTitle(tr("Edit Custom Block"));
    dialog.pinLibrary(QString::fromStdString(library->name));
    if (dialog.exec() != QDialog::Accepted) return;

    CustomBlockDef edited = dialog.definition();
    const std::string previousName = stored->name;

    try {
        if (edited.name != previousName)
            manager.removeBlock(*library, previousName);
        manager.addBlock(*library, std::move(edited));
    } catch (const ModelError& error) {
        QMessageBox::critical(this, tr("Block Libraries"),
                              QString::fromStdString(error.what()));
    }

    reload();
    emit librariesChanged();
}

}
