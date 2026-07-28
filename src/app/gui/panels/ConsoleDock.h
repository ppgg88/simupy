#pragma once

#include <QWidget>

class QPlainTextEdit;

namespace simupy {

class ConsoleDock : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleDock(QWidget* parent = nullptr);

public slots:
    void appendOutput(const QString& text, bool isError);

    void appendMessage(const QString& message);
    void appendError(const QString& message);

    void clearConsole();

private:
    void writeLine(const QString& line, const QColor& color);

    QPlainTextEdit* view_;
    QString pending_;
    bool pendingIsError_ = false;
};

}  // namespace simupy
