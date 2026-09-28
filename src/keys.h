#pragma once

#include <QObject>
#include <QVariant>

// Shortcut labels for menus. An Action's `shortcut` is either a string
// ("Ctrl+E") or a StandardKey, which QML only knows as a number; this turns
// both into what the key caps say: "Ctrl + E", "Ctrl + =".
class Keys : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE static QString text(const QVariant &shortcut);
};
