#include "ConsoleDock.h"

#include "app/gui/style/Theme.h"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr int kMaxBlocks = 5000;

}

ConsoleDock::ConsoleDock(QWidget* parent) : QWidget(parent) {
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setFont(theme::monospaceFont(9));
    view_->setMaximumBlockCount(kMaxBlocks);
    view_->setFrameShape(QFrame::NoFrame);

    auto* clearButton = new QPushButton(tr("Clear"), this);
    connect(clearButton, &QPushButton::clicked, this,
            &ConsoleDock::clearConsole);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->addStretch(1);
    controls->addWidget(clearButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(view_, 1);
    layout->addLayout(controls);
}

void ConsoleDock::writeLine(const QString& line, const QColor& color) {
    QTextCharFormat format;
    format.setForeground(color);

    QTextCursor cursor(view_->document());
    cursor.movePosition(QTextCursor::End);
    if (!view_->document()->isEmpty()) cursor.insertBlock();
    cursor.setCharFormat(format);
    cursor.insertText(line);

    view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
}

void ConsoleDock::appendOutput(const QString& text, bool isError) {
    if (isError != pendingIsError_ && !pending_.isEmpty()) {
        writeLine(pending_, pendingIsError_ ? theme::palette().error
                                            : theme::palette().blockText);
        pending_.clear();
    }
    pendingIsError_ = isError;
    pending_ += text;

    const QColor color =
        isError ? theme::palette().error : theme::palette().blockText;

    int newline;
    while ((newline = pending_.indexOf(QLatin1Char('\n'))) >= 0) {
        writeLine(pending_.left(newline), color);
        pending_ = pending_.mid(newline + 1);
    }
}

void ConsoleDock::appendMessage(const QString& message) {
    if (!pending_.isEmpty()) {
        writeLine(pending_, pendingIsError_ ? theme::palette().error
                                            : theme::palette().blockText);
        pending_.clear();
    }
    writeLine(message, theme::palette().accent);
}

void ConsoleDock::appendError(const QString& message) {
    if (!pending_.isEmpty()) {
        writeLine(pending_, theme::palette().blockText);
        pending_.clear();
    }
    for (const QString& line : message.split(QLatin1Char('\n')))
        writeLine(line, theme::palette().error);
}

void ConsoleDock::clearConsole() {
    view_->clear();
    pending_.clear();
}

}
