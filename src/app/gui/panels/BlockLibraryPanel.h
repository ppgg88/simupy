#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace simupy {

/// Palette of available blocks, grouped by category, with a search box.
/// Items are drag sources for the canvas and can also be added by
/// double-clicking.
///
/// Blocks from an installed library sit alongside the built-in ones, drawn
/// with their own icon, and carry a context menu for editing or exporting the
/// library they came from.
class BlockLibraryPanel : public QWidget {
    Q_OBJECT

public:
    explicit BlockLibraryPanel(QWidget* parent = nullptr);

    /// Rebuilds the tree from the registry. Called at startup and whenever a
    /// library is installed, edited or removed.
    void reload();

signals:
    void blockActivated(const QString& typeName);

    void librariesRequested();

    void customBlockEditRequested(const QString& typeName);

private:
    void applyFilter(const QString& text);
    void showContextMenu(const QPoint& position);

    QLineEdit* search_;
    QTreeWidget* tree_;
};

}  // namespace simupy
