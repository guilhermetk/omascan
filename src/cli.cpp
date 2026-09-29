#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <csignal>
#include <functional>

#include <fcntl.h>
#include <unistd.h>

#include "exporter.h"
#include "pages.h"
#include "scanner.h"

namespace Cli {

namespace {

const QStringList kCommands{QStringLiteral("scan"), QStringLiteral("devices"), QStringLiteral("setup"),
                            QStringLiteral("config"), QStringLiteral("help")};
const QStringList kFormats{QStringLiteral("pdf"), QStringLiteral("png")};
const QStringList kFilters{QStringLiteral("original"), QStringLiteral("enhanced"),
                           QStringLiteral("greyscale"), QStringLiteral("bw")}; // Page::Filter order
const QStringList kPapers{QStringLiteral("A4"), QStringLiteral("Letter"), QStringLiteral("Legal"),
                          QStringLiteral("A5"), QStringLiteral("Full area")};

// What `omascan config` reads and writes. The scanner settings are the
// window's own, so changing them in either place changes both.
struct Key { const char *name; const char *setting; const char *help; };
constexpr Key kKeys[] = {
    {"device", "scan/device", "the scanner, as `omascan devices` lists it"},
    {"format", "cli/format", "pdf (to Documents) or png (to Pictures); pdf when not set"},
    {"resolution", "scan/resolution", "dots per inch; 300 when not set"},
    {"mode", "scan/mode", "Color, Gray, Lineart…, as the scanner names them"},
    {"source", "scan/source", "Flatbed, ADF…, as the scanner names them"},
    {"paper", "scan/paper", "A4, Letter, Legal, A5 or Full area"},
    {"filter", "cli/filter", "original, enhanced, greyscale or bw; enhanced when not set"},
};

const Key *findKey(const QString &name) {
    for (const Key &key : kKeys)
        if (name == QLatin1String(key.name))
            return &key;
    return nullptr;
}

QString setting(const char *key) {
    return QSettings().value(QLatin1String(key)).toString();
}

QString usage() {
    return QStringLiteral(
        "Usage: omascan [command]\n"
        "\n"
        "With no command, OmaScan opens its window. Commands:\n"
        "  scan      Scan to a file in Documents (PDF) or Pictures (PNG)\n"
        "  devices   List the scanners SANE can see\n"
        "  setup     Pick the scanner and file type `omascan scan` uses\n"
        "  config    Show or change those settings\n"
        "\n"
        "`omascan <command> --help` says more about each.\n");
}

// Writes the outcome as one JSON object, or as text for a person.
class Reporter {
public:
    Reporter(QTextStream &out, QTextStream &err, bool json) : m_out(out), m_err(err), m_json(json) {}

    bool json() const { return m_json; }

    int fail(Exit code, const QString &kind, const QString &message) {
        if (m_json)
            print(QJsonObject{{"ok", false}, {"error", kind}, {"message", message}});
        else
            m_err << "omascan: " << message << Qt::endl;
        return code;
    }

    void print(const QJsonObject &object) {
        m_out << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
    }

    // Progress for a person watching; nothing when the output is JSON.
    void status(const QString &text) {
        if (m_json || text.isEmpty() || text == m_last)
            return;
        m_last = text;
        if (m_live)
            m_err << '\r' << QString(m_liveWidth, u' ') << '\r';
        m_live = false;
        m_err << text << Qt::endl;
    }

    void progress(const QString &text, qreal fraction) {
        if (m_json || !::isatty(STDERR_FILENO) || fraction < 0)
            return;
        const QString line = QStringLiteral("%1 %2%").arg(text).arg(int(fraction * 100));
        m_err << '\r' << line << Qt::flush;
        m_live = true;
        m_liveWidth = int(line.size());
    }

private:
    QTextStream &m_out;
    QTextStream &m_err;
    bool m_json;
    QString m_last;
    bool m_live = false;
    int m_liveWidth = 0;
};

// Ctrl+C, or the bar plugin giving up, cancels the scan instead of leaving
// scanimage holding the scanner and a half page in the temporary folder.
class Interrupts {
public:
    Interrupts() {
        if (::pipe2(s_pipe, O_CLOEXEC | O_NONBLOCK) != 0)
            return;
        struct sigaction action {};
        action.sa_handler = [](int) { const char c = 1; (void)!::write(s_pipe[1], &c, 1); };
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        ::sigaction(SIGINT, &action, &m_oldInt);
        ::sigaction(SIGTERM, &action, &m_oldTerm);
        m_notifier = new QSocketNotifier(s_pipe[0], QSocketNotifier::Read);
        QObject::connect(m_notifier, &QSocketNotifier::activated, m_notifier, [this] {
            char c;
            while (::read(s_pipe[0], &c, 1) > 0) {}
            m_hit = true;
        });
    }
    ~Interrupts() {
        if (!m_notifier)
            return;
        ::sigaction(SIGINT, &m_oldInt, nullptr);
        ::sigaction(SIGTERM, &m_oldTerm, nullptr);
        delete m_notifier;
        ::close(s_pipe[0]);
        ::close(s_pipe[1]);
    }
    Interrupts(const Interrupts &) = delete;
    Interrupts &operator=(const Interrupts &) = delete;

    bool hit() const { return m_hit; }
    QSocketNotifier *notifier() const { return m_notifier; }

private:
    static inline int s_pipe[2] = {-1, -1};
    struct sigaction m_oldInt {};
    struct sigaction m_oldTerm {};
    QSocketNotifier *m_notifier = nullptr;
    bool m_hit = false;
};

// Runs the event loop until `done` holds, looking again whenever `sender`
// sends `signal`, or until an interrupt arrives.
template <typename Sender, typename Signal>
void waitUntil(const Sender *sender, Signal signal, const std::function<bool()> &done,
               const Interrupts *interrupts = nullptr) {
    if (done())
        return;
    QEventLoop loop;
    QObject::connect(sender, signal, &loop, [&] { if (done()) loop.quit(); });
    if (interrupts && interrupts->notifier())
        QObject::connect(interrupts->notifier(), &QSocketNotifier::activated, &loop,
                         &QEventLoop::quit, Qt::QueuedConnection);
    loop.exec();
}

// Qt's help names the program; ours names the command too.
QString help(const QCommandLineParser &parser, const QString &command) {
    return QStringLiteral("Usage: omascan %1 [options]\n").arg(command) + parser.helpText().section(u'\n', 1);
}

QVariantList findDevices(Scanner &scanner) {
    scanner.refresh();
    waitUntil(&scanner, &Scanner::discoveringChanged, [&] { return !scanner.discovering(); });
    return scanner.devices();
}

QString deviceLine(const QVariantMap &device) {
    const QString id = device.value(QStringLiteral("id")).toString();
    const QString name = device.value(QStringLiteral("name")).toString();
    return name == id ? id : QStringLiteral("%1 (%2)").arg(name, id);
}

// ── scan ─────────────────────────────────────────────────────────────────────

int scan(const QStringList &arguments, QTextStream &out, QTextStream &err) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Scans one page (or every page in the feeder) with the saved settings and\n"
        "writes it out: a PDF to Documents, or PNG pictures to Pictures. Prints the\n"
        "files written, one per line."));
    parser.addHelpOption();
    const QCommandLineOption output({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Write to <path>: a file, or a folder to name it in."), QStringLiteral("path"));
    const QCommandLineOption format(QStringLiteral("format"),
        QStringLiteral("pdf or png, instead of the saved one."), QStringLiteral("format"));
    const QCommandLineOption device(QStringLiteral("device"),
        QStringLiteral("Scan with <id> instead of the saved scanner."), QStringLiteral("id"));
    const QCommandLineOption resolution(QStringLiteral("resolution"),
        QStringLiteral("Dots per inch, for this scan only."), QStringLiteral("dpi"));
    const QCommandLineOption mode(QStringLiteral("mode"),
        QStringLiteral("Colour mode, for this scan only."), QStringLiteral("mode"));
    const QCommandLineOption source(QStringLiteral("source"),
        QStringLiteral("Flatbed, ADF…, for this scan only."), QStringLiteral("source"));
    const QCommandLineOption paper(QStringLiteral("paper"),
        QStringLiteral("A4, Letter, Legal, A5 or Full area, for this scan only."), QStringLiteral("size"));
    const QCommandLineOption filter(QStringLiteral("filter"),
        QStringLiteral("original, enhanced, greyscale or bw."), QStringLiteral("filter"));
    const QCommandLineOption json(QStringLiteral("json"),
        QStringLiteral("Print the outcome as one line of JSON."));
    parser.addOptions({output, format, device, resolution, mode, source, paper, filter, json});
    if (!parser.parse(arguments)) {
        err << "omascan scan: " << parser.errorText() << Qt::endl;
        return Failed;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        out << help(parser, arguments.first());
        return Ok;
    }
    Reporter report(out, err, parser.isSet(json));
    if (!parser.positionalArguments().isEmpty())
        return report.fail(Failed, QStringLiteral("usage"),
                           QStringLiteral("scan takes no arguments; see `omascan scan --help`"));

    // What to write, and where.
    const QString outputPath = parser.value(output);
    const bool intoFolder = !outputPath.isEmpty() && QFileInfo(outputPath).isDir();
    QString kind = parser.value(format).toLower();
    if (kind.isEmpty() && !outputPath.isEmpty() && !intoFolder)
        kind = QFileInfo(outputPath).suffix().toLower();
    if (kind.isEmpty())
        kind = setting("cli/format").toLower();
    if (kind.isEmpty())
        kind = QStringLiteral("pdf");
    if (!kFormats.contains(kind))
        return report.fail(Failed, QStringLiteral("usage"),
                           QStringLiteral("cannot write \"%1\" files: use pdf or png").arg(kind));
    if (!outputPath.isEmpty() && !intoFolder && QFileInfo(outputPath).suffix().toLower() != kind)
        return report.fail(Failed, QStringLiteral("usage"),
                           QStringLiteral("%1 does not end in .%2").arg(outputPath, kind));
    const QString filterName = parser.isSet(filter) ? parser.value(filter).toLower()
                             : setting("cli/filter").isEmpty() ? QStringLiteral("enhanced")
                             : setting("cli/filter");
    if (!kFilters.contains(filterName))
        return report.fail(Failed, QStringLiteral("usage"),
                           QStringLiteral("unknown filter \"%1\": use %2")
                               .arg(filterName, kFilters.join(QStringLiteral(", "))));
    bool dpiOk = true;
    const int dpi = parser.isSet(resolution) ? parser.value(resolution).toInt(&dpiOk) : 0;
    if (!dpiOk || dpi < 0 || (parser.isSet(resolution) && dpi == 0))
        return report.fail(Failed, QStringLiteral("usage"),
                           QStringLiteral("the resolution is a number of dots per inch"));

    const QString deviceId = parser.isSet(device) ? parser.value(device) : setting("scan/device");
    if (deviceId.isEmpty())
        return report.fail(NotSetUp, QStringLiteral("setup"),
                           QStringLiteral("no scanner chosen yet: run `omascan setup`"));

    // Two scans at once would only fight over the scanner.
    QLockFile lock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                   + QStringLiteral("/omascan-scan.lock"));
    if (!lock.tryLock(0))
        return report.fail(Busy, QStringLiteral("busy"), QStringLiteral("another scan is running"));

    // Pages land here, private to this user, and go when the command ends.
    QTemporaryDir incoming;
    if (!incoming.isValid())
        return report.fail(Failed, QStringLiteral("write"),
                           QStringLiteral("could not make a temporary folder for the scan"));
    // The scanner clears its folder once a scan ends, so pages move out of it
    // as they arrive, as they do into the window's session.
    const QString kept = incoming.filePath(QStringLiteral("pages"));
    Scanner scanner(incoming.filePath(QStringLiteral("incoming")), false);
    if (!scanner.available())
        return report.fail(ScanFailed, QStringLiteral("scan"),
                           QStringLiteral("SANE is not installed: scanimage was not found"));
    if (!QDir().mkpath(kept))
        return report.fail(Failed, QStringLiteral("write"),
                           QStringLiteral("could not make a temporary folder for the scan"));
    scanner.setRemember(false);
    QObject::connect(&scanner, &Scanner::statusChanged, [&] { report.status(scanner.status()); });
    QObject::connect(&scanner, &Scanner::progressChanged, [&] {
        report.progress(scanner.status(), scanner.progress());
    });

    Interrupts interrupts;
    scanner.useDevice(deviceId);
    waitUntil(&scanner, &Scanner::optionsChanged, [&] { return !scanner.loadingOptions(); }, &interrupts);
    if (interrupts.hit())
        return report.fail(Interrupted, QStringLiteral("cancelled"), QStringLiteral("cancelled"));
    if (scanner.modes().isEmpty() && scanner.sources().isEmpty() && scanner.resolutions().isEmpty())
        return report.fail(ScanFailed, QStringLiteral("scan"),
                           QStringLiteral("%1 did not answer: %2").arg(deviceId, scanner.status()));

    // One-time settings, checked against what this scanner offers.
    const auto choose = [&](const QCommandLineOption &option, const QStringList &offered,
                            const char *what, const std::function<void(const QString &)> &set) -> bool {
        if (!parser.isSet(option))
            return true;
        const QString wanted = parser.value(option);
        for (const QString &name : offered) {
            if (name.compare(wanted, Qt::CaseInsensitive) == 0) {
                set(name);
                return true;
            }
        }
        report.fail(Failed, QStringLiteral("usage"), QStringLiteral("this scanner has no %1 \"%2\"; it offers %3")
                    .arg(QLatin1String(what), wanted, offered.join(QStringLiteral(", "))));
        return false;
    };
    if (!choose(mode, scanner.modes(), "mode", [&](const QString &v) { scanner.setMode(v); })
            || !choose(source, scanner.sources(), "source", [&](const QString &v) { scanner.setSource(v); })
            || !choose(paper, scanner.paperSizes(), "paper size", [&](const QString &v) { scanner.setPaperSize(v); }))
        return Failed;
    if (dpi > 0)
        scanner.setResolution(dpi);

    QList<QPair<QString, qreal>> scanned;
    QString failure;
    QObject::connect(&scanner, &Scanner::pageScanned, [&](const QString &path, qreal pageDpi) {
        const QString page = QStringLiteral("%1/%2.%3").arg(kept).arg(scanned.size() + 1)
                                 .arg(QFileInfo(path).suffix());
        if (QFile::rename(path, page))
            scanned.append({page, pageDpi});
        else
            failure = QStringLiteral("could not keep page %1 of the scan").arg(scanned.size() + 1);
    });
    QObject::connect(&scanner, &Scanner::failed, [&](const QString &message) { failure = message; });
    if (interrupts.notifier())
        QObject::connect(interrupts.notifier(), &QSocketNotifier::activated, &scanner, &Scanner::cancel,
                         Qt::QueuedConnection);
    scanner.scan();
    if (!scanner.scanning())
        return report.fail(ScanFailed, QStringLiteral("scan"), scanner.status());
    waitUntil(&scanner, &Scanner::scanningChanged, [&] { return !scanner.scanning(); });
    if (interrupts.hit())
        return report.fail(Interrupted, QStringLiteral("cancelled"), QStringLiteral("cancelled"));
    if (scanned.isEmpty())
        return report.fail(scanner.deviceBusy() ? Busy : ScanFailed,
                           scanner.deviceBusy() ? QStringLiteral("busy") : QStringLiteral("scan"),
                           failure.isEmpty() ? QStringLiteral("the scanner sent no pages") : failure);

    QList<Page> pages;
    for (const auto &[path, pageDpi] : std::as_const(scanned)) {
        Page page;
        if (const QString error = PageModel::readScan(path, pageDpi, &page); !error.isEmpty())
            return report.fail(ScanFailed, QStringLiteral("scan"), error);
        page.filter = int(kFilters.indexOf(filterName));
        pages.append(page);
    }

    // Pages that did arrive before a jam are still written, and the jam said.
    QString target = outputPath;
    if (target.isEmpty() || intoFolder) {
        const QString folder = intoFolder ? outputPath : Exporter::userFolder(
            kind == u"pdf" ? QStandardPaths::DocumentsLocation : QStandardPaths::PicturesLocation);
        if (!QDir().mkpath(folder))
            return report.fail(Failed, QStringLiteral("write"), QStringLiteral("could not make %1").arg(folder));
        target = folder + u'/' + Exporter::defaultStem() + u'.' + kind;
    }
    target = QFileInfo(target).absoluteFilePath();
    const QStringList names = Exporter::freeNames(target, kind == u"pdf" ? 1 : int(pages.size()),
                                                  !outputPath.isEmpty() && !intoFolder);

    // A PDF is written the way the export dialog last wrote one.
    QSettings settings;
    static const QStringList papers{QStringLiteral("match"), QStringLiteral("A4"),
                                    QStringLiteral("Letter"), QStringLiteral("Legal")};
    static const QStringList qualities{QStringLiteral("best"), QStringLiteral("balanced"), QStringLiteral("small")};
    Exporter exporter(nullptr);
    const QString language = settings.value(QStringLiteral("export/language")).toString();
    const QVariantMap options{
        {QStringLiteral("format"), kind},
        {QStringLiteral("paper"), papers.value(settings.value(QStringLiteral("export/paper"), 0).toInt(), papers.first())},
        {QStringLiteral("quality"), qualities.value(settings.value(QStringLiteral("export/quality"), 1).toInt(),
                                                    qualities.at(1))},
        {QStringLiteral("ocr"), settings.value(QStringLiteral("export/ocr"), false).toBool()},
        {QStringLiteral("language"), language.isEmpty() ? exporter.defaultOcrLanguage() : language},
    };
    report.status(QStringLiteral("Writing %1…").arg(QFileInfo(names.first()).fileName()));
    if (const QString error = exporter.write(pages, names, options); !error.isEmpty())
        return report.fail(Failed, QStringLiteral("write"), error);

    if (report.json()) {
        QJsonObject result{{"ok", failure.isEmpty()}, {"files", QJsonArray::fromStringList(names)},
                           {"pages", int(pages.size())}};
        if (!failure.isEmpty()) {
            result.insert("error", scanner.deviceBusy() ? QStringLiteral("busy") : QStringLiteral("scan"));
            result.insert("message", failure);
        }
        report.print(result);
    } else {
        for (const QString &name : names)
            out << name << Qt::endl;
        if (!failure.isEmpty())
            err << "omascan: " << failure << Qt::endl;
    }
    return failure.isEmpty() ? Ok : ScanFailed;
}

// ── devices ──────────────────────────────────────────────────────────────────

int devices(const QStringList &arguments, QTextStream &out, QTextStream &err) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Lists the scanners SANE can see, the saved one marked with *. Network\n"
        "scanners can take several seconds to answer."));
    parser.addHelpOption();
    const QCommandLineOption json(QStringLiteral("json"), QStringLiteral("Print the list as JSON."));
    parser.addOption(json);
    if (!parser.parse(arguments)) {
        err << "omascan devices: " << parser.errorText() << Qt::endl;
        return Failed;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        out << help(parser, arguments.first());
        return Ok;
    }
    Reporter report(out, err, parser.isSet(json));
    QTemporaryDir incoming;
    Scanner scanner(incoming.path(), false);
    if (!scanner.available())
        return report.fail(ScanFailed, QStringLiteral("scan"),
                           QStringLiteral("SANE is not installed: scanimage was not found"));
    scanner.setRemember(false);
    if (!report.json())
        err << "Looking for scanners…" << Qt::endl;
    const QVariantList found = findDevices(scanner);
    const QString current = setting("scan/device");

    if (report.json()) {
        QJsonArray list;
        for (const QVariant &d : found)
            list.append(QJsonObject::fromVariantMap(d.toMap()));
        report.print(QJsonObject{{"ok", true}, {"current", current}, {"devices", list}});
        return Ok;
    }
    if (found.isEmpty())
        err << "No scanners found. Check the scanner is on, and connected or on this network." << Qt::endl;
    for (const QVariant &d : found) {
        const QVariantMap device = d.toMap();
        const bool saved = device.value(QStringLiteral("id")).toString() == current;
        out << (saved ? "* " : "  ") << deviceLine(device) << Qt::endl;
    }
    return Ok;
}

// ── setup ────────────────────────────────────────────────────────────────────

// Asks until the answer is one of `count` numbers, or empty for the default.
// -1 when the input ends first.
int askNumber(QTextStream &in, QTextStream &out, const QString &question, int count, int fallback) {
    for (;;) {
        out << question << " [" << fallback << "]: " << Qt::flush;
        const QString answer = in.readLine();
        if (answer.isNull())
            return -1;
        if (answer.trimmed().isEmpty())
            return fallback;
        bool ok = false;
        const int n = answer.trimmed().toInt(&ok);
        if (ok && n >= 1 && n <= count)
            return n;
        out << "Type a number from 1 to " << count << "." << Qt::endl;
    }
}

int setup(const QStringList &arguments, QTextStream &in, QTextStream &out, QTextStream &err) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Picks the scanner and the kind of file `omascan scan` makes, and saves\n"
        "them. The window uses the same scanner."));
    parser.addHelpOption();
    if (!parser.parse(arguments)) {
        err << "omascan setup: " << parser.errorText() << Qt::endl;
        return Failed;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        out << help(parser, arguments.first());
        return Ok;
    }
    QTemporaryDir incoming;
    Scanner scanner(incoming.path(), false);
    if (!scanner.available()) {
        err << "omascan: SANE is not installed: scanimage was not found" << Qt::endl;
        return ScanFailed;
    }
    scanner.setRemember(false);
    out << "Looking for scanners… (network scanners can take a few seconds)" << Qt::endl;
    const QVariantList found = findDevices(scanner);
    if (found.isEmpty()) {
        err << "omascan: no scanners found. Check the scanner is on, and connected or on this network,"
               " then run `omascan setup` again." << Qt::endl;
        return ScanFailed;
    }

    const QString current = setting("scan/device");
    int fallback = 1;
    out << Qt::endl;
    for (int i = 0; i < found.size(); ++i) {
        const QVariantMap device = found.at(i).toMap();
        if (device.value(QStringLiteral("id")).toString() == current)
            fallback = i + 1;
        out << "  " << (i + 1) << ") " << deviceLine(device) << Qt::endl;
    }
    const int pick = askNumber(in, out, QStringLiteral("Scanner"), int(found.size()), fallback);
    if (pick < 0)
        return Failed;

    out << Qt::endl << "  1) PDF, saved to Documents" << Qt::endl << "  2) PNG, saved to Pictures" << Qt::endl;
    const int kind = askNumber(in, out, QStringLiteral("File type"), 2,
                               setting("cli/format") == u"png" ? 2 : 1);
    if (kind < 0)
        return Failed;

    const QVariantMap device = found.at(pick - 1).toMap();
    QSettings settings;
    settings.setValue(QStringLiteral("scan/device"), device.value(QStringLiteral("id")));
    settings.setValue(QStringLiteral("cli/format"), kFormats.at(kind - 1));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        err << "omascan: could not save to " << settings.fileName() << Qt::endl;
        return Failed;
    }
    out << Qt::endl << "Saved. `omascan scan` now scans with "
        << device.value(QStringLiteral("name")).toString() << " to "
        << (kind == 1 ? "a PDF in Documents." : "a PNG in Pictures.") << Qt::endl;
    return Ok;
}

// ── config ───────────────────────────────────────────────────────────────────

// Why `value` cannot be saved as `key`, or empty when it can.
QString invalidValue(const QString &key, const QString &value) {
    if (value.isEmpty())
        return {};
    if (key == u"format" && !kFormats.contains(value))
        return QStringLiteral("format is pdf or png");
    if (key == u"filter" && !kFilters.contains(value))
        return QStringLiteral("filter is one of %1").arg(kFilters.join(QStringLiteral(", ")));
    if (key == u"paper" && !kPapers.contains(value))
        return QStringLiteral("paper is one of %1").arg(kPapers.join(QStringLiteral(", ")));
    if (key == u"resolution" && value.toInt() <= 0)
        return QStringLiteral("resolution is a number of dots per inch");
    return {};
}

int config(const QStringList &arguments, QTextStream &out, QTextStream &err) {
    QCommandLineParser parser;
    QString keys;
    for (const Key &key : kKeys)
        keys += QStringLiteral("  %1  %2\n").arg(QString::fromLatin1(key.name).leftJustified(10), QLatin1String(key.help));
    parser.setApplicationDescription(QStringLiteral(
        "Shows the settings `omascan scan` uses, or gets or changes one:\n"
        "  omascan config\n  omascan config get KEY\n  omascan config set KEY VALUE\n"
        "An empty VALUE goes back to the default. Keys:\n") + keys);
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("action"), QStringLiteral("get or set"));
    if (!parser.parse(arguments)) {
        err << "omascan config: " << parser.errorText() << Qt::endl;
        return Failed;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        out << help(parser, arguments.first());
        return Ok;
    }

    const QStringList args = parser.positionalArguments();
    QSettings settings;
    if (args.isEmpty()) {
        for (const Key &key : kKeys) {
            const QString value = settings.value(QLatin1String(key.setting)).toString();
            out << QString::fromLatin1(key.name).leftJustified(11) << (value.isEmpty() ? QStringLiteral("(not set)") : value)
                << Qt::endl;
        }
        return Ok;
    }
    const QString action = args.first();
    const Key *key = args.size() > 1 ? findKey(args.at(1)) : nullptr;
    if ((action == u"get" && args.size() == 2 && key) || (action == u"set" && args.size() == 3 && key)) {
        const QString name = QLatin1String(key->name);
        const QString stored = QLatin1String(key->setting);
        if (action == u"get") {
            out << settings.value(stored).toString() << Qt::endl;
            return Ok;
        }
        QString value = args.at(2).trimmed();
        if (name == u"format" || name == u"filter")
            value = value.toLower();
        if (const QString error = invalidValue(name, value); !error.isEmpty()) {
            err << "omascan: " << error << Qt::endl;
            return Failed;
        }
        if (value.isEmpty())
            settings.remove(stored);
        else if (name == u"resolution")
            settings.setValue(stored, value.toInt());
        else
            settings.setValue(stored, value);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            err << "omascan: could not save to " << settings.fileName() << Qt::endl;
            return Failed;
        }
        return Ok;
    }
    if (args.size() > 1 && !key) {
        QStringList names;
        for (const Key &k : kKeys)
            names << QLatin1String(k.name);
        err << "omascan: no setting \"" << args.at(1) << "\"; there are " << names.join(QStringLiteral(", "))
            << Qt::endl;
    } else {
        err << "omascan: use `omascan config`, `config get KEY` or `config set KEY VALUE`" << Qt::endl;
    }
    return Failed;
}

} // namespace

void setIdentity() {
    // The desktop file, Hyprland's window rules and QSettings' file all go by
    // this: ~/.config/omascan/omascan.conf.
    QCoreApplication::setApplicationName(QStringLiteral("omascan"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("omascan"));
}

bool wanted(int argc, char *argv[]) {
    return argc > 1 && kCommands.contains(QString::fromLocal8Bit(argv[1]));
}

int run(const QStringList &arguments, QTextStream &in, QTextStream &out, QTextStream &err) {
    const QString command = arguments.value(1);
    const QStringList rest = arguments.mid(1);
    if (command == u"scan")
        return scan(rest, out, err);
    if (command == u"devices")
        return devices(rest, out, err);
    if (command == u"setup")
        return setup(rest, in, out, err);
    if (command == u"config")
        return config(rest, out, err);
    if (command == u"help") {
        out << usage();
        return Ok;
    }
    err << usage();
    return Failed;
}

} // namespace Cli
