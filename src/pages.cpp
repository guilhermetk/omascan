#include "pages.h"

#include <QCache>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTransform>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ── Rendering ────────────────────────────────────────────────────────────────

namespace {

// Decoded pictures, at a few fixed sizes, shared by the thumbnails, the preview
// and every revision of a page. A slider drag re-renders from here rather than
// decoding a 30 MB scan again.
QMutex cacheLock;
QCache<QString, QImage> cache(320 * 1024); // in KiB

int bucketFor(int needed) {
    for (int bucket : {480, 1200, 2400})
        if (needed <= bucket)
            return bucket;
    return 0; // full size
}

QImage loadSource(const QString &path, int bucket) {
    const QString key = path + u'@' + QString::number(bucket);
    {
        QMutexLocker lock(&cacheLock);
        if (QImage *hit = cache.object(key))
            return *hit;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize native = reader.size();
    if (bucket > 0 && native.isValid() && std::max(native.width(), native.height()) > bucket)
        reader.setScaledSize(native.scaled(bucket, bucket, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull())
        return image;

    // Transparent pixels land on paper, not on black.
    if (image.hasAlphaChannel()) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, image);
        image = flat;
    } else if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_Grayscale8) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QMutexLocker lock(&cacheLock);
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// Brightness and contrast as one lookup table, applied before any filter.
std::array<uchar, 256> toneTable(int brightness, int contrast) {
    const double gain = contrast >= 0 ? 1.0 + contrast / 50.0 : 1.0 + contrast / 100.0;
    const double lift = brightness * 1.28;
    std::array<uchar, 256> lut{};
    for (int v = 0; v < 256; ++v)
        lut[v] = uchar(std::clamp(int(std::lround((v - 128) * gain + 128 + lift)), 0, 255));
    return lut;
}

QImage toGrey(const QImage &image) {
    return image.format() == QImage::Format_Grayscale8
        ? image : image.convertToFormat(QImage::Format_Grayscale8);
}

int percentile(const std::array<qint64, 256> &histogram, qint64 total, double fraction) {
    const qint64 target = qint64(total * fraction);
    qint64 seen = 0;
    for (int v = 0; v < 256; ++v) {
        seen += histogram[v];
        if (seen > target)
            return v;
    }
    return 255;
}

// "Enhanced": paper to white, ink to black. Paper is most of a document page,
// so a low-middle percentile of each channel sits on the paper; mapping that
// to white per channel also takes out the paper's colour cast. The black point
// comes from the darkest few pixels, but never closer than 96 levels to the
// paper, since a page of text is barely 1% ink.
struct Levels { std::array<uchar, 256> r, g, b; };

Levels paperLevels(const QImage &image) {
    std::array<std::array<qint64, 256>, 3> histogram{};
    qint64 total = 0;
    const bool grey = image.format() == QImage::Format_Grayscale8;
    for (int y = 0; y < image.height(); y += 2) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); x += 2) {
            if (grey) {
                for (auto &h : histogram) ++h[line[x]];
            } else {
                const QRgb p = reinterpret_cast<const QRgb *>(line)[x];
                ++histogram[0][qRed(p)];
                ++histogram[1][qGreen(p)];
                ++histogram[2][qBlue(p)];
            }
            ++total;
        }
    }

    std::array<int, 3> white{};
    int black = 255;
    for (int c = 0; c < 3; ++c) {
        white[c] = percentile(histogram[c], total, 0.40);
        // Mostly dark: a photo, not paper. Stretch gently instead.
        if (white[c] < 140)
            white[c] = percentile(histogram[c], total, 0.98);
        white[c] = std::max(white[c], 64);
        black = std::min(black, percentile(histogram[c], total, 0.002));
    }
    black = std::min(black, *std::min_element(white.cbegin(), white.cend()) - 96);
    black = std::max(black, 0);

    Levels levels;
    std::array<std::array<uchar, 256> *, 3> tables{&levels.r, &levels.g, &levels.b};
    for (int c = 0; c < 3; ++c) {
        for (int v = 0; v < 256; ++v) {
            const double t = std::clamp((v - black) / double(white[c] - black), 0.0, 1.0);
            // A slight gamma keeps thin strokes from washing out.
            (*tables[c])[v] = uchar(std::lround(std::pow(t, 1.2) * 255));
        }
    }
    return levels;
}

void applyLevels(QImage &image, const Levels &levels) {
    if (image.format() == QImage::Format_Grayscale8) {
        for (int y = 0; y < image.height(); ++y) {
            uchar *line = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x)
                line[x] = levels.g[line[x]];
        }
        return;
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            line[x] = qRgb(levels.r[qRed(p)], levels.g[qGreen(p)], levels.b[qBlue(p)]);
        }
    }
}

void applyTable(QImage &image, const std::array<uchar, 256> &lut) {
    if (image.format() == QImage::Format_Grayscale8) {
        for (int y = 0; y < image.height(); ++y) {
            uchar *line = image.scanLine(y);
            for (int x = 0; x < image.width(); ++x)
                line[x] = lut[line[x]];
        }
        return;
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = line[x];
            line[x] = qRgb(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)]);
        }
    }
}

// Black & white with a local threshold (Bradley): a pixel is ink when it is
// darker than its neighbourhood by some margin. Survives uneven light and the
// shadow of a book's spine, where a single global cut does not.
QImage adaptiveThreshold(const QImage &grey, int threshold) {
    const int w = grey.width(), h = grey.height();
    QImage out(w, h, QImage::Format_Grayscale8);
    if (w == 0 || h == 0)
        return out;

    std::vector<quint32> integral(size_t(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        const uchar *line = grey.constScanLine(y);
        quint32 row = 0;
        for (int x = 0; x < w; ++x) {
            row += line[x];
            integral[size_t(y + 1) * (w + 1) + x + 1] = integral[size_t(y) * (w + 1) + x + 1] + row;
        }
    }

    const int half = std::max(4, std::max(w, h) / 24);
    const double margin = 0.30 * (1.0 - threshold / 100.0);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - half), y1 = std::min(h, y + half + 1);
        const uchar *in = grey.constScanLine(y);
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - half), x1 = std::min(w, x + half + 1);
            // The table wraps past 2^32 on big scans (600 dpi and up); the
            // box's own sum never does, so 32-bit wrapping arithmetic still
            // gets it right. Widening before subtracting would not.
            const quint32 sum = integral[size_t(y1) * (w + 1) + x1]
                              - integral[size_t(y0) * (w + 1) + x1]
                              - integral[size_t(y1) * (w + 1) + x0]
                              + integral[size_t(y0) * (w + 1) + x0];
            const quint64 area = quint64(x1 - x0) * (y1 - y0);
            const bool ink = double(in[x]) * area <= sum * (1.0 - margin) || in[x] < 40;
            dst[x] = ink ? 0 : 255;
        }
    }
    return out;
}

QSize rotated(QSize size, int rotation) {
    return rotation == 90 || rotation == 270 ? size.transposed() : size;
}

QRect cropRect(const QRectF &crop, QSize size) {
    const QRect rect(int(std::lround(crop.x() * size.width())),
                     int(std::lround(crop.y() * size.height())),
                     int(std::lround(crop.width() * size.width())),
                     int(std::lround(crop.height() * size.height())));
    const QRect bounded = rect.intersected(QRect(QPoint(0, 0), size));
    return bounded.isEmpty() ? QRect(QPoint(0, 0), size) : bounded;
}

} // namespace

QImage PageRender::render(const Page &page, int maxEdge, bool withCrop) {
    const QRectF crop = withCrop ? page.crop : QRectF(0, 0, 1, 1);
    int bucket = 0;
    if (maxEdge > 0) {
        const double visible = std::max(crop.width(), crop.height());
        bucket = bucketFor(int(std::ceil(maxEdge / std::max(visible, 0.05))));
    }

    QImage image = loadSource(page.source, bucket);
    if (image.isNull())
        return image;

    if (page.rotation % 360 != 0)
        image = image.transformed(QTransform().rotate(page.rotation));
    if (withCrop)
        image = image.copy(cropRect(crop, image.size()));
    else
        image = image.copy(); // detach from the cache before editing in place
    if (maxEdge > 0 && std::max(image.width(), image.height()) > maxEdge)
        image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const auto tone = toneTable(page.brightness, page.contrast);
    switch (page.filter) {
    case Page::Original:
        applyTable(image, tone);
        break;
    case Page::Enhanced:
        applyLevels(image, paperLevels(image));
        applyTable(image, tone);
        break;
    case Page::Greyscale:
        image = toGrey(image);
        applyTable(image, tone);
        break;
    case Page::BlackWhite:
        image = toGrey(image);
        applyTable(image, tone);
        image = adaptiveThreshold(image, page.threshold);
        break;
    }
    return image;
}

QSize PageRender::outputSize(const Page &page) {
    const QSize size = rotated(page.sourceSize, page.rotation);
    return cropRect(page.crop, size).size();
}

void PageRender::dropCache(const QString &source) {
    QMutexLocker lock(&cacheLock);
    for (const QString &key : cache.keys())
        if (key.startsWith(source + u'@'))
            cache.remove(key);
}

// ── Model ────────────────────────────────────────────────────────────────────

namespace {
constexpr int kUndoDepth = 100;

QSize orientedSize(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    const bool quarterTurn = reader.transformation() & QImageIOHandler::TransformationRotate90;
    return quarterTurn ? size.transposed() : size;
}

// Scans are documents people keep private: nobody else on the machine reads them.
void makePrivate(const QString &path) {
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

// A resolution that can size a sheet of paper; anything else counts as unknown.
qreal saneDpi(qreal dpi) {
    return std::isfinite(dpi) && dpi >= 10 && dpi <= 20000 ? dpi : 0;
}

// The pictures the session keeps, as opposed to state.json and anything else.
bool isPicture(const QString &name) {
    return name.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".tiff"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".pnm"), Qt::CaseInsensitive);
}

// Whether the picture fits under the decoder's memory limit once unpacked.
bool fitsInMemory(QSize size) {
    const int limit = QImageReader::allocationLimit();
    return limit <= 0 || qint64(size.width()) * size.height() * 4 <= qint64(limit) * 1024 * 1024;
}

// Whether a state file has the shape save() writes. One that does not is
// treated like one that cannot be read at all: its time says nothing.
bool isState(const QJsonObject &state) {
    const QJsonValue exported = state.value("exported");
    const QJsonValue current = state.value("current");
    return state.value("version").toInt() == 1 && state.value("pages").isArray()
        && (exported.isUndefined() || exported.isBool())
        && (current.isUndefined() || current.isDouble());
}

QJsonObject toJson(const Page &p) {
    return {
        {"id", p.id}, {"source", QFileInfo(p.source).fileName()}, {"rotation", p.rotation},
        {"crop", QJsonArray{p.crop.x(), p.crop.y(), p.crop.width(), p.crop.height()}},
        {"filter", p.filter}, {"brightness", p.brightness}, {"contrast", p.contrast},
        {"threshold", p.threshold}, {"dpi", p.dpi},
        {"width", p.sourceSize.width()}, {"height", p.sourceSize.height()},
    };
}
}

PageModel::PageModel(QObject *parent) : QAbstractListModel(parent) {
    // Pages live on disk from the moment they are scanned, so closing the
    // window (or a crash) never loses a scan that has not been exported yet.
    m_dir = defaultDir();
    QDir().mkpath(m_dir);
    makePrivate(m_dir);
    restore();
}

QString PageModel::defaultDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/session");
}

int PageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_pages.size());
}

QVariant PageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !valid(index.row()))
        return {};
    const Page &p = m_pages.at(index.row());
    const QString base = QStringLiteral("image://page/%1/%2").arg(p.id).arg(p.revision);
    switch (role) {
    case PageIdRole: return p.id;
    case ImageUrlRole: return base;
    case FullUrlRole: return base + QStringLiteral("/full");
    case RotationRole: return p.rotation;
    case FilterRole: return p.filter;
    case BrightnessRole: return p.brightness;
    case ContrastRole: return p.contrast;
    case ThresholdRole: return p.threshold;
    case CropRole: return p.crop;
    case PixelWidthRole: return PageRender::outputSize(p).width();
    case PixelHeightRole: return PageRender::outputSize(p).height();
    case FullWidthRole: return rotated(p.sourceSize, p.rotation).width();
    case FullHeightRole: return rotated(p.sourceSize, p.rotation).height();
    case DpiRole: return p.dpi;
    case RevisionRole: return p.revision;
    }
    return {};
}

QHash<int, QByteArray> PageModel::roleNames() const {
    return {
        {PageIdRole, "pageId"}, {ImageUrlRole, "imageUrl"}, {FullUrlRole, "fullUrl"},
        {RotationRole, "rotation"}, {FilterRole, "filter"}, {BrightnessRole, "brightness"},
        {ContrastRole, "contrast"}, {ThresholdRole, "threshold"}, {CropRole, "crop"},
        {PixelWidthRole, "pixelWidth"}, {PixelHeightRole, "pixelHeight"},
        {FullWidthRole, "fullWidth"}, {FullHeightRole, "fullHeight"}, {DpiRole, "dpi"},
        {RevisionRole, "revision"},
    };
}

void PageModel::setCurrent(int index) {
    index = m_pages.isEmpty() ? -1 : std::clamp(index, 0, int(m_pages.size()) - 1);
    if (index == m_current)
        return;
    m_current = index;
    emit currentChanged();
    save();
}

bool PageModel::pageById(int id, Page *out) const {
    QMutexLocker lock(&m_mirrorLock);
    const auto it = m_mirror.constFind(id);
    if (it == m_mirror.constEnd())
        return false;
    *out = *it;
    return true;
}

QString PageModel::newSourcePath(const QString &suffix) const {
    return m_dir + u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces).left(13)
           + u'.' + suffix;
}

int PageModel::indexOf(int id) const {
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages.at(i).id == id)
            return i;
    return -1;
}

QVariantMap PageModel::page(int index) const {
    QVariantMap map;
    if (!valid(index))
        return map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(this->index(index), it.key()));
    return map;
}

void PageModel::append(Page page) {
    page.id = nextId();
    page.filter = m_defaultFilter;
    const int row = int(m_pages.size());
    ++m_changes;
    beginInsertRows({}, row, row);
    m_pages.append(page);
    endInsertRows();
    syncMirror();
    emit countChanged();
    m_current = row;
    emit currentChanged();
}

QString PageModel::readScan(const QString &path, qreal dpi, Page *page) {
    page->source = path;
    page->dpi = saneDpi(dpi);
    page->sourceSize = orientedSize(path);
    if (!page->sourceSize.isValid())
        return tr("The scanner sent a picture that could not be read.");
    if (!fitsInMemory(page->sourceSize))
        return tr("That scan is too large to work with. Try a lower resolution.");
    return {};
}

void PageModel::addScan(const QString &path, qreal dpi) {
    // Scans are written into incoming/ so a half-written file is never taken
    // for a page; once whole, they join the session.
    QString source = path;
    if (QFileInfo(path).absolutePath() != QFileInfo(m_dir).absoluteFilePath()) {
        // Only a file inside the session survives a restart, so a scan that
        // cannot be moved there is copied, and one that cannot be copied
        // either is not taken in as if it were safe.
        source = newSourcePath(QFileInfo(path).suffix());
        if (!QFile::rename(path, source)) {
            if (!QFile::copy(path, source)) {
                QFile::remove(source);
                emit message(tr("The scan could not be saved. Is the disk full?"));
                return;
            }
            QFile::remove(path);
        }
    }
    Page page;
    if (const QString error = readScan(source, dpi, &page); !error.isEmpty()) {
        QFile::remove(source);
        emit message(error);
        return;
    }
    checkpoint(tr("Scan"));
    append(page);
    save();
}

void PageModel::touch(int index) {
    Page &p = m_pages[index];
    ++p.revision;
    m_exported = false;
    ++m_changes;
    syncMirror();
    emit dataChanged(this->index(index), this->index(index));
    save();
}

void PageModel::remove(int index) {
    if (!valid(index))
        return;
    checkpoint(tr("Delete page"));
    beginRemoveRows({}, index, index);
    m_pages.removeAt(index);
    endRemoveRows();
    syncMirror();
    emit countChanged();
    const int next = m_pages.isEmpty() ? -1 : std::min(index, int(m_pages.size()) - 1);
    m_current = -2; // force the signal: the row under `current` changed
    setCurrent(next);
    save();
}

void PageModel::move(int from, int to) {
    if (!valid(from) || !valid(to) || from == to)
        return;
    checkpoint(tr("Move page"));
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_pages.move(from, to);
    endMoveRows();
    m_current = to;
    emit currentChanged();
    save();
}

void PageModel::duplicate(int index) {
    if (!valid(index))
        return;
    checkpoint(tr("Duplicate page"));
    Page copy = m_pages.at(index);
    copy.id = nextId();
    copy.revision = 0;
    beginInsertRows({}, index + 1, index + 1);
    m_pages.insert(index + 1, copy);
    endInsertRows();
    syncMirror();
    emit countChanged();
    setCurrent(index + 1);
    save();
}

void PageModel::rotate(int index, int degrees) {
    if (!valid(index) || degrees % 90 != 0 || degrees % 360 == 0)
        return;
    checkpoint(degrees > 0 ? tr("Rotate right") : tr("Rotate left"));
    Page &p = m_pages[index];
    p.rotation = ((p.rotation + degrees) % 360 + 360) % 360;
    // Keep the same part of the picture cropped: turn the crop with it.
    const QRectF c = p.crop;
    if (((degrees % 360) + 360) % 360 == 90)
        p.crop = QRectF(1 - c.y() - c.height(), c.x(), c.height(), c.width());
    else if (((degrees % 360) + 360) % 360 == 270)
        p.crop = QRectF(c.y(), 1 - c.x() - c.width(), c.height(), c.width());
    else if (((degrees % 360) + 360) % 360 == 180)
        p.crop = QRectF(1 - c.x() - c.width(), 1 - c.y() - c.height(), c.width(), c.height());
    touch(index);
}

void PageModel::setFilter(int index, int filter) {
    if (!valid(index) || m_pages.at(index).filter == filter)
        return;
    checkpoint(tr("Change look"));
    m_pages[index].filter = std::clamp(filter, 0, int(Page::BlackWhite));
    m_defaultFilter = m_pages[index].filter;
    touch(index);
}

void PageModel::setCrop(int index, qreal x, qreal y, qreal w, qreal h) {
    if (!valid(index))
        return;
    const QRectF crop = QRectF(x, y, w, h).intersected(QRectF(0, 0, 1, 1));
    if (crop.width() < 0.02 || crop.height() < 0.02 || crop == m_pages.at(index).crop)
        return;
    checkpoint(tr("Crop"));
    m_pages[index].crop = crop;
    touch(index);
}

void PageModel::resetCrop(int index) {
    if (!valid(index) || m_pages.at(index).crop == QRectF(0, 0, 1, 1))
        return;
    checkpoint(tr("Remove crop"));
    m_pages[index].crop = QRectF(0, 0, 1, 1);
    touch(index);
}

void PageModel::setAdjustment(int index, const QString &name, int value) {
    if (!valid(index))
        return;
    Page &p = m_pages[index];
    int *field = name == u"brightness" ? &p.brightness
               : name == u"contrast" ? &p.contrast
               : name == u"threshold" ? &p.threshold : nullptr;
    value = std::clamp(value, field == &p.threshold ? 0 : -100, 100);
    if (!field || *field == value)
        return;
    *field = value;
    touch(index);
}

void PageModel::resetAdjustments(int index) {
    if (!valid(index))
        return;
    const Page &now = m_pages.at(index);
    if (now.brightness == 0 && now.contrast == 0 && now.threshold == 50)
        return;
    // The checkpoint shares the list; only a reference taken after it writes
    // to a copy of its own, not into the undo step.
    checkpoint(tr("Reset adjustments"));
    Page &p = m_pages[index];
    p.brightness = 0;
    p.contrast = 0;
    p.threshold = 50;
    touch(index);
}

void PageModel::applyLookToAll(int index) {
    if (!valid(index))
        return;
    checkpoint(tr("Apply look to all pages"));
    const Page &look = m_pages.at(index);
    for (int i = 0; i < m_pages.size(); ++i) {
        if (i == index)
            continue;
        Page &p = m_pages[i];
        p.filter = look.filter;
        p.brightness = look.brightness;
        p.contrast = look.contrast;
        p.threshold = look.threshold;
        ++p.revision;
    }
    syncMirror();
    emit dataChanged(this->index(0), this->index(int(m_pages.size()) - 1));
    save();
}

void PageModel::clear() {
    m_exported = false;
    ++m_changes;
    beginResetModel();
    m_pages.clear();
    endResetModel();
    m_undo.clear();
    m_redo.clear();
    syncMirror();
    emit countChanged();
    emit historyChanged();
    m_current = -2;
    setCurrent(-1);
    save();
    collectGarbage();
}

void PageModel::markExported(quint64 changes) {
    if (changes != m_changes)
        return;
    m_exported = true;
    save();
}

void PageModel::checkpoint(const QString &label) {
    m_exported = false;
    ++m_changes;
    m_undo.append({m_pages, m_current, label});
    if (m_undo.size() > kUndoDepth)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

void PageModel::resetTo(const QList<Page> &pages, int current) {
    beginResetModel();
    m_pages = pages;
    ++m_changes;
    // Every restored page gets a new revision so its Image reloads.
    for (Page &p : m_pages)
        p.revision = ++m_lastId;
    endResetModel();
    syncMirror();
    emit countChanged();
    m_current = -2;
    setCurrent(current);
    save();
}

void PageModel::undo() {
    if (m_undo.isEmpty())
        return;
    Snapshot s = m_undo.takeLast();
    m_exported = false;
    m_redo.append({m_pages, m_current, s.label});
    resetTo(s.pages, s.current);
    emit historyChanged();
}

void PageModel::redo() {
    if (m_redo.isEmpty())
        return;
    Snapshot s = m_redo.takeLast();
    m_exported = false;
    m_undo.append({m_pages, m_current, s.label});
    resetTo(s.pages, s.current);
    emit historyChanged();
}

void PageModel::syncMirror() {
    QMutexLocker lock(&m_mirrorLock);
    m_mirror.clear();
    for (const Page &p : std::as_const(m_pages))
        m_mirror.insert(p.id, p);
    // Pages only reachable through undo still need drawing if they come back,
    // and the provider may be asked for them while the model resets.
    for (const Snapshot &s : std::as_const(m_undo))
        for (const Page &p : s.pages)
            if (!m_mirror.contains(p.id))
                m_mirror.insert(p.id, p);
}

void PageModel::save() {
    QJsonArray pages;
    for (const Page &p : m_pages)
        pages.append(toJson(p));
    const QJsonObject state{{"version", 1}, {"current", m_current}, {"exported", m_exported},
                            {"pages", pages}};

    const QString path = m_dir + QStringLiteral("/state.json");
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
        m_saveFailed = false;
        return;
    }
    // The state left on disk describes an older document, and may call it
    // exported. With none at all, the next run keeps every picture instead.
    QFile::remove(path);
    if (!m_saveFailed)
        emit message(tr("Your changes could not be saved. Is the disk full?"));
    m_saveFailed = true;
}

void PageModel::restore() {
    QFile file(m_dir + QStringLiteral("/state.json"));
    // Pictures newer than the last saved state belong to pages that state
    // never heard of: a crash, or a full disk, between a scan and its save.
    // With no readable state at all, every picture is such a page.
    QDateTime savedAt;
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        const QJsonObject state = document.object();
        if (error.error == QJsonParseError::NoError && document.isObject() && isState(state)) {
            savedAt = QFileInfo(file).lastModified();
            // An exported document is finished: start the new run empty, and
            // let collectGarbage() below remove its pictures.
            const QJsonArray saved = state.value("exported").toBool() ? QJsonArray() : state.value("pages").toArray();
            for (const QJsonValue &v : saved) {
                const QJsonObject o = v.toObject();
                // Only a bare name: the file is inside the session or nowhere.
                const QString name = QFileInfo(o.value("source").toString()).fileName();
                Page p;
                p.source = m_dir + u'/' + name;
                if (name.isEmpty() || !isPicture(name) || !QFileInfo(p.source).isFile())
                    continue;
                p.id = nextId();
                p.rotation = (o.value("rotation").toInt() / 90 % 4 + 4) % 4 * 90;
                const QJsonArray c = o.value("crop").toArray();
                const QRectF crop = c.size() == 4
                    ? QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble())
                          .intersected(QRectF(0, 0, 1, 1))
                    : QRectF();
                if (crop.width() >= 0.02 && crop.height() >= 0.02)
                    p.crop = crop;
                p.filter = std::clamp(o.value("filter").toInt(Page::Enhanced), 0, int(Page::BlackWhite));
                p.brightness = std::clamp(o.value("brightness").toInt(), -100, 100);
                p.contrast = std::clamp(o.value("contrast").toInt(), -100, 100);
                p.threshold = std::clamp(o.value("threshold").toInt(50), 0, 100);
                p.dpi = saneDpi(o.value("dpi").toDouble());
                p.sourceSize = QSize(o.value("width").toInt(), o.value("height").toInt());
                if (p.sourceSize.isEmpty())
                    p.sourceSize = orientedSize(p.source);
                if (p.sourceSize.isEmpty())
                    continue;
                m_pages.append(p);
            }
            m_current = state.value("current").toInt();
        }
    }

    QSet<QString> known;
    for (const Page &p : std::as_const(m_pages))
        known.insert(QFileInfo(p.source).fileName());
    const QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo &info : files) {
        if (!isPicture(info.fileName()) || known.contains(info.fileName())
                || (savedAt.isValid() && info.lastModified() <= savedAt))
            continue;
        Page p;
        p.id = nextId();
        p.source = info.absoluteFilePath();
        p.sourceSize = orientedSize(p.source);
        if (p.sourceSize.isEmpty())
            continue;
        m_pages.append(p);
        m_current = int(m_pages.size()) - 1;
    }

    m_current = m_pages.isEmpty() ? -1 : std::clamp(m_current, 0, int(m_pages.size()) - 1);
    m_restoredCount = int(m_pages.size());
    syncMirror();
    if (!m_pages.isEmpty() || savedAt.isValid())
        save();
    // Nothing is listening yet: a failure here is said at the next one.
    m_saveFailed = false;
    collectGarbage();
}

// Pictures nothing refers to any more: deleted pages from an earlier session.
// Undo only reaches back within one session, so they are safe to remove.
void PageModel::collectGarbage() const {
    QSet<QString> keep;
    for (const Page &p : m_pages)
        keep.insert(QFileInfo(p.source).fileName());
    for (const Snapshot &s : m_undo)
        for (const Page &p : s.pages)
            keep.insert(QFileInfo(p.source).fileName());
    const QDir dir(m_dir);
    for (const QString &name : dir.entryList(QDir::Files)) {
        if (name == QStringLiteral("state.json") || keep.contains(name))
            continue;
        PageRender::dropCache(dir.filePath(name));
        QFile::remove(dir.filePath(name));
    }
}
