#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>

// The part of the look that belongs to the desktop: the Omarchy accent. The
// rest of the palette is fixed in Theme.qml, because a scan is judged by eye
// and the surround it sits in must not change hue under it.
//
// Reads ~/.local/state/omarchy/current/theme/colors.toml and re-reads it when
// the theme is switched. Without Omarchy, `accentFollowed` stays false and the
// interface uses its own accent.
class OmarchyTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY colorsChanged)
    Q_PROPERTY(bool accentFollowed READ accentFollowed NOTIFY colorsChanged)

public:
    explicit OmarchyTheme(QObject *parent = nullptr);

    QString accent() const { return m_accent; }
    bool accentFollowed() const { return m_accentFollowed; }

    // key = value pairs from a colors.toml; empty when there is none.
    static QHash<QString, QString> colorsFromFile(const QString &path);

public slots:
    void reload();

signals:
    void colorsChanged();

private:
    static QString currentDir();
    static QString colorsPath();
    void watch();

    QString m_accent;
    bool m_accentFollowed = false;
    QFileSystemWatcher m_watcher;
};
