#pragma once

#include <QString>

class QLineEdit;

namespace simupy {

/// Numbers are stored, saved and parsed with a decimal point, so the fields
/// that carry them are held to the same convention rather than the system
/// locale's — a French or German locale would otherwise let the user type a
/// decimal comma that nothing downstream can read back.
///
/// Typing one is still allowed: entry accepts either separator and the field
/// is redrawn with a point once the value is committed.
namespace numbers {

/// Locale-independent, and forgiving of a decimal comma.
double parse(const QString& text, bool* ok);

/// Always writes a decimal point, whatever the locale.
QString format(double value, int precision = 10);

/// Installs a validator that accepts both separators within [bottom, top].
void constrain(QLineEdit* field, double bottom, double top);

void constrain(QLineEdit* field);

}

}
