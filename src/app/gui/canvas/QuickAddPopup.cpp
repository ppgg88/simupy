#include "QuickAddPopup.h"

#include "app/gui/style/BlockIconRenderer.h"
#include "app/gui/style/Theme.h"
#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>

namespace simupy {
namespace {

constexpr int kTypeNameRole = Qt::UserRole + 1;
constexpr int kMaxResults = 60;

int scoreOf(const QString& name, const QString& category,
            const QString& description, const QString& query) {
    if (query.isEmpty()) return 100;

    const Qt::CaseSensitivity ci = Qt::CaseInsensitive;

    if (name.compare(query, ci) == 0) return 0;
    if (name.startsWith(query, ci)) return 1;

    for (int i = 1; i < name.size(); ++i) {
        const bool boundary = name[i].isUpper() || name[i - 1] == QLatin1Char(' ');
        if (boundary && QStringView(name).sliced(i).startsWith(query, ci))
            return 2;
    }

    if (name.contains(query, ci)) return 3;
    if (category.startsWith(query, ci)) return 4;

    int cursor = 0;
    for (const QChar& wanted : query) {
        bool found = false;
        while (cursor < name.size()) {
            if (name[cursor++].toLower() == wanted.toLower()) {
                found = true;
                break;
            }
        }
        if (!found) {
            cursor = -1;
            break;
        }
    }
    if (cursor >= 0) return 5;

    if (description.contains(query, ci)) return 6;
    return -1;
}

}

QuickAddPopup::QuickAddPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedWidth(340);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Add a block…"));
    search_->setClearButtonEnabled(false);

    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(true);
    list_->setMaximumHeight(260);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    hint_ = new QLabel(tr("↑↓ to choose · Enter to add · Esc to cancel"), this);
    hint_->setEnabled(false);
    QFont hintFont = hint_->font();
    hintFont.setPointSize(std::max(7, hintFont.pointSize() - 2));
    hint_->setFont(hintFont);

    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setAutoFillBackground(true);
    frame->setBackgroundRole(QPalette::Window);

    auto* inner = new QVBoxLayout(frame);
    inner->setContentsMargins(6, 6, 6, 4);
    inner->setSpacing(4);
    inner->addWidget(search_);
    inner->addWidget(list_, 1);
    inner->addWidget(hint_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(frame);

    connect(search_, &QLineEdit::textChanged, this, &QuickAddPopup::refresh);
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { chooseCurrent(); });
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { chooseCurrent(); });

    search_->installEventFilter(this);
}

void QuickAddPopup::start(const QPoint& globalPos, const QString& initialText) {
    search_->blockSignals(true);
    search_->setText(initialText);
    search_->blockSignals(false);
    refresh(initialText);

    adjustSize();

    QRect available = QGuiApplication::screenAt(globalPos)
                          ? QGuiApplication::screenAt(globalPos)->availableGeometry()
                          : QGuiApplication::primaryScreen()->availableGeometry();
    QPoint target = globalPos + QPoint(8, 8);
    target.setX(std::min(target.x(), available.right() - width() - 4));
    target.setY(std::min(target.y(), available.bottom() - height() - 4));
    target.setX(std::max(target.x(), available.left() + 4));
    target.setY(std::max(target.y(), available.top() + 4));
    move(target);

    show();
    search_->setFocus(Qt::PopupFocusReason);
    search_->selectAll();
    search_->deselect();
    search_->end(false);
}

QStringList QuickAddPopup::rankBlockTypes(const QString& query, int limit) {
    const QString needle = query.trimmed();

    struct Candidate {
        QString typeName;
        int score;
        int order;
    };
    std::vector<Candidate> candidates;

    int order = 0;
    for (const BlockType* type : BlockRegistry::instance().all()) {
        const int score = scoreOf(QString::fromStdString(type->displayName),
                                  QString::fromStdString(type->category),
                                  QString::fromStdString(type->description),
                                  needle);
        if (score >= 0)
            candidates.push_back(
                {QString::fromStdString(type->name), score, order});
        ++order;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.score != b.score) return a.score < b.score;
                         return a.order < b.order;
                     });

    QStringList result;
    for (const Candidate& candidate : candidates) {
        if (result.size() >= limit) break;
        result << candidate.typeName;
    }
    return result;
}

void QuickAddPopup::refresh(const QString& query) {
    list_->clear();

    const BlockRegistry& registry = BlockRegistry::instance();
    const LibraryManager& manager = LibraryManager::instance();
    const theme::Palette& colors = theme::palette();

    for (const QString& typeName : rankBlockTypes(query, kMaxResults)) {
        const BlockType* type = registry.find(typeName.toStdString());
        if (!type) continue;

        auto* item = new QListWidgetItem(list_);
        item->setText(QStringLiteral("%1    %2")
                          .arg(QString::fromStdString(type->displayName),
                               QString::fromStdString(type->category)));
        item->setData(kTypeNameRole, typeName);
        item->setToolTip(QString::fromStdString(type->description));

        if (const CustomBlockDef* def = manager.definition(type->name))
            item->setIcon(icons::toIcon(def->icon, colors.blockGlyph));
    }

    if (list_->count() > 0) list_->setCurrentRow(0);

    hint_->setText(list_->count() == 0
                       ? tr("No block matches — Esc to cancel")
                       : tr("↑↓ to choose · Enter to add · Esc to cancel"));
}

void QuickAddPopup::chooseCurrent() {
    QListWidgetItem* item = list_->currentItem();
    if (!item) return;

    const QString typeName = item->data(kTypeNameRole).toString();
    close();
    if (!typeName.isEmpty()) emit blockChosen(typeName);
}

bool QuickAddPopup::eventFilter(QObject* watched, QEvent* event) {
    if (watched != search_ || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    switch (key->key()) {
        case Qt::Key_Escape:
            close();
            return true;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            chooseCurrent();
            return true;

        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QCoreApplication::sendEvent(list_, key);
            return true;

        default:
            return QWidget::eventFilter(watched, event);
    }
}

}
