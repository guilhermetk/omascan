#include "pageimageprovider.h"

#include "pages.h"

#include <algorithm>

PageImageProvider::PageImageProvider(const PageModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading),
      m_model(model) {}

QImage PageImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QStringList parts = id.split(u'/');
    Page page;
    if (parts.isEmpty() || !m_model->pageById(parts.first().toInt(), &page))
        return {};

    const bool full = parts.size() > 2 && parts.at(2) == u"full";
    const int maxEdge = requestedSize.isValid()
        ? std::max(requestedSize.width(), requestedSize.height()) : 2400;
    const QImage image = PageRender::render(page, maxEdge, !full);
    if (size)
        *size = image.size();
    return image;
}
