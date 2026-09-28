#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <atomic>

#include "pages.h"

// Writes the document out: a PDF (optionally with a text layer from Tesseract)
// or one picture per page. Work runs on a thread of its own; the window stays
// usable and the progress comes back as properties.
class Exporter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    Q_PROPERTY(QStringList ocrLanguages READ ocrLanguages CONSTANT)
    Q_PROPERTY(QString defaultOcrLanguage READ defaultOcrLanguage CONSTANT)

public:
    explicit Exporter(PageModel *model, QObject *parent = nullptr);
    ~Exporter() override;

    bool busy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool ocrAvailable() const { return !m_tesseract.isEmpty() && !m_languages.isEmpty(); }
    QStringList ocrLanguages() const { return m_languages; }
    QString defaultOcrLanguage() const;

    // options: paper ("match"|"A4"|"Letter"|"Legal"), quality ("best"|"balanced"|"small"),
    //          ocr (bool), language, scope ("all"|"current")
    Q_INVOKABLE void exportPdf(const QUrl &file, const QVariantMap &options);
    // options: format ("png"|"jpeg"), scope ("all"|"current")
    Q_INVOKABLE void exportImages(const QUrl &file, const QVariantMap &options);
    Q_INVOKABLE void cancel() { m_cancel = true; }

    Q_INVOKABLE void open(const QUrl &file) const;
    Q_INVOKABLE void showInFolder(const QUrl &file) const;
    // A file name that does not exist yet, for the save dialog to suggest.
    Q_INVOKABLE QString suggestedName(const QString &extension) const;

signals:
    void busyChanged();
    void progressChanged();
    void finished(bool ok, const QString &message, const QUrl &file);

private:
    QList<Page> selectedPages(const QVariantMap &options) const;
    void run(std::function<QString()> job, const QUrl &file, bool wholeDocument);
    void report(qreal progress, const QString &status);

    // Each returns an error message, or empty on success.
    QString writePdf(const QList<Page> &pages, const QString &path, const QVariantMap &options);
    QString writeOcrPdf(const QList<Page> &pages, const QString &path, const QVariantMap &options);
    QString writeImages(const QList<Page> &pages, const QString &path, const QVariantMap &options);

    PageModel *m_model;
    QString m_tesseract;
    QStringList m_languages;
    bool m_busy = false;
    qreal m_progress = 0;
    QString m_status;
    std::atomic<bool> m_cancel{false};
    class QThread *m_thread = nullptr;
};
