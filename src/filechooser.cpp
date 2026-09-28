#include "filechooser.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QStandardPaths>
#include <QUuid>

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");

// The portal's filter wire format: a(sa(us)) — a name, then (kind, pattern)
// rules where kind 0 is a glob.
struct FilterRule { uint kind = 0; QString pattern; };
struct Filter { QString name; QList<FilterRule> rules; };

QDBusArgument &operator<<(QDBusArgument &arg, const FilterRule &rule) {
    arg.beginStructure();
    arg << rule.kind << rule.pattern;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, FilterRule &rule) {
    arg.beginStructure();
    arg >> rule.kind >> rule.pattern;
    arg.endStructure();
    return arg;
}
QDBusArgument &operator<<(QDBusArgument &arg, const Filter &filter) {
    arg.beginStructure();
    arg << filter.name << filter.rules;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, Filter &filter) {
    arg.beginStructure();
    arg >> filter.name >> filter.rules;
    arg.endStructure();
    return arg;
}

} // namespace

Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(Filter)

FileChooser::FileChooser(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();
}

void FileChooser::saveFile(const QString &title, const QString &suggestedName,
                           const QString &filterName, const QStringList &patterns) {
    QVariantMap options;
    options.insert(QStringLiteral("current_name"), suggestedName);
    // current_folder is a NUL-terminated byte string.
    QByteArray folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toUtf8();
    folder.append('\0');
    options.insert(QStringLiteral("current_folder"), folder);
    if (!patterns.isEmpty()) {
        Filter filter{filterName, {}};
        for (const QString &p : patterns)
            filter.rules.append({0, p});
        options.insert(QStringLiteral("filters"), QVariant::fromValue(QList<Filter>{filter}));
    }
    if (!request(QStringLiteral("SaveFile"), title, options))
        emit failed();
}

bool FileChooser::request(const QString &method, const QString &title, QVariantMap options) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    disconnectPending();

    // Subscribe before calling: the portal may answer before the call returns.
    const QString token = QStringLiteral("omascan_")
        + QUuid::createUuid().toString(QUuid::Id128).left(12);
    QString sender = bus.baseService().mid(1);
    sender.replace(u'.', u'_');
    m_pending = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kService, m_pending, kRequest, QStringLiteral("Response"),
                this, SLOT(handleResponse(uint,QVariantMap)));

    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.FileChooser"), method);
    call << QString() << title << options;

    const QDBusReply<QDBusObjectPath> reply = bus.call(call, QDBus::Block, 5000);
    if (!reply.isValid()) {
        disconnectPending();
        return false;
    }
    // Older portals ignore the token and pick their own path.
    if (reply.value().path() != m_pending) {
        disconnectPending();
        m_pending = reply.value().path();
        bus.connect(kService, m_pending, kRequest, QStringLiteral("Response"),
                    this, SLOT(handleResponse(uint,QVariantMap)));
    }
    return true;
}

void FileChooser::disconnectPending() {
    if (m_pending.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(kService, m_pending, kRequest, QStringLiteral("Response"),
                                             this, SLOT(handleResponse(uint,QVariantMap)));
    m_pending.clear();
}

void FileChooser::handleResponse(uint response, const QVariantMap &results) {
    disconnectPending();
    if (response != 0) {
        emit canceled();
        return;
    }
    QList<QUrl> urls;
    for (const QString &uri : results.value(QStringLiteral("uris")).toStringList())
        urls.append(QUrl(uri));
    if (urls.isEmpty())
        emit canceled();
    else
        emit selected(urls);
}
