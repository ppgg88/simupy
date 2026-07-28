#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QListWidget;
QT_END_NAMESPACE

namespace simupy {

/// Type-to-insert search over every registered block type.
///
/// Opens where the user last clicked on the canvas and closes as soon as a
/// block is chosen or the search is abandoned, so adding a block never means
/// crossing the window to the palette.
class QuickAddPopup : public QWidget {
    Q_OBJECT

public:
    explicit QuickAddPopup(QWidget* parent = nullptr);

    void start(const QPoint& globalPos, const QString& initialText);

    static QStringList rankBlockTypes(const QString& query, int limit = 60);

signals:
    void blockChosen(const QString& typeName);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh(const QString& query);
    void chooseCurrent();

    QLineEdit* search_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
};

}  // namespace simupy
