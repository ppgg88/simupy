#include "NumberInput.h"

#include <QDoubleValidator>
#include <QLineEdit>
#include <QLocale>

#include <cmath>

namespace simupy {
namespace numbers {
namespace {

QString withPoint(const QString& text) {
    QString normalised = text.trimmed();
    normalised.replace(QLatin1Char(','), QLatin1Char('.'));
    return normalised;
}

/// Validates against the C locale, so a decimal point is always welcome, and
/// lets a comma through as the same thing. The comma stays on screen while the
/// user is typing; committing the field rewrites it.
class NumberValidator : public QDoubleValidator {
public:
    explicit NumberValidator(QObject* parent) : QDoubleValidator(parent) {
        setNotation(QDoubleValidator::ScientificNotation);
        setLocale(QLocale::c());
    }

    QValidator::State validate(QString& input, int& pos) const override {
        // A copy: rewriting input here would move the cursor under the user.
        QString normalised = input;
        normalised.replace(QLatin1Char(','), QLatin1Char('.'));
        return QDoubleValidator::validate(normalised, pos);
    }
};

}

double parse(const QString& text, bool* ok) {
    return withPoint(text).toDouble(ok);
}

QString format(double value, int precision) {
    return QString::number(value, 'g', precision);
}

void constrain(QLineEdit* field, double bottom, double top) {
    auto* validator = new NumberValidator(field);
    if (std::isfinite(bottom)) validator->setBottom(bottom);
    if (std::isfinite(top)) validator->setTop(top);
    field->setValidator(validator);
}

void constrain(QLineEdit* field) {
    field->setValidator(new NumberValidator(field));
}

}
}
