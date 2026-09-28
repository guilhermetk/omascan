// The model and the exporter, without a window: pages in, edits, undo, what
// survives a restart, and every export format written and readable.

#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

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
        qputenv("XDG_DATA_HOME", m_home.path().toLocal8Bit());
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

    void cleanupTestCase() {
        PageModel model;
        model.clear();
        QCOMPARE(QDir(model.sessionDir()).entryList(QDir::Files), QStringList{QStringLiteral("state.json")});
    }
};

QTEST_MAIN(OmaScanTest)
#include "tst_omascan.moc"
