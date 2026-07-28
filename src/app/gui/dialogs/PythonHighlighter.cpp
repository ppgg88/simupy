#include "PythonHighlighter.h"

#include <QApplication>
#include <QPalette>

namespace simupy {
namespace {

struct Colors {
    QColor keyword, builtin, string, comment, number, decorator, definition,
        self;
};

Colors colorsFor(bool dark) {
    Colors c;
    if (dark) {
        c.keyword = QColor(0xc5, 0x92, 0xe8);
        c.builtin = QColor(0x6f, 0xb3, 0xd2);
        c.string = QColor(0x8f, 0xc9, 0x7a);
        c.comment = QColor(0x7a, 0x85, 0x94);
        c.number = QColor(0xe8, 0xa3, 0x3d);
        c.decorator = QColor(0xe0, 0xc3, 0x4a);
        c.definition = QColor(0x63, 0xb6, 0xf5);
        c.self = QColor(0xd4, 0x7c, 0x9a);
    } else {
        c.keyword = QColor(0x7c, 0x28, 0xb8);
        c.builtin = QColor(0x0b, 0x63, 0x8a);
        c.string = QColor(0x1a, 0x7f, 0x2e);
        c.comment = QColor(0x6a, 0x73, 0x7d);
        c.number = QColor(0xa8, 0x53, 0x00);
        c.decorator = QColor(0x8a, 0x6d, 0x00);
        c.definition = QColor(0x11, 0x55, 0xcc);
        c.self = QColor(0xb0, 0x2a, 0x63);
    }
    return c;
}

const char* kKeywords[] = {
    "and",   "as",       "assert", "async",  "await",  "break", "class",
    "continue", "def",   "del",    "elif",   "else",   "except", "finally",
    "for",   "from",     "global", "if",     "import", "in",    "is",
    "lambda", "nonlocal", "not",   "or",     "pass",   "raise", "return",
    "try",   "while",    "with",   "yield",  "True",   "False", "None"};

const char* kBuiltins[] = {
    "abs",   "all",   "any",    "bool",   "dict",  "enumerate", "float",
    "int",   "len",   "list",   "max",    "min",   "print",     "range",
    "round", "sum",   "str",    "tuple",  "zip",   "isinstance", "getattr",
    "setattr", "super", "np",   "numpy"};

}  // namespace

PythonHighlighter::PythonHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
    const bool dark =
        QApplication::palette().color(QPalette::Base).lightness() < 128;
    const Colors colors = colorsFor(dark);

    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(colors.keyword);
    keywordFormat.setFontWeight(QFont::DemiBold);
    for (const char* keyword : kKeywords)
        rules_.append({QRegularExpression(QStringLiteral("\\b%1\\b")
                                              .arg(QLatin1String(keyword))),
                       keywordFormat});

    QTextCharFormat builtinFormat;
    builtinFormat.setForeground(colors.builtin);
    for (const char* builtin : kBuiltins)
        rules_.append({QRegularExpression(QStringLiteral("\\b%1\\b")
                                              .arg(QLatin1String(builtin))),
                       builtinFormat});

    QTextCharFormat selfFormat;
    selfFormat.setForeground(colors.self);
    selfFormat.setFontItalic(true);
    rules_.append({QRegularExpression(QStringLiteral("\\bself\\b")), selfFormat});

    QTextCharFormat definitionFormat;
    definitionFormat.setForeground(colors.definition);
    definitionFormat.setFontWeight(QFont::Bold);
    rules_.append({QRegularExpression(
                       QStringLiteral("(?<=\\bdef\\s)\\s*\\w+")),
                   definitionFormat});
    rules_.append({QRegularExpression(
                       QStringLiteral("(?<=\\bclass\\s)\\s*\\w+")),
                   definitionFormat});

    QTextCharFormat numberFormat;
    numberFormat.setForeground(colors.number);
    rules_.append({QRegularExpression(QStringLiteral(
                       "\\b[0-9]+\\.?[0-9]*([eE][-+]?[0-9]+)?\\b")),
                   numberFormat});

    QTextCharFormat decoratorFormat;
    decoratorFormat.setForeground(colors.decorator);
    rules_.append({QRegularExpression(QStringLiteral("@\\w+")), decoratorFormat});

    stringFormat_.setForeground(colors.string);
    rules_.append({QRegularExpression(QStringLiteral("'[^'\\\\]*(\\\\.[^'\\\\]*)*'")),
                   stringFormat_});
    rules_.append({QRegularExpression(QStringLiteral("\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"")),
                   stringFormat_});

    QTextCharFormat commentFormat;
    commentFormat.setForeground(colors.comment);
    commentFormat.setFontItalic(true);
    rules_.append({QRegularExpression(QStringLiteral("#[^\n]*")), commentFormat});

    tripleSingle_ = QRegularExpression(QStringLiteral("'''"));
    tripleDouble_ = QRegularExpression(QStringLiteral("\"\"\""));
}

void PythonHighlighter::highlightBlock(const QString& text) {
    for (const Rule& rule : std::as_const(rules_)) {
        QRegularExpressionMatchIterator matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);
    if (!matchMultilineString(text, tripleSingle_, 1))
        matchMultilineString(text, tripleDouble_, 2);
}

bool PythonHighlighter::matchMultilineString(const QString& text,
                                             const QRegularExpression& delimiter,
                                             int state) {
    int start = 0;
    int offset = 0;

    if (previousBlockState() != state) {
        const QRegularExpressionMatch match = delimiter.match(text);
        if (!match.hasMatch()) return false;
        start = match.capturedStart();
        offset = match.capturedLength();
    }

    while (start >= 0) {
        const QRegularExpressionMatch match = delimiter.match(text, start + offset);
        int length = 0;
        if (match.hasMatch()) {
            length = match.capturedEnd() - start;
            setCurrentBlockState(0);
        } else {
            setCurrentBlockState(state);
            length = text.length() - start;
        }
        setFormat(start, length, stringFormat_);

        if (!match.hasMatch()) return true;
        const QRegularExpressionMatch next = delimiter.match(text, start + length);
        start = next.hasMatch() ? next.capturedStart() : -1;
        offset = next.capturedLength();
    }
    return false;
}

}  // namespace simupy
