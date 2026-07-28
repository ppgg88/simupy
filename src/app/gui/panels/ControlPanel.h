#pragma once

#include "model/Model.h"

#include <QList>
#include <QWidget>

#include <functional>

QT_BEGIN_NAMESPACE
class QLabel;
class QScrollArea;
class QVBoxLayout;
QT_END_NAMESPACE

namespace simupy {

class InteractiveBlock;

class ControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    /// Rebuilds from the model hierarchy. Call after loading, and whenever a
    /// block is added, removed or renamed.
    void setModel(Model* root);

    void refreshValues();

    bool commitValues();

    bool isEmpty() const { return controls_.isEmpty(); }

signals:
    void controlChanged();

private:
    struct Entry {
        InteractiveBlock* block = nullptr;
        QString path;
    };

    void collect(Model& model, const QString& prefix);
    QWidget* buildWidget(const Entry& entry);
    void clearWidgets();

    Model* root_ = nullptr;
    QList<Entry> controls_;
    QList<std::function<void()>> refreshers_;

    QScrollArea* scroll_;
    QWidget* content_;
    QVBoxLayout* form_;
    QLabel* empty_;
};

}  // namespace simupy
