#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QRectF>
#include <QString>
#include <QUrl>

// One page of the document. The scanned or imported picture is never touched;
// everything done to it is a setting here, applied when it is drawn. That keeps
// every edit reversible and lets undo be a copy of this struct.
struct Page {
    enum Filter { Original = 0, Enhanced, Greyscale, BlackWhite };

    int id = 0;
    QString source;             // the untouched picture, inside the session folder
    int rotation = 0;           // clockwise: 0, 90, 180 or 270
    QRectF crop{0, 0, 1, 1};    // unit rect, measured on the rotated picture
    int filter = Enhanced;
    int brightness = 0;         // -100 … 100
    int contrast = 0;           // -100 … 100
    int threshold = 50;         // black & white only: 0 … 100
    qreal dpi = 0;              // 0 when the picture does not say
    QSize sourceSize;           // after EXIF orientation, before rotation
    int revision = 0;           // bumps on every change, so Image sources change
};

// Everything that turns a Page into pixels. Safe to call from any thread.
namespace PageRender {
// maxEdge limits the longer side of the result (0 = full resolution).
// `withCrop` false draws the whole rotated picture, for the crop editor.
QImage render(const Page &page, int maxEdge, bool withCrop = true);
// Output size in pixels at full resolution.
QSize outputSize(const Page &page);
void dropCache(const QString &source);
// The most a decoded picture may take, in MiB. Qt's default (256) cannot hold
// a colour page at 1200 dpi; this can, up to Legal size.
constexpr int kImageLimitMiB = 1024;
}

class PageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QString sessionDir READ sessionDir CONSTANT)
    // Pages brought back from the last run, because they were never exported.
    Q_PROPERTY(int restoredCount READ restoredCount CONSTANT)

public:
    enum Roles {
        PageIdRole = Qt::UserRole + 1,
        ImageUrlRole,
        FullUrlRole,
        RotationRole,
        FilterRole,
        BrightnessRole,
        ContrastRole,
        ThresholdRole,
        CropRole,
        PixelWidthRole,
        PixelHeightRole,
        FullWidthRole,
        FullHeightRole,
        DpiRole,
        RevisionRole,
    };

    explicit PageModel(QObject *parent = nullptr);

    // Where the pages of the running document are kept.
    static QString defaultDir();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_pages.size()); }
    int current() const { return m_current; }
    void setCurrent(int index);
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoText() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoText() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    QString sessionDir() const { return m_dir; }
    int restoredCount() const { return m_restoredCount; }

    // A copy of the current pages, for export on another thread.
    QList<Page> pages() const { return m_pages; }
    // Thread-safe lookup for the image provider.
    bool pageById(int id, Page *out) const;

    // A fresh file name inside the session folder.
    QString newSourcePath(const QString &suffix) const;

    Q_INVOKABLE QVariantMap page(int index) const;

    // Adds a scanned picture as the last page; the file moves into the session.
    Q_INVOKABLE void addScan(const QString &path, qreal dpi);

    // The whole document has been written out. Until something changes, the
    // next run starts empty instead of bringing these pages back.
    void markExported();

    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void duplicate(int index);
    Q_INVOKABLE void rotate(int index, int degrees);
    Q_INVOKABLE void setFilter(int index, int filter);
    Q_INVOKABLE void setCrop(int index, qreal x, qreal y, qreal w, qreal h);
    Q_INVOKABLE void resetCrop(int index);
    // Live while a slider moves: call checkpoint() once when the drag starts.
    Q_INVOKABLE void setAdjustment(int index, const QString &name, int value);
    Q_INVOKABLE void resetAdjustments(int index);
    Q_INVOKABLE void applyLookToAll(int index);
    Q_INVOKABLE void clear();

    Q_INVOKABLE void checkpoint(const QString &label);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Default look for new pages, set from the inspector.
    Q_INVOKABLE void setDefaultFilter(int filter) { m_defaultFilter = filter; }

signals:
    void countChanged();
    void currentChanged();
    void historyChanged();
    void message(const QString &text);

private:
    struct Snapshot { QList<Page> pages; int current; QString label; };

    int nextId() { return ++m_lastId; }
    bool valid(int index) const { return index >= 0 && index < m_pages.size(); }
    void append(Page page);
    void touch(int index);
    void resetTo(const QList<Page> &pages, int current);
    void syncMirror();
    void save() const;
    void restore();
    void collectGarbage() const;

    QString m_dir;
    QList<Page> m_pages;
    int m_current = -1;
    int m_lastId = 0;
    int m_defaultFilter = Page::Enhanced;
    int m_restoredCount = 0;
    bool m_exported = false;
    QList<Snapshot> m_undo;
    QList<Snapshot> m_redo;

    mutable QMutex m_mirrorLock;
    QHash<int, Page> m_mirror;
};
