#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace simupy {

class BlockLibraryPanel : public QWidget {
    Q_OBJECT

public:
    explicit BlockLibraryPanel(QWidget* parent = nullptr);

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

}
