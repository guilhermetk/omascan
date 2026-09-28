// OmaScan — document scanning for Omarchy.

#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "exporter.h"
#include "filechooser.h"
#include "keys.h"
#include "pageimageprovider.h"
#include "pages.h"
#include "scanner.h"
#include "theme.h"

namespace {

void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Photograph the interface, one PNG per state, for design review off screen:
//   QT_QPA_PLATFORM=offscreen OMASCAN_SCANIMAGE=bin/fake-scanimage omascan --ui-shot dir
// With a scanner (real or fake) it also scans, to capture that on the way.
int captureInterface(QQmlApplicationEngine &engine, Scanner &scanner, const QString &outDir) {
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window)
        return 1;
    QDir().mkpath(outDir);
    window->resize(1440, 900);
    const auto shoot = [&](const QString &state, const QString &name, int wait) {
        QMetaObject::invokeMethod(window, "shotState", Q_ARG(QVariant, state));
        settle(wait);
        window->grabWindow().save(outDir + u'/' + name + QStringLiteral(".png"));
    };

    for (int i = 0; i < 100 && !scanner.ready() && scanner.available(); ++i)
        settle(100);
    shoot(QStringLiteral("main"), QStringLiteral("start"), 900);
    if (scanner.ready()) {
        shoot(QStringLiteral("scan"), QStringLiteral("scanning"), 700);
        for (int i = 0; i < 300 && scanner.scanning(); ++i)
            settle(100);
    }
    shoot(QStringLiteral("main"), QStringLiteral("main"), 1200);
    for (const QString &state : {QStringLiteral("crop"), QStringLiteral("export"), QStringLiteral("shortcuts"),
                                 QStringLiteral("menu")})
        shoot(state, state, 900);
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    // The identity trio: setDesktopFileName becomes the Wayland app_id, which
    // is what Hyprland window rules and the launcher match on.
    app.setApplicationName(QStringLiteral("omascan"));
    app.setOrganizationDomain(QStringLiteral("omascan"));
    app.setApplicationDisplayName(QStringLiteral("OmaScan"));
    app.setDesktopFileName(QStringLiteral("omascan"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omascan")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OmaScan — document scanning for Omarchy."));
    parser.addHelpOption();
    QCommandLineOption shot(QStringLiteral("ui-shot"),
                            QStringLiteral("Save screenshots of the interface to <dir> and quit."),
                            QStringLiteral("dir"));
    parser.addOption(shot);
    parser.process(app);

    QImageReader::setAllocationLimit(PageRender::kImageLimitMiB);

    // One window per session folder: a second one would save over the first
    // one's pages, and could delete the pictures it thinks nobody uses.
    QDir().mkpath(PageModel::defaultDir());
    QLockFile lock(PageModel::defaultDir() + QStringLiteral("/.lock"));
    if (!lock.tryLock(0)) {
        const QString text = QStringLiteral("OmaScan is already open.");
        qWarning("%s", qPrintable(text));
        if (const QString notify = QStandardPaths::findExecutable(QStringLiteral("notify-send")); !notify.isEmpty())
            QProcess::execute(notify, {QStringLiteral("--app-name=OmaScan"), text});
        return 1;
    }

    QQuickStyle::setStyle(QStringLiteral("OmaScanStyle"));

    OmarchyTheme theme;
    PageModel pages;
    Scanner scanner(pages.sessionDir() + QStringLiteral("/incoming"));
    Exporter exporter(&pages);
    FileChooser chooser;
    Keys keys;
    QObject::connect(&scanner, &Scanner::pageScanned, &pages, &PageModel::addScan);


    qmlRegisterSingletonInstance("Omascan", 1, 0, "OmarchyTheme", &theme);
    qmlRegisterSingletonInstance("Omascan", 1, 0, "Pages", &pages);
    qmlRegisterSingletonInstance("Omascan", 1, 0, "Scanner", &scanner);
    qmlRegisterSingletonInstance("Omascan", 1, 0, "Exporter", &exporter);
    qmlRegisterSingletonInstance("Omascan", 1, 0, "FileChooser", &chooser);
    qmlRegisterSingletonInstance("Omascan", 1, 0, "Keys", &keys);
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")), "Omascan", 1, 0, "Theme");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/ui/icons/Icons.qml")), "Omascan", 1, 0, "Icons");
    qmlRegisterType(QUrl(QStringLiteral("qrc:/ui/Icon.qml")), "Omascan", 1, 0, "Icon");

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("page"), new PageImageProvider(&pages));
    engine.rootContext()->setContextProperty(QStringLiteral("shotMode"), parser.isSet(shot));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(shot))
        return captureInterface(engine, scanner, parser.value(shot));
    return app.exec();
}
