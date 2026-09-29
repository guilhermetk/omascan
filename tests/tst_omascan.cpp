// The model and the exporter, without a window: pages in, edits, undo, what
// survives a restart, and every export format written and readable. The
// command line too, scanning with bin/fake-scanimage.

#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

#include "cli.h"
#include "exporter.h"
#include "keys.h"
#include "pages.h"

namespace {

// A page of "document": pale grey paper with black text on it.
QString makePage(const QString &dir, const QString &name, const QString &heading) {
    QImage page(1240, 1754, QImage::Format_RGB32); // A4 at 150 dpi
    page.fill(QColor(0xe2, 0xde, 0xd4));
    QPainter p(&page);
    p.setPen(Qt::black);
    QFont font(QStringLiteral("Noto Sans"));
    font.setPixelSize(64);
    font.setBold(true);
    p.setFont(font);
    p.drawText(120, 220, heading);
    font.setPixelSize(30);
    font.setBold(false);
    p.setFont(font);
    for (int line = 0; line < 12; ++line)
        p.drawText(120, 360 + line * 52, QStringLiteral("The quick brown fox jumps over the lazy dog %1").arg(line + 1));
    p.end();
    const QString path = dir + u'/' + name;
    page.save(path);
    return path;
}

QString run(const QString &program, const QStringList &args) {
    QProcess process;
    process.start(program, args);
    process.waitForFinished(60000);
    return QString::fromLocal8Bit(process.readAllStandardOutput() + process.readAllStandardError());
}

// Hands a picture to the model the way the scanner does: a file in incoming/.
void addScan(PageModel &model, const QString &picture) {
    const QString incoming = model.sessionDir() + QStringLiteral("/incoming");
    QDir().mkpath(incoming);
    const QString copy = incoming + u'/' + QFileInfo(picture).fileName();
    QFile::remove(copy);
    QVERIFY(QFile::copy(picture, copy));
    model.addScan(copy, 150);
}

struct CliResult { int code; QJsonObject json; QString out; QString err; };

// Runs `omascan <arguments>` in this process, answering prompts with `input`.
CliResult cli(const QStringList &arguments, const QString &input = {}) {
    QString inText = input, outText, errText;
    QTextStream in(&inText), out(&outText), err(&errText);
    const int code = Cli::run(QStringList{QStringLiteral("omascan")} + arguments, in, out, err);
    out.flush();
    err.flush();
    return {code, QJsonDocument::fromJson(outText.toUtf8()).object(), outText, errText};
}

bool waitForExport(Exporter &exporter, bool *ok, QString *message) {
    QSignalSpy spy(&exporter, &Exporter::finished);
    if (!spy.wait(120000))
        return false;
    *ok = spy.first().at(0).toBool();
    *message = spy.first().at(1).toString();
    return true;
}

} // namespace

class OmaScanTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_home;
    QTemporaryDir m_out;
    QStringList m_pictures;
    // The export tests share one model, so marking it exported on disk does
    // not empty the next test's model.
    std::unique_ptr<PageModel> m_model;

private slots:
    void initTestCase() {
        // Before anything reads a setting: QSettings keeps the first folder it sees.
        Cli::setIdentity();
        qputenv("XDG_DATA_HOME", m_home.path().toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", (m_home.path() + QStringLiteral("/config")).toLocal8Bit());
        qputenv("OMASCAN_SCANIMAGE", OMASCAN_ROOT "/bin/fake-scanimage");
        // Where `omascan scan` puts its files, instead of the real home's.
        QDir().mkpath(m_home.path() + QStringLiteral("/config"));
        QFile dirs(m_home.path() + QStringLiteral("/config/user-dirs.dirs"));
        QVERIFY(dirs.open(QIODevice::WriteOnly));
        dirs.write(QStringLiteral("XDG_DOCUMENTS_DIR=\"%1/Documents\"\nXDG_PICTURES_DIR=\"%1/Pictures\"\n")
                       .arg(m_home.path()).toUtf8());
        dirs.close();
        QImageReader::setAllocationLimit(PageRender::kImageLimitMiB);
        QStandardPaths::setTestModeEnabled(false);
        m_pictures << makePage(m_out.path(), QStringLiteral("a.png"), QStringLiteral("Invoice Alpha"))
                   << makePage(m_out.path(), QStringLiteral("b.png"), QStringLiteral("Receipt Bravo"))
                   << makePage(m_out.path(), QStringLiteral("c.png"), QStringLiteral("Letter Charlie"));
    }

    void scanEditUndo() {
        PageModel model;
        model.clear();
        QCOMPARE(model.restoredCount(), 0);
        for (const QString &p : m_pictures)
            addScan(model, p);
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.page(0).value("dpi").toDouble(), 150.0);
        QCOMPARE(model.current(), 2);

        model.rotate(1, 90);
        QCOMPARE(model.page(1).value("pixelWidth").toInt(), 1754);
        model.setCrop(2, 0.1, 0.1, 0.8, 0.5);
        QCOMPARE(model.page(2).value("pixelWidth").toInt(), 992);
        QCOMPARE(model.page(2).value("pixelHeight").toInt(), 877);

        // A crop turns with the page.
        model.rotate(2, 90);
        const QRectF turned = model.page(2).value("crop").toRectF();
        QVERIFY(qAbs(turned.x() - 0.4) < 1e-9 && qAbs(turned.y() - 0.1) < 1e-9);
        QCOMPARE(model.page(2).value("pixelWidth").toInt(), 877);

        model.remove(0);
        QCOMPARE(model.count(), 2);
        model.undo();
        QCOMPARE(model.count(), 3);
        model.redo();
        QCOMPARE(model.count(), 2);
        model.undo();

        model.move(0, 2);
        QCOMPARE(model.page(2).value("pixelWidth").toInt(), 1240); // Alpha, unrotated, now last
        model.undo();
        model.setFilter(0, Page::BlackWhite);
    }

    // Pages not yet exported are on disk: a new run sees the same pages and edits.
    void unexportedPagesSurviveRestart() {
        PageModel model;
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.restoredCount(), 3);
        QCOMPARE(model.page(0).value("filter").toInt(), int(Page::BlackWhite));
        QCOMPARE(model.page(1).value("rotation").toInt(), 90);
    }

    void rendering() {
        PageModel model;
        Page page;
        QVERIFY(model.pageById(model.page(0).value("pageId").toInt(), &page));
        // Black & white really is two levels.
        const QImage bw = PageRender::render(page, 600);
        QCOMPARE(bw.format(), QImage::Format_Grayscale8);
        QSet<int> levels;
        for (int y = 0; y < bw.height(); y += 3)
            for (int x = 0; x < bw.width(); x += 3)
                levels.insert(bw.constScanLine(y)[x]);
        QCOMPARE(levels, (QSet<int>{0, 255}));

        // Enhanced turns the grey paper white.
        page.filter = Page::Enhanced;
        const QImage enhanced = PageRender::render(page, 600);
        const QColor paper = enhanced.pixelColor(enhanced.width() - 10, enhanced.height() - 10);
        QVERIFY2(paper.red() > 245 && paper.green() > 245 && paper.blue() > 245,
                 qPrintable(paper.name()));
    }

    void exportPdf() {
        if (!m_model)
            m_model = std::make_unique<PageModel>();
        Exporter exporter(m_model.get());
        const QString path = m_out.path() + QStringLiteral("/scan.pdf");
        exporter.exportPdf(QUrl::fromLocalFile(path), {{"paper", "A4"}, {"quality", "balanced"}});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));

        const QString check = run(QStringLiteral("qpdf"), {QStringLiteral("--check"), path});
        QVERIFY2(check.contains(QStringLiteral("No syntax or stream encoding errors")), qPrintable(check));
        const QString info = run(QStringLiteral("pdfinfo"), {QStringLiteral("-f"), QStringLiteral("1"),
                                                             QStringLiteral("-l"), QStringLiteral("3"), path});
        QVERIFY2(info.contains(QStringLiteral("Pages:           3")), qPrintable(info));
        // Page 2 was turned landscape, so its sheet is too.
        QVERIFY2(info.contains(QStringLiteral("Page    1 size:  595.28 x 841.89")), qPrintable(info));
        QVERIFY2(info.contains(QStringLiteral("Page    2 size:  841.89 x 595.28")), qPrintable(info));
        // Black & white goes in at one bit; the others as JPEG.
        const QString images = run(QStringLiteral("pdfimages"), {QStringLiteral("-list"), path});
        QVERIFY2(images.contains(QRegularExpression(QStringLiteral(R"(\n\s+1\s+0 image.*gray\s+1\s+1\s+image)"))), qPrintable(images));
        QVERIFY2(images.contains(QStringLiteral("jpeg")), qPrintable(images));
        qInfo().noquote() << "PDF size" << QFileInfo(path).size() / 1024 << "KiB";
    }

    void exportSearchablePdf() {
        if (!m_model)
            m_model = std::make_unique<PageModel>();
        Exporter exporter(m_model.get());
        if (!exporter.ocrAvailable())
            QSKIP("tesseract is not installed");
        const QString path = m_out.path() + QStringLiteral("/searchable.pdf");
        exporter.exportPdf(QUrl::fromLocalFile(path), {{"paper", "match"}, {"quality", "balanced"},
                                                       {"ocr", true}, {"language", "eng"}});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));
        const QString text = run(QStringLiteral("pdftotext"), {path, QStringLiteral("-")});
        QVERIFY2(text.contains(QStringLiteral("Invoice Alpha")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("quick brown fox")), qPrintable(text));
        const QString info = run(QStringLiteral("pdfinfo"), {path});
        QVERIFY2(info.contains(QStringLiteral("Pages:           3")), qPrintable(info));
    }

    void exportPictures() {
        if (!m_model)
            m_model = std::make_unique<PageModel>();
        Exporter exporter(m_model.get());
        const QString path = m_out.path() + QStringLiteral("/page.jpg");
        exporter.exportImages(QUrl::fromLocalFile(path), {{"format", "jpeg"}});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));
        for (const char *name : {"page-01.jpg", "page-02.jpg", "page-03.jpg"})
            QVERIFY2(!QImage(m_out.path() + u'/' + QLatin1String(name)).isNull(), name);
        QCOMPARE(QImage(m_out.path() + QStringLiteral("/page-02.jpg")).width(), 1754);
    }

    // A change after exporting means there is unsaved work again.
    void changeAfterExportComesBack() {
        m_model->rotate(0, 90);
        PageModel restarted;
        QCOMPARE(restarted.restoredCount(), 3);
        m_model->rotate(0, -90);
    }

    // Exporting only some pages does not finish the document.
    void partialExportComesBack() {
        Exporter exporter(m_model.get());
        exporter.exportImages(QUrl::fromLocalFile(m_out.path() + QStringLiteral("/one.png")),
                              {{"scope", "current"}});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));
        PageModel restarted;
        QCOMPARE(restarted.restoredCount(), 3);
    }

    // After the whole document is exported, the next run starts empty and the
    // pictures are cleared away.
    void exportedDocumentStartsFresh() {
        Exporter exporter(m_model.get());
        exporter.exportPdf(QUrl::fromLocalFile(m_out.path() + QStringLiteral("/final.pdf")), {});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));
        m_model.reset();

        PageModel restarted;
        QCOMPARE(restarted.count(), 0);
        QCOMPARE(restarted.restoredCount(), 0);
        QCOMPARE(QDir(restarted.sessionDir()).entryList({QStringLiteral("*.png")}, QDir::Files), QStringList{});
    }

    // A page scanned while the export runs is not in the file, so the document
    // is not finished.
    void pageDuringExportComesBack() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        addScan(model, m_pictures.at(1));
        Exporter exporter(&model);
        exporter.exportPdf(QUrl::fromLocalFile(m_out.path() + QStringLiteral("/during.pdf")), {});
        addScan(model, m_pictures.at(2));
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));

        PageModel restarted;
        QCOMPARE(restarted.restoredCount(), 3);
    }

    // An adjustment made without an undo step, as a slider moved by keyboard
    // does, still means there is unsaved work after an export.
    void adjustmentAfterExportComesBack() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        Exporter exporter(&model);
        exporter.exportPdf(QUrl::fromLocalFile(m_out.path() + QStringLiteral("/adjusted.pdf")), {});
        bool ok = false;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY2(ok, qPrintable(message));
        model.setAdjustment(0, QStringLiteral("brightness"), 10);
        PageModel restarted;
        QCOMPARE(restarted.restoredCount(), 1);
    }

    // A name whose extension was changed after the save dialog confirmed it
    // was never asked about, so it is not replaced.
    void unconfirmedNameIsKept() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        Exporter exporter(&model);
        const QString dir = m_out.path() + QStringLiteral("/renamed");
        QVERIFY(QDir().mkpath(dir));
        const QString existing = dir + QStringLiteral("/scan.jpg");
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("keep");
        file.close();
        QSignalSpy spy(&exporter, &Exporter::finished);
        exporter.exportImages(QUrl::fromLocalFile(existing), {{"format", "jpeg"}, {"confirmed", false}});
        QVERIFY(spy.wait(120000));
        QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
        QCOMPARE(spy.first().at(2).toUrl().toLocalFile(), dir + QStringLiteral("/scan (2).jpg"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep"));

        // The same goes for ".pdf" added to a name typed without it.
        const QString pdf = dir + QStringLiteral("/scan.pdf");
        QFile existingPdf(pdf);
        QVERIFY(existingPdf.open(QIODevice::WriteOnly));
        existingPdf.write("keep");
        existingPdf.close();
        QSignalSpy pdfSpy(&exporter, &Exporter::finished);
        exporter.exportPdf(QUrl::fromLocalFile(pdf), {{"confirmed", false}});
        QVERIFY(pdfSpy.wait(120000));
        QVERIFY2(pdfSpy.first().at(0).toBool(), qPrintable(pdfSpy.first().at(1).toString()));
        QCOMPARE(pdfSpy.first().at(2).toUrl().toLocalFile(), dir + QStringLiteral("/scan (2).pdf"));
        QVERIFY(existingPdf.open(QIODevice::ReadOnly));
        QCOMPARE(existingPdf.readAll(), QByteArray("keep"));

        // A name the dialog confirmed is still replaced as asked.
        QSignalSpy confirmedSpy(&exporter, &Exporter::finished);
        exporter.exportPdf(QUrl::fromLocalFile(pdf), {});
        QVERIFY(confirmedSpy.wait(120000));
        QCOMPARE(confirmedSpy.first().at(2).toUrl().toLocalFile(), pdf);
    }

    // Numbered pictures never replace files the save dialog did not ask about,
    // and the export reports a file that exists.
    void picturesKeepEarlierExports() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        addScan(model, m_pictures.at(1));
        Exporter exporter(&model);
        const QString dir = m_out.path() + QStringLiteral("/pictures");
        QVERIFY(QDir().mkpath(dir));
        QStringList written;
        for (int run = 0; run < 2; ++run) {
            QSignalSpy spy(&exporter, &Exporter::finished);
            exporter.exportImages(QUrl::fromLocalFile(dir + QStringLiteral("/scan.png")), {});
            QVERIFY(spy.wait(120000));
            QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
            written << spy.first().at(2).toUrl().toLocalFile();
        }
        QCOMPARE(written, (QStringList{dir + QStringLiteral("/scan-01.png"), dir + QStringLiteral("/scan (2)-01.png")}));
        QCOMPARE(QDir(dir).entryList(QDir::Files).size(), 4);
    }

    // A page that cannot be encoded fails the export, and the document is
    // not taken for finished.
    void unencodablePageFailsExport() {
        QImage tall(2, 66000, QImage::Format_Grayscale8); // past libjpeg's 65,500
        for (int y = 0; y < tall.height(); ++y)
            std::fill_n(tall.scanLine(y), tall.width(), uchar(40 + y % 160));
        const QString path = m_out.path() + QStringLiteral("/tall.png");
        QVERIFY(tall.save(path));
        PageModel model;
        model.clear();
        addScan(model, path);
        QCOMPARE(model.count(), 1);
        model.setFilter(0, Page::Original);
        Exporter exporter(&model);
        exporter.exportPdf(QUrl::fromLocalFile(m_out.path() + QStringLiteral("/tall.pdf")), {{"quality", "best"}});
        bool ok = true;
        QString message;
        QVERIFY(waitForExport(exporter, &ok, &message));
        QVERIFY(!ok);
        QVERIFY(!QFile::exists(m_out.path() + QStringLiteral("/tall.pdf")));

        PageModel restarted;
        QCOMPARE(restarted.restoredCount(), 1);
        restarted.clear();
    }

    // Undo after Reset brings the adjustments back.
    void undoReset() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        model.checkpoint(QStringLiteral("Adjust brightness"));
        model.setAdjustment(0, QStringLiteral("brightness"), 40);
        model.resetAdjustments(0);
        QCOMPARE(model.page(0).value("brightness").toInt(), 0);
        model.undo();
        QCOMPARE(model.page(0).value("brightness").toInt(), 40);
        model.redo();
        QCOMPARE(model.page(0).value("brightness").toInt(), 0);
    }

    // A state that parses but is not one save() writes is as good as none.
    void malformedStateKeepsPictures() {
        {
            PageModel model;
            model.clear();
            addScan(model, m_pictures.at(0));
            addScan(model, m_pictures.at(1));
        }
        for (const char *json : {"{}", "{\"version\": 1, \"pages\": {}}", "{\"version\": 1, \"pages\": [], \"exported\": 1}"}) {
            QSaveFile file(PageModel::defaultDir() + QStringLiteral("/state.json"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(json);
            QVERIFY(file.commit());
            PageModel restarted;
            QVERIFY2(restarted.count() == 2, json);
        }
    }

    // A state that cannot be saved is said once, and not left behind to
    // speak for a document it no longer describes.
    void failedSaveIsSaid() {
        PageModel model;
        model.clear();
        addScan(model, m_pictures.at(0));
        QSignalSpy said(&model, &PageModel::message);
        const QString dir = model.sessionDir();
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner);
        if (QFileInfo(dir).isWritable()) {
            QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QSKIP("The session folder cannot be made read-only here");
        }
        model.rotate(0, 90);
        model.rotate(0, 90);
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(said.size(), 1);
        model.rotate(0, 180);
        PageModel restarted;
        QCOMPARE(restarted.page(0).value("rotation").toInt(), 0);
    }

    // Menu shortcut labels: strings and standard keys alike read as key caps.
    void shortcutLabels() {
        QCOMPARE(Keys::text(QStringLiteral("Ctrl+E")), QStringLiteral("Ctrl + E"));
        QCOMPARE(Keys::text(QStringLiteral("Ctrl+Return")), QStringLiteral("Ctrl + Enter"));
        QCOMPARE(Keys::text(QStringLiteral("[")), QStringLiteral("["));
        QCOMPARE(Keys::text(int(QKeySequence::ZoomIn)).left(7), QStringLiteral("Ctrl + "));
        QCOMPARE(Keys::text(int(QKeySequence::ZoomOut)), QStringLiteral("Ctrl + -"));
        QCOMPARE(Keys::text(int(QKeySequence::Undo)), QStringLiteral("Ctrl + Z"));
        QCOMPARE(Keys::text(int(QKeySequence::Quit)), QStringLiteral("Ctrl + Q"));
        QCOMPARE(Keys::text(int(QKeySequence::Delete)), QStringLiteral("Del"));
        QCOMPARE(Keys::text(QVariant()), QString());
        qInfo().noquote() << "Zoom in:" << Keys::text(int(QKeySequence::ZoomIn))
                          << "| Redo:" << Keys::text(int(QKeySequence::Redo));
    }

    // Past 16 million pixels the local threshold's summed table wraps around;
    // a 600 dpi page must still come out as white paper, not black.
    void bigBlackAndWhite() {
        QImage white(6000, 6000, QImage::Format_Grayscale8);
        white.fill(235);
        const QString path = m_out.path() + QStringLiteral("/big.png");
        QVERIFY(white.save(path));
        Page page;
        page.source = path;
        page.sourceSize = white.size();
        page.filter = Page::BlackWhite;
        const QImage bw = PageRender::render(page, 0);
        QCOMPARE(bw.size(), white.size());
        qint64 ink = 0;
        for (int y = 0; y < bw.height(); y += 7)
            for (int x = 0; x < bw.width(); x += 7)
                ink += bw.constScanLine(y)[x] == 0;
        QCOMPARE(ink, 0);
        PageRender::dropCache(path);
    }

    // A state file that cannot be read must not cost the scans it described.
    void corruptStateKeepsPictures() {
        {
            PageModel model;
            model.clear();
            addScan(model, m_pictures.at(0));
            addScan(model, m_pictures.at(1));
        }
        const QString state = PageModel::defaultDir() + QStringLiteral("/state.json");
        QFile file(state);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"version\": 1, \"pages\": [");
        file.close();

        PageModel restarted;
        QCOMPARE(restarted.count(), 2);
        QCOMPARE(QDir(restarted.sessionDir()).entryList({QStringLiteral("*.png")}, QDir::Files).size(), 2);
    }

    // What the state file says is checked, not trusted.
    void stateIsValidated() {
        QString source;
        {
            PageModel model;
            model.clear();
            addScan(model, m_pictures.at(0));
            Page page;
            QVERIFY(model.pageById(model.page(0).value("pageId").toInt(), &page));
            source = QFileInfo(page.source).fileName();
        }
        const QString outside = QFileInfo(PageModel::defaultDir()).absolutePath() + QStringLiteral("/outside.png");
        QVERIFY(QFile::copy(m_pictures.at(1), outside));

        const QJsonObject tampered{
            {"source", source}, {"rotation", 45}, {"crop", QJsonArray{0.5, 0.5, 5, 5}},
            {"filter", 9}, {"brightness", 999}, {"contrast", -999}, {"threshold", 400}, {"dpi", 1e-9},
        };
        const QJsonObject escaping{{"source", QStringLiteral("../outside.png")}};
        const QJsonObject nothing{{"source", QString()}};
        QSaveFile file(PageModel::defaultDir() + QStringLiteral("/state.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"current", 7},
                                             {"pages", QJsonArray{tampered, escaping, nothing}}}).toJson());
        QVERIFY(file.commit());

        PageModel restarted;
        QCOMPARE(restarted.count(), 1);
        QCOMPARE(restarted.current(), 0);
        const QVariantMap page = restarted.page(0);
        QCOMPARE(page.value("rotation").toInt(), 0);
        QCOMPARE(page.value("crop").toRectF(), QRectF(0.5, 0.5, 0.5, 0.5));
        QCOMPARE(page.value("filter").toInt(), int(Page::BlackWhite));
        QCOMPARE(page.value("brightness").toInt(), 100);
        QCOMPARE(page.value("contrast").toInt(), -100);
        QCOMPARE(page.value("threshold").toInt(), 100);
        QCOMPARE(page.value("dpi").toDouble(), 0.0);
        QVERIFY(QFile::exists(outside));
        QFile::remove(outside);
    }

    // A scan too big to decode is turned away with a word, not kept as a
    // page that can never be drawn or exported.
    void oversizedScanIsRefused() {
        PageModel model;
        model.clear();
        QSignalSpy said(&model, &PageModel::message);
        QImageReader::setAllocationLimit(1);
        addScan(model, m_pictures.at(0));
        QImageReader::setAllocationLimit(PageRender::kImageLimitMiB);
        QCOMPARE(model.count(), 0);
        QCOMPARE(said.size(), 1);
        QCOMPARE(QDir(model.sessionDir()).entryList({QStringLiteral("*.png")}, QDir::Files), QStringList{});
    }

    // Until a scanner is chosen, a scan says to run setup, in a way the bar
    // plugin can tell apart.
    void cliNeedsSetup() {
        const CliResult result = cli({QStringLiteral("scan"), QStringLiteral("--json")});
        QCOMPARE(result.code, int(Cli::NotSetUp));
        QCOMPARE(result.json.value("error").toString(), QStringLiteral("setup"));
    }

    // Setup lists the scanners and saves the one picked, and the file type.
    void cliSetup() {
        const CliResult result = cli({QStringLiteral("setup")}, QStringLiteral("2\n2\n"));
        QVERIFY2(result.code == Cli::Ok, qPrintable(result.err));
        QVERIFY(result.out.contains(QStringLiteral("Oma Feeder 9000")));
        QCOMPARE(cli({QStringLiteral("config"), QStringLiteral("get"), QStringLiteral("device")}).out.trimmed(),
                 QStringLiteral("fake:adf"));
        QCOMPARE(cli({QStringLiteral("config"), QStringLiteral("get"), QStringLiteral("format")}).out.trimmed(),
                 QStringLiteral("png"));
        // Input that ends before an answer saves nothing.
        QCOMPARE(cli({QStringLiteral("setup")}, QStringLiteral("1\n")).code, int(Cli::Failed));
        QCOMPARE(cli({QStringLiteral("config"), QStringLiteral("get"), QStringLiteral("device")}).out.trimmed(),
                 QStringLiteral("fake:adf"));
    }

    void cliConfig() {
        const auto set = [](const QString &key, const QString &value) {
            return cli({QStringLiteral("config"), QStringLiteral("set"), key, value}).code;
        };
        QCOMPARE(set(QStringLiteral("device"), QStringLiteral("fake:flatbed")), int(Cli::Ok));
        QCOMPARE(set(QStringLiteral("format"), QStringLiteral("PDF")), int(Cli::Ok));
        QCOMPARE(cli({QStringLiteral("config"), QStringLiteral("get"), QStringLiteral("format")}).out.trimmed(),
                 QStringLiteral("pdf"));
        QCOMPARE(set(QStringLiteral("format"), QStringLiteral("tiff")), int(Cli::Failed));
        QCOMPARE(set(QStringLiteral("filter"), QStringLiteral("sepia")), int(Cli::Failed));
        QCOMPARE(set(QStringLiteral("resolution"), QStringLiteral("lots")), int(Cli::Failed));
        QCOMPARE(set(QStringLiteral("colour"), QStringLiteral("red")), int(Cli::Failed));
        QCOMPARE(set(QStringLiteral("resolution"), QStringLiteral("75")), int(Cli::Ok));
        QVERIFY(cli({QStringLiteral("config")}).out.contains(QStringLiteral("resolution 75")));
    }

    // A scan becomes a finished file: a PDF in Documents, a PNG in Pictures,
    // never over one already there.
    void cliScansToFiles() {
        CliResult result = cli({QStringLiteral("scan"), QStringLiteral("--json")});
        QVERIFY2(result.code == Cli::Ok, qPrintable(result.out + result.err));
        const QString pdf = result.json.value("files").toArray().at(0).toString();
        QCOMPARE(QFileInfo(pdf).absolutePath(), m_home.path() + QStringLiteral("/Documents"));
        QVERIFY(run(QStringLiteral("pdfinfo"), {pdf}).contains(QStringLiteral("Pages:           1")));

        result = cli({QStringLiteral("scan"), QStringLiteral("--json")});
        QCOMPARE(result.json.value("files").toArray().at(0).toString(),
                 QFileInfo(pdf).absolutePath() + u'/' + QFileInfo(pdf).completeBaseName() + QStringLiteral(" (2).pdf"));

        result = cli({QStringLiteral("scan"), QStringLiteral("--json"), QStringLiteral("--format"), QStringLiteral("png"),
                      QStringLiteral("--filter"), QStringLiteral("bw")});
        QVERIFY2(result.code == Cli::Ok, qPrintable(result.out + result.err));
        const QString png = result.json.value("files").toArray().at(0).toString();
        QCOMPARE(QFileInfo(png).absolutePath(), m_home.path() + QStringLiteral("/Pictures"));
        const QImage image(png);
        QVERIFY(!image.isNull());
        QVERIFY(image.allGray());
        QVERIFY(image.width() > 500); // 75 dpi, from the saved setting
    }

    // Every page from the feeder: numbered pictures, or one PDF.
    void cliScansFeeder() {
        qputenv("FAKE_SCAN_PAGES", "2");
        const QString dir = m_out.path() + QStringLiteral("/feeder");
        QVERIFY(QDir().mkpath(dir));
        const CliResult result = cli({QStringLiteral("scan"), QStringLiteral("--json"), QStringLiteral("--device"),
                                      QStringLiteral("fake:adf"), QStringLiteral("--source"), QStringLiteral("adf"),
                                      QStringLiteral("-o"), dir, QStringLiteral("--format"), QStringLiteral("png")});
        qunsetenv("FAKE_SCAN_PAGES");
        QVERIFY2(result.code == Cli::Ok, qPrintable(result.out + result.err));
        QCOMPARE(result.json.value("pages").toInt(), 2);
        const QJsonArray files = result.json.value("files").toArray();
        QCOMPARE(files.size(), 2);
        QVERIFY(files.at(1).toString().endsWith(QStringLiteral("-02.png")));
        QVERIFY(!QImage(files.at(1).toString()).isNull());
        // A setting given on the command line is for that scan only.
        QCOMPARE(cli({QStringLiteral("config"), QStringLiteral("get"), QStringLiteral("device")}).out.trimmed(),
                 QStringLiteral("fake:flatbed"));
    }

    // user-dirs.dirs turns a folder off by setting it to home itself; scans
    // then go to ~/Documents and ~/Pictures, not loose in home.
    void homeAsFolderMeansUnset() {
        const QByteArray home = qgetenv("HOME");
        qputenv("HOME", m_home.path().toLocal8Bit());
        QFile dirs(m_home.path() + QStringLiteral("/config/user-dirs.dirs"));
        QVERIFY(dirs.open(QIODevice::ReadOnly));
        const QByteArray saved = dirs.readAll();
        dirs.close();
        QVERIFY(dirs.open(QIODevice::WriteOnly));
        dirs.write("XDG_DOCUMENTS_DIR=\"$HOME/\"\nXDG_PICTURES_DIR=\"$HOME\"\n");
        dirs.close();

        const QString documents = Exporter::userFolder(QStandardPaths::DocumentsLocation);
        const QString pictures = Exporter::userFolder(QStandardPaths::PicturesLocation);
        const QString existing = Exporter::userFolder(QStandardPaths::DocumentsLocation, true);

        QVERIFY(dirs.open(QIODevice::WriteOnly));
        dirs.write(saved);
        dirs.close();
        qputenv("HOME", home);
        QCOMPARE(documents, m_home.path() + QStringLiteral("/Documents"));
        QCOMPARE(pictures, m_home.path() + QStringLiteral("/Pictures"));
        // The save dialog only opens in a folder that is there.
        QCOMPARE(existing, QFileInfo::exists(m_home.path() + QStringLiteral("/Documents"))
                               ? m_home.path() + QStringLiteral("/Documents") : m_home.path());
    }

    void cliScanErrors() {
        qputenv("FAKE_SCAN_FAIL", "busy");
        CliResult result = cli({QStringLiteral("scan"), QStringLiteral("--json")});
        qunsetenv("FAKE_SCAN_FAIL");
        QCOMPARE(result.code, int(Cli::Busy));
        QCOMPARE(result.json.value("error").toString(), QStringLiteral("busy"));

        result = cli({QStringLiteral("scan"), QStringLiteral("--mode"), QStringLiteral("Sepia")});
        QCOMPARE(result.code, int(Cli::Failed));
        QVERIFY(result.err.contains(QStringLiteral("Color, Gray, Lineart")));

        result = cli({QStringLiteral("scan"), QStringLiteral("-o"), m_out.path() + QStringLiteral("/x.tiff")});
        QCOMPARE(result.code, int(Cli::Failed));
    }

    void cleanupTestCase() {
        PageModel model;
        model.clear();
        QCOMPARE(QDir(model.sessionDir()).entryList(QDir::Files), QStringList{QStringLiteral("state.json")});
    }
};

QTEST_MAIN(OmaScanTest)
#include "tst_omascan.moc"
