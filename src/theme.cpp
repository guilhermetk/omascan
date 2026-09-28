#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {
QString unquoted(const QString &value) {
    if (value.size() >= 2
            && ((value.front() == u'"' && value.back() == u'"')
                || (value.front() == u'\'' && value.back() == u'\'')))
        return value.mid(1, value.size() - 2);
    return value;
}
}

OmarchyTheme::OmarchyTheme(QObject *parent) : QObject(parent) {
    // A theme switch replaces the `current` directory's contents, so the watch
    // has to be re-armed on every change, not just at startup.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &OmarchyTheme::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &OmarchyTheme::reload);
    reload();
}

QString OmarchyTheme::currentDir() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

QString OmarchyTheme::colorsPath() {
    return currentDir() + QStringLiteral("/theme/colors.toml");
}

QHash<QString, QString> OmarchyTheme::colorsFromFile(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return colors;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const qsizetype equals = line.indexOf(u'=');
        if (equals < 0)
            continue;
        colors.insert(line.left(equals).trimmed(), unquoted(line.mid(equals + 1).trimmed()));
    }
    return colors;
}

void OmarchyTheme::reload() {
    watch();

    const QString wasAccent = m_accent;
    const bool wasFollowed = m_accentFollowed;

    const QString accent = colorsFromFile(colorsPath()).value(QStringLiteral("accent"));
    m_accentFollowed = !accent.isEmpty() && QColor::fromString(accent).isValid();
    m_accent = m_accentFollowed ? accent : QString();

    if (m_accent != wasAccent || m_accentFollowed != wasFollowed)
        emit colorsChanged();
}

void OmarchyTheme::watch() {
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    const QString themeDir = currentDir() + QStringLiteral("/theme");
    if (QDir(currentDir()).exists())
        m_watcher.addPath(currentDir());
    if (QDir(themeDir).exists())
        m_watcher.addPath(themeDir);
    if (QFileInfo::exists(colorsPath()))
        m_watcher.addPath(colorsPath());
}
