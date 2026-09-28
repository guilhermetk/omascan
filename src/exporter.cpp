#include "exporter.h"

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPtPerInch = 72.0;
constexpr double kMmPerInch = 25.4;

QSizeF paperPoints(const QString &name) {
    if (name == u"Letter") return {612, 792};
    if (name == u"Legal") return {612, 1008};
    return {595.28, 841.89}; // A4
}

QString localPaper() {
    return QLocale().measurementSystem() == QLocale::ImperialUSSystem
        ? QStringLiteral("Letter") : QStringLiteral("A4");
}

struct Quality { int maxDpi; int jpeg; };
Quality qualityFor(const QString &name) {
    if (name == u"best") return {0, 92};
    if (name == u"small") return {150, 60};
    return {200, 78}; // balanced
}

// Where one page's picture goes on the sheet, at what pixel size.
struct Placement {
    QImage image;
    QSizeF sheet;   // points
    QRectF target;  // points, y down from the top
    double dpi = 0; // of the image as placed
};

Placement place(const Page &page, const QString &paper, const Quality &quality) {
    Placement out;
    out.image = PageRender::render(page, 0);
    if (out.image.isNull())
        return out;
    const QSizeF px = out.image.size();

    if ((paper == u"match" || paper.isEmpty()) && page.dpi > 0) {
        // The sheet is the size of what was scanned.
        out.sheet = px * (kPtPerInch / page.dpi);
        out.target = QRectF(QPointF(0, 0), out.sheet);
    } else {
        // A picture with no size of its own (a photo) goes on paper, turned to
        // match its orientation, scaled to fit and centred.
        out.sheet = paperPoints(paper == u"match" ? localPaper() : paper);
        if (px.width() > px.height())
            out.sheet.transpose();
        const QSizeF fitted = px.scaled(out.sheet, Qt::KeepAspectRatio);
        out.target = QRectF(QPointF((out.sheet.width() - fitted.width()) / 2,
                                    (out.sheet.height() - fitted.height()) / 2), fitted);
    }

    out.dpi = px.width() / (out.target.width() / kPtPerInch);
    if (quality.maxDpi > 0 && out.dpi > quality.maxDpi * 1.05) {
        const double scale = quality.maxDpi / out.dpi;
        out.image = out.image.scaled(std::max(1, int(std::lround(px.width() * scale))),
                                     std::max(1, int(std::lround(px.height() * scale))),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out.dpi = quality.maxDpi;
    }
    return out;
}

bool isBilevel(const QImage &image) {
    if (image.format() != QImage::Format_Grayscale8)
        return false;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            if (line[x] != 0 && line[x] != 255)
                return false;
    }
    return true;
}

QByteArray jpeg(const QImage &image, int quality) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(quality);
    writer.setOptimizedWrite(true);
    // Empty when it cannot be encoded: past 65,500 pixels, for one.
    return writer.write(image) ? bytes : QByteArray();
}

// The files a picture export writes: the chosen name for one page, numbered
// from it for several. The save dialog only confirmed the chosen name, so any
// other name already taken moves the whole set on to "name (2)" and so on.
QStringList pictureNames(const QString &path, int count, const QString &extension) {
    const QFileInfo chosen(path);
    const int digits = count >= 100 ? 3 : 2;
    const auto namesFor = [&](const QString &stem) {
        QStringList names;
        if (count == 1)
            names << chosen.absolutePath() + u'/' + stem + u'.' + extension;
        for (int i = 0; count > 1 && i < count; ++i)
            names << QStringLiteral("%1/%2-%3.%4").arg(chosen.absolutePath(), stem)
                         .arg(i + 1, digits, 10, QLatin1Char('0')).arg(extension);
        return names;
    };
    const auto taken = [&](const QString &name) {
        return name != chosen.absoluteFilePath() && QFileInfo::exists(name);
    };
    QStringList names = namesFor(chosen.completeBaseName());
    for (int n = 2; std::any_of(names.cbegin(), names.cend(), taken); ++n)
        names = namesFor(QStringLiteral("%1 (%2)").arg(chosen.completeBaseName()).arg(n));
    return names;
}

// zlib stream for /FlateDecode. qCompress prefixes a 4-byte length; PDF does not.
QByteArray flate(const QByteArray &data) {
    return qCompress(data, 9).mid(4);
}

QByteArray packBits(const QImage &grey) {
    const int stride = (grey.width() + 7) / 8;
    QByteArray bits(qsizetype(stride) * grey.height(), '\0');
    for (int y = 0; y < grey.height(); ++y) {
        const uchar *in = grey.constScanLine(y);
        uchar *out = reinterpret_cast<uchar *>(bits.data()) + qsizetype(y) * stride;
        for (int x = 0; x < grey.width(); ++x)
            if (in[x]) // 1 = white in DeviceGray
                out[x >> 3] |= uchar(0x80 >> (x & 7));
    }
    return bits;
}

QByteArray pdfText(const QString &text) {
    // UTF-16BE with a byte order mark, as a hex string: safe for any title.
    QByteArray hex = "<FEFF";
    for (const QChar c : text)
        hex += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
    return hex + '>';
}

QByteArray num(double v) {
    return QByteArray::number(v, 'f', 3);
}

// A deliberately small PDF writer: one image per page, nothing else. Qt's own
// PDF engine re-encodes pictures its own way; writing the file directly keeps
// scans as JPEG at a chosen quality and black & white pages as 1-bit, which
// is what keeps a 20-page letter under a megabyte.
class PdfWriter {
public:
    explicit PdfWriter(QIODevice *out) : m_out(out) {
        write("%PDF-1.5\n%\xE2\xE3\xCF\xD3\n");
    }

    int reserve() { m_offsets.append(0); return int(m_offsets.size()); }

    void object(int id, const QByteArray &body, const QByteArray &stream = {}, bool hasStream = false) {
        m_offsets[id - 1] = m_written;
        write(QByteArray::number(id) + " 0 obj\n" + body + "\n");
        if (hasStream) {
            write("stream\n");
            write(stream);
            write("\nendstream\n");
        }
        write("endobj\n");
    }

    void finish(int catalog, int info) {
        const qint64 xref = m_written;
        write("xref\n0 " + QByteArray::number(m_offsets.size() + 1) + "\n0000000000 65535 f \n");
        for (qint64 offset : std::as_const(m_offsets))
            write(QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n");
        write("trailer\n<< /Size " + QByteArray::number(m_offsets.size() + 1)
              + " /Root " + QByteArray::number(catalog) + " 0 R /Info "
              + QByteArray::number(info) + " 0 R >>\nstartxref\n"
              + QByteArray::number(xref) + "\n%%EOF\n");
    }

    bool ok() const { return m_ok; }

private:
    void write(const QByteArray &bytes) {
        if (m_out->write(bytes) != bytes.size())
            m_ok = false;
        m_written += bytes.size();
    }

    QIODevice *m_out;
    QList<qint64> m_offsets;
    qint64 m_written = 0;
    bool m_ok = true;
};

} // namespace

Exporter::Exporter(PageModel *model, QObject *parent) : QObject(parent), m_model(model) {
    m_tesseract = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (!m_tesseract.isEmpty()) {
        QProcess list;
        list.start(m_tesseract, {QStringLiteral("--list-langs")});
        if (list.waitForFinished(3000)) {
            const QString out = QString::fromLocal8Bit(list.readAllStandardOutput());
            for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts)) {
                const QString code = line.trimmed();
                if (code.isEmpty() || code.contains(u' ') || code.contains(u':')
                        || code == u"osd" || code == u"equ")
                    continue;
                m_languages.append(code);
            }
        }
    }
}

Exporter::~Exporter() {
    m_cancel = true;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

QString Exporter::defaultOcrLanguage() const {
    const QString mine = QLocale::languageToCode(QLocale().language(), QLocale::ISO639Part3);
    if (m_languages.contains(mine))
        return mine;
    if (m_languages.contains(QStringLiteral("eng")))
        return QStringLiteral("eng");
    return m_languages.value(0);
}

QString Exporter::suggestedName(const QString &extension) const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString stem = QStringLiteral("Scan %1").arg(QDate::currentDate().toString(Qt::ISODate));
    // Several pictures are numbered from the name, so those count as taken too.
    const auto taken = [&](const QString &name) {
        const QString numbered = dir + u'/' + QFileInfo(name).completeBaseName() + u'-';
        return QFileInfo::exists(dir + u'/' + name) || QFileInfo::exists(numbered + u"01." + extension)
            || QFileInfo::exists(numbered + u"001." + extension);
    };
    QString name = stem + u'.' + extension;
    for (int n = 2; taken(name); ++n)
        name = QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(extension);
    return name;
}

QList<Page> Exporter::selectedPages(const QVariantMap &options) const {
    const QList<Page> all = m_model->pages();
    if (options.value(QStringLiteral("scope")).toString() == u"current") {
        const int current = m_model->current();
        return current >= 0 && current < all.size() ? QList<Page>{all.at(current)} : QList<Page>{};
    }
    return all;
}

void Exporter::report(qreal progress, const QString &status) {
    QMetaObject::invokeMethod(this, [this, progress, status] {
        m_progress = progress;
        m_status = status;
        emit progressChanged();
    }, Qt::QueuedConnection);
}

void Exporter::run(std::function<QString()> job, const QUrl &file, bool wholeDocument) {
    if (m_busy)
        return;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
    m_cancel = false;
    m_busy = true;
    emit busyChanged();
    report(0, tr("Preparing…"));

    // Pages can still arrive, or change, while this runs: the document only
    // counts as exported if it is still what was written.
    const quint64 changes = m_model->changes();
    m_thread = QThread::create([this, job, file, wholeDocument, changes] {
        const QString error = job();
        QMetaObject::invokeMethod(this, [this, error, file, wholeDocument, changes] {
            m_busy = false;
            emit busyChanged();
            const bool cancelled = m_cancel.load();
            if (error.isEmpty() && !cancelled && wholeDocument)
                m_model->markExported(changes);
            emit finished(error.isEmpty() && !cancelled,
                          cancelled ? tr("Export cancelled") : error, file);
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

void Exporter::exportPdf(const QUrl &file, const QVariantMap &options) {
    const QList<Page> pages = selectedPages(options);
    const QString path = file.toLocalFile();
    if (pages.isEmpty() || path.isEmpty())
        return;
    const bool ocr = options.value(QStringLiteral("ocr")).toBool() && ocrAvailable();
    run([this, pages, path, options, ocr] {
        return ocr ? writeOcrPdf(pages, path, options) : writePdf(pages, path, options);
    }, file, pages.size() == m_model->count());
}

void Exporter::exportImages(const QUrl &file, const QVariantMap &options) {
    const QList<Page> pages = selectedPages(options);
    const QString path = file.toLocalFile();
    if (pages.isEmpty() || path.isEmpty())
        return;
    const bool png = options.value(QStringLiteral("format")).toString() != u"jpeg";
    const QStringList names = pictureNames(path, int(pages.size()),
                                           png ? QStringLiteral("png") : QStringLiteral("jpg"));
    // Open and Show in folder go to the first picture written.
    run([this, pages, names, options] { return writeImages(pages, names, options); },
        QUrl::fromLocalFile(names.first()), pages.size() == m_model->count());
}

QString Exporter::writePdf(const QList<Page> &pages, const QString &path, const QVariantMap &options) {
    const QString paper = options.value(QStringLiteral("paper")).toString();
    const Quality quality = qualityFor(options.value(QStringLiteral("quality")).toString());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not write to %1").arg(QFileInfo(path).fileName());

    PdfWriter pdf(&file);
    const int catalog = pdf.reserve();
    const int tree = pdf.reserve();
    const int info = pdf.reserve();
    QByteArray kids;

    for (int i = 0; i < pages.size(); ++i) {
        if (m_cancel) {
            file.cancelWriting();
            return {};
        }
        report(qreal(i) / pages.size(), tr("Page %1 of %2").arg(i + 1).arg(pages.size()));

        const Placement p = place(pages.at(i), paper, quality);
        if (p.image.isNull()) {
            file.cancelWriting();
            return tr("Page %1 could not be read").arg(i + 1);
        }

        QByteArray image, dict;
        if (isBilevel(p.image)) {
            image = flate(packBits(p.image));
            dict = "/ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /FlateDecode";
        } else {
            image = jpeg(p.image, quality.jpeg);
            if (image.isEmpty()) {
                file.cancelWriting();
                return tr("Page %1 could not be written").arg(i + 1);
            }
            dict = QByteArray("/ColorSpace ")
                 + (p.image.format() == QImage::Format_Grayscale8 ? "/DeviceGray" : "/DeviceRGB")
                 + " /BitsPerComponent 8 /Filter /DCTDecode";
        }

        const int pageId = pdf.reserve();
        const int contentId = pdf.reserve();
        const int imageId = pdf.reserve();
        kids += QByteArray::number(pageId) + " 0 R ";

        // PDF measures from the bottom-left corner.
        const QRectF t = p.target;
        const QByteArray content = "q " + num(t.width()) + " 0 0 " + num(t.height()) + ' '
            + num(t.x()) + ' ' + num(p.sheet.height() - t.bottom()) + " cm /Im0 Do Q";

        pdf.object(pageId, "<< /Type /Page /Parent " + QByteArray::number(tree) + " 0 R /MediaBox [0 0 "
                   + num(p.sheet.width()) + ' ' + num(p.sheet.height()) + "] /Resources << /XObject << /Im0 "
                   + QByteArray::number(imageId) + " 0 R >> >> /Contents " + QByteArray::number(contentId) + " 0 R >>");
        pdf.object(contentId, "<< /Length " + QByteArray::number(content.size()) + " >>", content, true);
        pdf.object(imageId, "<< /Type /XObject /Subtype /Image /Width " + QByteArray::number(p.image.width())
                   + " /Height " + QByteArray::number(p.image.height()) + ' ' + dict
                   + " /Length " + QByteArray::number(image.size()) + " >>", image, true);
    }

    pdf.object(catalog, "<< /Type /Catalog /Pages " + QByteArray::number(tree) + " 0 R >>");
    pdf.object(tree, "<< /Type /Pages /Kids [" + kids.trimmed() + "] /Count "
               + QByteArray::number(pages.size()) + " >>");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    pdf.object(info, "<< /Title " + pdfText(QFileInfo(path).completeBaseName())
               + " /Producer (OmaScan) /Creator (OmaScan) /CreationDate (D:" + stamp.toLatin1() + ") >>");
    pdf.finish(catalog, info);

    if (!pdf.ok() || !file.commit())
        return tr("Could not finish writing %1").arg(QFileInfo(path).fileName());
    report(1, tr("Done"));
    return {};
}

// Tesseract draws the page and lays invisible, selectable text over it. Each
// picture carries its own resolution, which is what sets the sheet size, so a
// page meant for paper is first put on a white sheet of that paper's shape.
QString Exporter::writeOcrPdf(const QList<Page> &pages, const QString &path, const QVariantMap &options) {
    const QString paper = options.value(QStringLiteral("paper")).toString();
    const Quality quality = qualityFor(options.value(QStringLiteral("quality")).toString());
    QString language = options.value(QStringLiteral("language")).toString();
    if (!m_languages.contains(language))
        language = defaultOcrLanguage();

    QTemporaryDir work;
    if (!work.isValid())
        return tr("No room for temporary files");

    QStringList list;
    for (int i = 0; i < pages.size(); ++i) {
        if (m_cancel)
            return {};
        report(0.25 * i / pages.size(), tr("Preparing page %1 of %2").arg(i + 1).arg(pages.size()));
        Placement p = place(pages.at(i), paper, quality);
        if (p.image.isNull())
            return tr("Page %1 could not be read").arg(i + 1);

        QImage sheet = p.image;
        if (p.target.size() != p.sheet) {
            const double scale = p.image.width() / p.target.width();
            sheet = QImage(QSizeF(p.sheet * scale).toSize(), p.image.format() == QImage::Format_Grayscale8
                                                              ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
            sheet.fill(Qt::white);
            QPainter painter(&sheet);
            painter.drawImage(QPointF(p.target.x() * scale, p.target.y() * scale), p.image);
        }
        const int dpm = int(std::lround(p.dpi / kMmPerInch * 1000));
        sheet.setDotsPerMeterX(dpm);
        sheet.setDotsPerMeterY(dpm);

        const bool bilevel = isBilevel(sheet);
        const QString name = work.filePath(QStringLiteral("page-%1.%2").arg(i, 4, 10, QLatin1Char('0'))
                                           .arg(bilevel ? u"png" : u"jpg"));
        QImageWriter writer(name);
        if (!bilevel)
            writer.setQuality(quality.jpeg);
        if (!writer.write(bilevel ? sheet.convertToFormat(QImage::Format_Mono) : sheet))
            return tr("No room for temporary files");
        list.append(name);
    }

    const QString listFile = work.filePath(QStringLiteral("pages.txt"));
    {
        QFile f(listFile);
        if (!f.open(QIODevice::WriteOnly))
            return tr("No room for temporary files");
        f.write(list.join(u'\n').toLocal8Bit() + '\n');
    }

    QProcess tesseract;
    tesseract.setProcessChannelMode(QProcess::MergedChannels);
    const QString base = work.filePath(QStringLiteral("out"));
    tesseract.start(m_tesseract, {listFile, base, QStringLiteral("-l"), language, QStringLiteral("pdf")});
    if (!tesseract.waitForStarted())
        return tr("Tesseract could not be started");

    int done = 0;
    QByteArray log;
    while (tesseract.state() != QProcess::NotRunning) {
        if (m_cancel) {
            tesseract.kill();
            tesseract.waitForFinished();
            return {};
        }
        tesseract.waitForReadyRead(200);
        const QByteArray chunk = tesseract.readAll();
        log += chunk;
        done += int(chunk.count("Page "));
        report(0.25 + 0.75 * std::min(done, int(pages.size())) / pages.size(),
               tr("Reading text on page %1 of %2").arg(std::min(done + 1, int(pages.size()))).arg(pages.size()));
    }
    if (tesseract.exitStatus() != QProcess::NormalExit || tesseract.exitCode() != 0
            || !QFileInfo::exists(base + QStringLiteral(".pdf"))) {
        const QString last = QString::fromLocal8Bit(log).trimmed().section(u'\n', -1);
        return tr("Text recognition failed: %1").arg(last.isEmpty() ? tr("no output") : last);
    }

    // Replace the file in one step, so a failure leaves any old one intact.
    QFile result(base + QStringLiteral(".pdf"));
    QSaveFile file(path);
    if (!result.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly))
        return tr("Could not write to %1").arg(QFileInfo(path).fileName());
    while (!result.atEnd()) {
        const QByteArray chunk = result.read(1 << 20);
        if (chunk.isEmpty() || file.write(chunk) != chunk.size()) {
            file.cancelWriting();
            break;
        }
    }
    if (!file.commit())
        return tr("Could not finish writing %1").arg(QFileInfo(path).fileName());
    report(1, tr("Done"));
    return {};
}

QString Exporter::writeImages(const QList<Page> &pages, const QStringList &names, const QVariantMap &options) {
    const bool png = options.value(QStringLiteral("format")).toString() != u"jpeg";

    for (int i = 0; i < pages.size(); ++i) {
        if (m_cancel)
            return {};
        report(qreal(i) / pages.size(), tr("Page %1 of %2").arg(i + 1).arg(pages.size()));
        QImage image = PageRender::render(pages.at(i), 0);
        if (image.isNull())
            return tr("Page %1 could not be read").arg(i + 1);
        if (pages.at(i).dpi > 0) {
            const int dpm = int(std::lround(pages.at(i).dpi / kMmPerInch * 1000));
            image.setDotsPerMeterX(dpm);
            image.setDotsPerMeterY(dpm);
        }
        const QString &name = names.at(i);
        QSaveFile file(name);
        if (!file.open(QIODevice::WriteOnly))
            return tr("Could not write %1").arg(QFileInfo(name).fileName());
        QImageWriter writer(&file, png ? "png" : "jpeg");
        if (png) {
            writer.setCompression(6);
            if (isBilevel(image))
                image = image.convertToFormat(QImage::Format_Mono);
        } else {
            writer.setQuality(92);
        }
        if (!writer.write(image) || !file.commit())
            return tr("Could not write %1").arg(QFileInfo(name).fileName());
    }
    report(1, tr("Done"));
    return {};
}

void Exporter::open(const QUrl &file) const {
    QDesktopServices::openUrl(file);
}

void Exporter::showInFolder(const QUrl &file) const {
    // The file manager's own "show this file" when it offers one.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    call << QStringList{file.toString()} << QString();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file.toLocalFile()).absolutePath()));
}
