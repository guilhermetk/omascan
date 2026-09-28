#include "keys.h"

#include <QKeySequence>

QString Keys::text(const QVariant &shortcut) {
    QKeySequence sequence;
    if (shortcut.metaType().id() == QMetaType::QString)
        sequence = QKeySequence::fromString(shortcut.toString(), QKeySequence::PortableText);
    else if (shortcut.metaType().id() == QMetaType::QKeySequence)
        sequence = shortcut.value<QKeySequence>();
    else if (shortcut.canConvert<int>())
        // A standard key may have several bindings; the first is the usual one.
        sequence = QKeySequence::keyBindings(QKeySequence::StandardKey(shortcut.toInt())).value(0);
    if (sequence.isEmpty())
        return {};

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    QStringList parts;
    if (modifiers & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
    if (modifiers & Qt::AltModifier) parts << QStringLiteral("Alt");
    if (modifiers & Qt::ShiftModifier) parts << QStringLiteral("Shift");
    if (modifiers & Qt::MetaModifier) parts << QStringLiteral("Super");

    switch (combination.key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: parts << QStringLiteral("Enter"); break;
    case Qt::Key_Up: parts << QStringLiteral("↑"); break;
    case Qt::Key_Down: parts << QStringLiteral("↓"); break;
    default: parts << QKeySequence(combination.key()).toString(QKeySequence::NativeText);
    }
    return parts.join(QStringLiteral(" + "));
}
