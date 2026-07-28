#include "PythonEditor.h"

#include "PythonHighlighter.h"
#include "app/gui/style/Theme.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTextBlock>
#include <QVBoxLayout>

namespace simupy {
namespace {

constexpr int kIndentWidth = 4;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor), editor_(editor) {}

    QSize sizeHint() const override {
        return QSize(editor_->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        editor_->paintLineNumbers(event);
    }

private:
    CodeEditor* editor_;
};

QString leadingWhitespace(const QString& text) {
    int i = 0;
    while (i < text.size() && (text[i] == QLatin1Char(' ') ||
                               text[i] == QLatin1Char('\t')))
        ++i;
    return text.left(i);
}

}

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    gutter_ = new LineNumberArea(this);

    setFont(theme::monospaceFont(10));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) *
                       kIndentWidth);

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    for (int lines = std::max(1, blockCount()); lines >= 10; lines /= 10)
        ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0)
        gutter_->scroll(0, dy);
    else
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());

    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    gutter_->setGeometry(
        QRect(area.left(), area.top(), lineNumberAreaWidth(), area.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor background = palette().color(QPalette::Base);
        background = background.lightness() < 128 ? background.lighter(125)
                                                  : background.darker(104);
        selection.format.setBackground(background);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
}

void CodeEditor::paintLineNumbers(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), palette().color(QPalette::Window));

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    const int currentLine = textCursor().blockNumber();
    const QColor dim = palette().color(QPalette::PlaceholderText);
    const QColor bright = palette().color(QPalette::Text);

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == currentLine ? bright : dim);
            painter.drawText(0, static_cast<int>(top),
                             gutter_->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++number;
    }
}

void CodeEditor::indentSelection(bool outdent) {
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();

    cursor.beginEditBlock();
    cursor.setPosition(start);
    const int firstBlock = cursor.blockNumber();
    cursor.setPosition(end);
    const int lastBlock = cursor.blockNumber();

    for (int i = firstBlock; i <= lastBlock; ++i) {
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, i);
        if (outdent) {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                                kIndentWidth);
            if (cursor.selectedText().trimmed().isEmpty())
                cursor.removeSelectedText();
        } else {
            cursor.insertText(QString(kIndentWidth, QLatin1Char(' ')));
        }
    }
    cursor.endEditBlock();
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    QTextCursor cursor = textCursor();

    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const bool outdent = event->key() == Qt::Key_Backtab ||
                             event->modifiers().testFlag(Qt::ShiftModifier);
        if (cursor.hasSelection()) {
            indentSelection(outdent);
        } else if (outdent) {
            indentSelection(true);
        } else {
            const int column = cursor.positionInBlock();
            cursor.insertText(
                QString(kIndentWidth - column % kIndentWidth, QLatin1Char(' ')));
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString line = cursor.block().text();
        QString indent = leadingWhitespace(line);
        if (line.trimmed().endsWith(QLatin1Char(':')))
            indent += QString(kIndentWidth, QLatin1Char(' '));

        QPlainTextEdit::keyPressEvent(event);
        textCursor().insertText(indent);
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

PythonEditor::PythonEditor(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Python source"));
    resize(820, 620);

    editor_ = new CodeEditor(this);
    highlighter_ = new PythonHighlighter(editor_->document());

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setFont(theme::monospaceFont(9));
    status_->setMinimumHeight(20);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* check = buttons->addButton(tr("Check"),
                                            QDialogButtonBox::ActionRole);
    check->setToolTip(tr("Compile the code without running the simulation"));

    connect(check, &QPushButton::clicked, this, [this] { runCheck(false); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(editor_, 1);
    layout->addWidget(status_);
    layout->addWidget(buttons);
}

void PythonEditor::setSource(const QString& source) {
    editor_->setPlainText(source);
    status_->clear();
}

QString PythonEditor::source() const { return editor_->toPlainText(); }

void PythonEditor::setSubject(const QString& subject) {
    setWindowTitle(subject.isEmpty() ? tr("Python source")
                                     : tr("%1 — Python source").arg(subject));
}

void PythonEditor::setValidator(
    std::function<QString(const QString&)> validator) {
    validator_ = std::move(validator);
}

void PythonEditor::runCheck(bool quiet) {
    if (!validator_) return;

    const QString problem = validator_(editor_->toPlainText());
    if (problem.isEmpty()) {
        status_->setStyleSheet(
            QStringLiteral("color: %1;").arg(theme::palette().wirePending.name()));
        status_->setText(quiet ? QString() : tr("The code compiles."));
    } else {
        status_->setStyleSheet(
            QStringLiteral("color: %1;").arg(theme::palette().error.name()));
        status_->setText(problem);
    }
}

}
