#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

namespace simupy {

class PythonHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit PythonHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    bool matchMultilineString(const QString& text,
                              const QRegularExpression& delimiter, int state);

    QVector<Rule> rules_;
    QTextCharFormat stringFormat_;
    QRegularExpression tripleSingle_;
    QRegularExpression tripleDouble_;
};

}
