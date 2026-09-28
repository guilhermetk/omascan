#pragma once

#include <QQuickImageProvider>

class PageModel;

// image://page/<id>/<revision>[/full]
// The revision is only there to change the URL when the page changes. `full`
// draws the whole rotated picture, ignoring the crop, for the crop editor.
class PageImageProvider : public QQuickImageProvider {
public:
    explicit PageImageProvider(const PageModel *model);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const PageModel *m_model;
};
