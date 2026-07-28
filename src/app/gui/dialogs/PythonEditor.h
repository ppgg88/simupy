#pragma once

#include <QDialog>
#include <QPlainTextEdit>

#include <functional>

class QLabel;

namespace simupy {

class PythonHighlighter;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void paintLineNumbers(QPaintEvent* event);
    int lineNumberAreaWidth() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void indentSelection(bool outdent);

    QWidget* gutter_;
};

class PythonEditor : public QDialog {
    Q_OBJECT

public:
    explicit PythonEditor(QWidget* parent = nullptr);

    void setSource(const QString& source);
    QString source() const;

    void setSubject(const QString& subject);

    void setValidator(std::function<QString(const QString&)> validator);

private:
    void runCheck(bool quiet);

    CodeEditor* editor_;
    PythonHighlighter* highlighter_;
    QLabel* status_;
    std::function<QString(const QString&)> validator_;
};

}  // namespace simupy
