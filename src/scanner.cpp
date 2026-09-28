#include "scanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {

struct Paper { const char *name; double w, h; }; // millimetres
constexpr Paper kPapers[] = {
    {"A4", 210, 297},
    {"Letter", 215.9, 279.4},
    {"Legal", 215.9, 355.6},
    {"A5", 148, 210},
};
const QString kFullArea = QStringLiteral("Full area");

bool isFeeder(const QString &source) {
    static const QRegularExpression feeder(QStringLiteral("adf|feeder|document|duplex"),
                                           QRegularExpression::CaseInsensitiveOption);
    return feeder.match(source).hasMatch();
}

// A probe of the scanner that never answers (a network device gone quiet, a
// wedged driver) would leave the window waiting forever.
void giveUpAfter(QProcess *process, int ms) {
    QTimer::singleShot(ms, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

// scanimage reads --batch as a printf format: any other % in it is ours.
QString batchPattern(QString dir, const QString &run) {
    return dir.replace(u'%', QStringLiteral("%%")) + u'/' + run + QStringLiteral("-%d.png");
}

QString localPaper() {
    return QLocale().measurementSystem() == QLocale::ImperialUSSystem
        ? QStringLiteral("Letter") : QStringLiteral("A4");
}

} // namespace

Scanner::Scanner(const QString &incomingDir, QObject *parent)
    : QObject(parent), m_incoming(incomingDir) {
    const QString override = qEnvironmentVariable("OMASCAN_SCANIMAGE");
    m_program = !override.isEmpty() ? override
              : QStandardPaths::findExecutable(QStringLiteral("scanimage"));
    QDir().mkpath(m_incoming);
    // Anything here is left from a scan that never finished.
    QDir incoming(m_incoming);
    for (const QString &name : incoming.entryList(QDir::Files | QDir::Hidden))
        incoming.remove(name);

    QSettings settings;
    m_mode = settings.value(QStringLiteral("scan/mode")).toString();
    m_source = settings.value(QStringLiteral("scan/source")).toString();
    m_resolution = settings.value(QStringLiteral("scan/resolution"), 300).toInt();
    m_paperSize = settings.value(QStringLiteral("scan/paper"), localPaper()).toString();

    if (available())
        refresh();
    else
        setStatus(tr("SANE is not installed"));
}

Scanner::~Scanner() {
    for (QProcess *p : {m_discover.data(), m_options.data(), m_scan.data()}) {
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
    }
}

QString Scanner::deviceId() const {
    if (m_deviceIndex < 0 || m_deviceIndex >= m_devices.size())
        return {};
    return m_devices.at(m_deviceIndex).toMap().value(QStringLiteral("id")).toString();
}

bool Scanner::ready() const {
    return available() && !deviceId().isEmpty() && !discovering() && !loadingOptions() && !scanning();
}

void Scanner::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

// ── Discovery ────────────────────────────────────────────────────────────────

void Scanner::refresh() {
    if (!available() || m_discover || m_scan)
        return;

    m_discover = new QProcess(this);
    emit discoveringChanged();
    emit readyChanged();
    setStatus(tr("Looking for scanners…"));

    connect(m_discover, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_discover->deleteLater();
        m_discover = nullptr;
        emit discoveringChanged();
        emit readyChanged();
        setStatus(tr("Could not run scanimage"));
    });
    connect(m_discover, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        const QString previous = deviceId().isEmpty()
            ? QSettings().value(QStringLiteral("scan/device")).toString() : deviceId();
        const QString out = QString::fromLocal8Bit(m_discover->readAllStandardOutput());
        m_discover->deleteLater();
        m_discover = nullptr;

        m_devices.clear();
        for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList f = line.split(u'\t');
            if (f.size() < 4 || f.at(0).isEmpty())
                continue;
            const QString name = (f.at(1) + u' ' + f.at(2)).simplified();
            m_devices.append(QVariantMap{
                {QStringLiteral("id"), f.at(0)},
                {QStringLiteral("name"), name.isEmpty() ? f.at(0) : name},
                {QStringLiteral("kind"), f.at(3)},
            });
        }
        emit devicesChanged();
        emit discoveringChanged();

        int index = m_devices.isEmpty() ? -1 : 0;
        for (int i = 0; i < m_devices.size(); ++i)
            if (m_devices.at(i).toMap().value(QStringLiteral("id")).toString() == previous)
                index = i;
        m_deviceIndex = -1; // force a reload even when it is the same device
        setDeviceIndex(index);
        if (index < 0)
            setStatus(tr("No scanner found"));
        emit readyChanged();
    });

    // One device per line; tabs never appear in SANE device names.
    m_discover->start(m_program, {QStringLiteral("-f"), QStringLiteral("%d\t%v\t%m\t%t%n")});
    giveUpAfter(m_discover, 90000);
}

void Scanner::setDeviceIndex(int index) {
    if (index == m_deviceIndex || index >= m_devices.size() || m_scan)
        return;
    m_deviceIndex = index;
    emit deviceIndexChanged();
    emit readyChanged();
    if (!deviceId().isEmpty()) {
        QSettings().setValue(QStringLiteral("scan/device"), deviceId());
        loadOptions();
    }
}

// ── Options ──────────────────────────────────────────────────────────────────

void Scanner::loadOptions() {
    if (m_options) {
        m_options->disconnect(this);
        m_options->kill();
        m_options->deleteLater();
    }
    m_options = new QProcess(this);
    emit optionsChanged();
    emit readyChanged();
    setStatus(tr("Talking to the scanner…"));

    connect(m_options, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_options->deleteLater();
        m_options = nullptr;
        emit optionsChanged();
        emit readyChanged();
        setStatus(tr("Could not run scanimage"));
    });
    connect(m_options, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString out = QString::fromLocal8Bit(m_options->readAllStandardOutput());
        const QString err = QString::fromLocal8Bit(m_options->readAllStandardError());
        m_options->deleteLater();
        m_options = nullptr;
        parseOptions(out);
        emit optionsChanged();
        emit settingsChanged();
        emit readyChanged();
        if (code != 0 && m_modes.isEmpty() && m_sources.isEmpty())
            setStatus(friendlyError(err));
        else
            setStatus(tr("Ready"));
    });
    m_options->start(m_program, {QStringLiteral("-d"), deviceId(), QStringLiteral("-A")});
    giveUpAfter(m_options, 60000);
}

// Reads the option list scanimage prints for a device, e.g.
//     --mode Color|Gray|Lineart [Color]
//     --resolution 75|150|300|600dpi [75]
//     --resolution 50..1200dpi (in steps of 1) [150]
//     -x 0..215.9mm [215.9]
void Scanner::parseOptions(const QString &text) {
    static const QRegularExpression option(
        QStringLiteral(R"(^\s+--?([A-Za-z][\w-]*)\s+(.*?)\s+\[(.*)\]\s*$)"));
    static const QRegularExpression range(QStringLiteral(R"(^(-?[\d.]+)\.\.(-?[\d.]+)\s*(dpi|mm|in)?)"));

    m_modes.clear();
    m_sources.clear();
    m_resolutionList.clear();
    m_resolutionMin = m_resolutionMax = 0;
    m_maxArea = {};
    m_hasGeometry = false;
    QString defaultMode, defaultSource;
    int defaultResolution = 0;
    double maxX = 0, maxY = 0;

    for (const QString &line : text.split(u'\n')) {
        const auto m = option.match(line);
        if (!m.hasMatch())
            continue;
        const QString name = m.captured(1);
        const QString values = m.captured(2);
        const QString current = m.captured(3);
        if (current == u"inactive")
            continue;

        if (name == u"mode") {
            m_modes = values.split(u'|', Qt::SkipEmptyParts);
            defaultMode = current;
        } else if (name == u"source") {
            m_sources = values.split(u'|', Qt::SkipEmptyParts);
            defaultSource = current;
        } else if (name == u"resolution") {
            defaultResolution = int(current.toDouble());
            const auto r = range.match(values);
            if (r.hasMatch()) {
                m_resolutionMin = int(std::ceil(r.captured(1).toDouble()));
                m_resolutionMax = int(r.captured(2).toDouble());
            } else {
                QString list = values;
                list.remove(QStringLiteral("dpi"));
                for (const QString &v : list.split(u'|', Qt::SkipEmptyParts))
                    if (const int dpi = int(v.trimmed().toDouble()); dpi > 0)
                        m_resolutionList.append(dpi);
            }
        } else if (name == u"x" || name == u"y") {
            const auto r = range.match(values);
            if (r.hasMatch() && r.captured(3) != u"in") { // pixels or inches: leave the area alone
                (name == u"x" ? maxX : maxY) = r.captured(2).toDouble();
                m_hasGeometry = true;
            }
        }
    }
    if (maxX > 0 && maxY > 0)
        m_maxArea = QSizeF(maxX, maxY);

    // Keep what was chosen last time when this device offers it too.
    if (!m_modes.contains(m_mode)) {
        const auto colour = std::find_if(m_modes.cbegin(), m_modes.cend(), [](const QString &m) {
            return m.contains(QStringLiteral("colo"), Qt::CaseInsensitive);
        });
        m_mode = colour != m_modes.cend() ? *colour : defaultMode;
    }
    if (!m_sources.contains(m_source))
        m_source = defaultSource;
    const QVariantList offered = resolutions();
    if (!offered.isEmpty() && !offered.contains(m_resolution)) {
        int best = offered.first().toInt();
        for (const QVariant &v : offered)
            if (std::abs(v.toInt() - 300) < std::abs(best - 300))
                best = v.toInt();
        m_resolution = best;
    } else if (offered.isEmpty() && defaultResolution > 0) {
        m_resolution = defaultResolution;
    }
    if (!paperSizes().contains(m_paperSize))
        m_paperSize = paperSizes().contains(localPaper()) ? localPaper() : kFullArea;
}

QVariantList Scanner::resolutions() const {
    QVariantList out;
    if (!m_resolutionList.isEmpty()) {
        for (int dpi : m_resolutionList)
            out.append(dpi);
        return out;
    }
    // A continuous range: offer the usual stops inside it.
    for (int dpi : {75, 100, 150, 200, 300, 400, 600, 1200, 2400})
        if (dpi >= m_resolutionMin && dpi <= m_resolutionMax)
            out.append(dpi);
    return out;
}

QStringList Scanner::paperSizes() const {
    QStringList out{kFullArea};
    if (!m_hasGeometry)
        return out;
    for (const Paper &p : kPapers)
        if (m_maxArea.isEmpty() || (p.w <= m_maxArea.width() + 0.5 && p.h <= m_maxArea.height() + 0.5))
            out.append(QString::fromLatin1(p.name));
    return out;
}

bool Scanner::feeder() const { return isFeeder(m_source); }

void Scanner::remember() const {
    QSettings settings;
    settings.setValue(QStringLiteral("scan/mode"), m_mode);
    settings.setValue(QStringLiteral("scan/source"), m_source);
    settings.setValue(QStringLiteral("scan/resolution"), m_resolution);
    settings.setValue(QStringLiteral("scan/paper"), m_paperSize);
}

void Scanner::setMode(const QString &mode) {
    if (mode == m_mode) return;
    m_mode = mode;
    remember();
    emit settingsChanged();
}

void Scanner::setSource(const QString &source) {
    if (source == m_source) return;
    m_source = source;
    remember();
    emit settingsChanged();
}

void Scanner::setResolution(int dpi) {
    if (dpi == m_resolution) return;
    m_resolution = dpi;
    remember();
    emit settingsChanged();
}

void Scanner::setPaperSize(const QString &size) {
    if (size == m_paperSize) return;
    m_paperSize = size;
    remember();
    emit settingsChanged();
}

// ── Scanning ─────────────────────────────────────────────────────────────────

void Scanner::scan() {
    if (!ready())
        return;

    QStringList args{QStringLiteral("-d"), deviceId(), QStringLiteral("--format=png"),
                     QStringLiteral("-p")};
    if (!m_mode.isEmpty() && !m_modes.isEmpty())
        args << QStringLiteral("--mode") << m_mode;
    if (!m_source.isEmpty() && !m_sources.isEmpty())
        args << QStringLiteral("--source") << m_source;
    if (m_resolution > 0)
        args << QStringLiteral("--resolution") << QString::number(m_resolution);
    for (const Paper &p : kPapers) {
        if (m_hasGeometry && m_paperSize == QLatin1String(p.name)) {
            args << QStringLiteral("-l") << QStringLiteral("0") << QStringLiteral("-t") << QStringLiteral("0")
                 << QStringLiteral("-x") << QString::number(p.w) << QStringLiteral("-y") << QString::number(p.h);
        }
    }

    const QString run = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_run = run;
    m_singleOutput.clear();
    if (feeder()) {
        // Pages arrive one at a time; --batch-print names each as it is done.
        args << QStringLiteral("--batch=") + batchPattern(m_incoming, run)
             << QStringLiteral("--batch-print");
    } else {
        m_singleOutput = QStringLiteral("%1/%2.png").arg(m_incoming, run);
        args << QStringLiteral("-o") << m_singleOutput;
    }

    m_scan = new QProcess(this);
    m_cancelled = false;
    m_pagesThisRun = 0;
    m_progress = -1;
    m_stderrTail.clear();
    m_runResolution = m_resolution;
    connect(m_scan, &QProcess::readyReadStandardOutput, this, &Scanner::readScanOutput);
    connect(m_scan, &QProcess::readyReadStandardError, this, &Scanner::readScanErrors);
    connect(m_scan, &QProcess::finished, this, &Scanner::scanFinished);
    connect(m_scan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            scanFinished(-1, QProcess::CrashExit);
    });
    emit scanningChanged();
    emit progressChanged();
    emit readyChanged();
    setStatus(feeder() ? tr("Scanning from the feeder…") : tr("Scanning…"));
    m_scan->start(m_program, args);
}

void Scanner::cancel() {
    if (!m_scan)
        return;
    m_cancelled = true;
    setStatus(tr("Stopping…"));
    // scanimage cancels the scan cleanly on SIGTERM; a stuck driver gets killed.
    m_scan->terminate();
    QPointer<QProcess> process = m_scan;
    QTimer::singleShot(4000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void Scanner::readScanOutput() {
    while (m_scan && m_scan->canReadLine()) {
        const QString line = QString::fromLocal8Bit(m_scan->readLine()).trimmed();
        // Only the pages this run was asked to write, never any other file.
        const QFileInfo file(line);
        if (file.fileName().startsWith(m_run + u'-') && file.fileName().endsWith(QStringLiteral(".png"))
                && file.absolutePath() == QFileInfo(m_incoming).absoluteFilePath() && file.isFile())
            deliver(file.absoluteFilePath());
    }
}

void Scanner::readScanErrors() {
    static const QRegularExpression progress(QStringLiteral(R"(Progress:\s*([\d.]+)%)"));
    if (!m_scan)
        return;
    const QString chunk = QString::fromLocal8Bit(m_scan->readAllStandardError());
    auto matches = progress.globalMatch(chunk);
    qreal last = -1;
    while (matches.hasNext())
        last = matches.next().captured(1).toDouble() / 100.0;
    if (last >= 0 && !qFuzzyCompare(last + 1, m_progress + 1)) {
        m_progress = std::clamp(last, 0.0, 1.0);
        emit progressChanged();
    }
    // Keep the last few lines for an error message; progress lines are noise.
    QString text = chunk;
    text.remove(progress);
    m_stderrTail = (m_stderrTail + text).right(2000);
}

void Scanner::deliver(const QString &path) {
    ++m_pagesThisRun;
    m_progress = -1;
    emit progressChanged();
    if (feeder())
        setStatus(m_pagesThisRun == 1 ? tr("Scanned 1 page…")
                                      : tr("Scanned %1 pages…").arg(m_pagesThisRun));
    emit pageScanned(path, m_runResolution);
}

void Scanner::scanFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_scan)
        return;
    // Anything still buffered, e.g. the last --batch-print line, and the
    // last words on stderr, which say why it stopped.
    readScanOutput();
    readScanErrors();
    m_scan->deleteLater();
    m_scan = nullptr;

    bool ok = status == QProcess::NormalExit && exitCode == 0;
    if (!m_singleOutput.isEmpty()) {
        const QFileInfo out(m_singleOutput);
        if (ok && out.exists() && out.size() > 0)
            deliver(m_singleOutput);
    }
    // Delivered pages have moved into the session; what is left is a page cut
    // short by a cancel or a jam.
    QDir incoming(m_incoming);
    for (const QString &name : incoming.entryList({m_run + u'*'}, QDir::Files))
        incoming.remove(name);
    // A feeder that ran out after at least one page is the normal way to end.
    if (!ok && m_pagesThisRun > 0 && m_stderrTail.contains(QStringLiteral("out of documents"),
                                                          Qt::CaseInsensitive))
        ok = true;

    if (m_cancelled)
        setStatus(tr("Scan cancelled"));
    else if (!ok) {
        const QString message = friendlyError(m_stderrTail);
        setStatus(message);
        emit failed(message);
    } else {
        setStatus(m_pagesThisRun == 1 ? tr("Scanned 1 page")
                                      : tr("Scanned %1 pages").arg(m_pagesThisRun));
    }
    m_progress = -1;
    emit progressChanged();
    emit scanningChanged();
    emit readyChanged();
}

// scanimage speaks in SANE status strings. Say what to do instead.
QString Scanner::friendlyError(const QString &raw) {
    const QString text = raw.toLower();
    if (text.contains(QStringLiteral("out of documents")) || text.contains(QStringLiteral("no docs")))
        return tr("The document feeder is empty");
    if (text.contains(QStringLiteral("busy")))
        return tr("The scanner is busy — another app may be using it");
    if (text.contains(QStringLiteral("jammed")))
        return tr("Paper is jammed in the scanner");
    if (text.contains(QStringLiteral("cover open")))
        return tr("The scanner's cover is open");
    if (text.contains(QStringLiteral("access to resource has been denied")))
        return tr("Not allowed to use the scanner (check your user is in the scanner group)");
    if (text.contains(QStringLiteral("i/o")) || text.contains(QStringLiteral("invalid argument")))
        return tr("Lost contact with the scanner — check it is on and connected");
    const QStringList lines = raw.split(u'\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        QString line = it->trimmed();
        if (line.isEmpty())
            continue;
        line.remove(QRegularExpression(QStringLiteral("^scanimage:\\s*")));
        return tr("Scan failed: %1").arg(line);
    }
    return tr("The scan did not finish");
}
