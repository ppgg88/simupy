#include "BlockLibraryPanel.h"

#include "app/gui/style/BlockIconRenderer.h"
#include "app/gui/canvas/DiagramScene.h"
#include "app/gui/style/Theme.h"
#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"

#include <QDrag>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr int kTypeNameRole = Qt::UserRole + 1;
constexpr int kLibraryRole = Qt::UserRole + 2;

class LibraryTree : public QTreeWidget {
public:
    explicit LibraryTree(QWidget* parent = nullptr) : QTreeWidget(parent) {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty()) return nullptr;
        const QString typeName = items.first()->data(0, kTypeNameRole).toString();
        if (typeName.isEmpty()) return nullptr;

        auto* data = new QMimeData;
        data->setData(DiagramScene::kBlockMimeType, typeName.toUtf8());
        data->setText(typeName);
        return data;
    }

    QStringList mimeTypes() const override {
        return {QString::fromLatin1(DiagramScene::kBlockMimeType)};
    }
};

}

BlockLibraryPanel::BlockLibraryPanel(QWidget* parent) : QWidget(parent) {
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search blocks…"));
    search_->setClearButtonEnabled(true);

    tree_ = new LibraryTree(this);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(12);
    tree_->setAlternatingRowColors(false);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);

    connect(search_, &QLineEdit::textChanged, this,
            &BlockLibraryPanel::applyFilter);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString typeName =
                    item->data(0, kTypeNameRole).toString();
                if (!typeName.isEmpty()) emit blockActivated(typeName);
            });

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this,
            &BlockLibraryPanel::showContextMenu);

    reload();
}

void BlockLibraryPanel::reload() {
    tree_->clear();
    const BlockRegistry& registry = BlockRegistry::instance();
    const LibraryManager& manager = LibraryManager::instance();
    const theme::Palette& colors = theme::palette();

    for (const std::string& category : registry.categories()) {
        auto* group = new QTreeWidgetItem(tree_);
        group->setText(0, QString::fromStdString(category));
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
        group->setFlags(Qt::ItemIsEnabled);

        for (const BlockType* type : registry.inCategory(category)) {
            auto* item = new QTreeWidgetItem(group);
            item->setText(0, QString::fromStdString(type->displayName));
            item->setData(0, kTypeNameRole,
                          QString::fromStdString(type->name));

            QString tooltip = QString::fromStdString(type->description);
            if (const CustomBlockDef* def = manager.definition(type->name)) {
                item->setIcon(0, icons::toIcon(def->icon, colors.blockGlyph));
                item->setData(0, kLibraryRole,
                              QString::fromStdString(def->libraryName));
                tooltip += tooltip.isEmpty() ? QString() : QStringLiteral("\n\n");
                tooltip += tr("From the '%1' library")
                               .arg(QString::fromStdString(def->libraryName));
            }
            item->setToolTip(0, tooltip);
        }
    }
    tree_->expandAll();
}

void BlockLibraryPanel::showContextMenu(const QPoint& position) {
    QTreeWidgetItem* item = tree_->itemAt(position);
    const QString typeName =
        item ? item->data(0, kTypeNameRole).toString() : QString();
    const QString libraryName =
        item ? item->data(0, kLibraryRole).toString() : QString();

    QMenu menu(this);

    if (!typeName.isEmpty()) {
        QAction* add = menu.addAction(tr("Add to diagram"));
        connect(add, &QAction::triggered, this,
                [this, typeName] { emit blockActivated(typeName); });
    }

    if (!libraryName.isEmpty()) {
        menu.addSeparator();
        QAction* edit = menu.addAction(tr("Edit '%1'…").arg(item->text(0)));
        connect(edit, &QAction::triggered, this, [this, typeName] {
            emit customBlockEditRequested(typeName);
        });
    }

    menu.addSeparator();
    QAction* manage = menu.addAction(tr("Manage libraries…"));
    connect(manage, &QAction::triggered, this,
            [this] { emit librariesRequested(); });

    menu.exec(tree_->viewport()->mapToGlobal(position));
}

void BlockLibraryPanel::applyFilter(const QString& text) {
    const QString needle = text.trimmed();

    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = tree_->topLevelItem(g);
        int visible = 0;

        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* item = group->child(i);
            const bool matches =
                needle.isEmpty() ||
                item->text(0).contains(needle, Qt::CaseInsensitive) ||
                item->toolTip(0).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches) ++visible;
        }

        group->setHidden(visible == 0);
        if (!needle.isEmpty() && visible > 0) group->setExpanded(true);
    }
}

}
