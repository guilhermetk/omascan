#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// The XDG desktop portal's file chooser: on Omarchy it is the same picker
// every other app uses. Calls return at once; the answer arrives as a signal.
// When there is no portal, `failed` fires and the window falls back to Qt's.
class FileChooser : public QObject {
    Q_OBJECT

public:
    explicit FileChooser(QObject *parent = nullptr);

    // `patterns` are globs such as "*.pdf"; empty means any file.
    Q_INVOKABLE void saveFile(const QString &title, const QString &suggestedName,
                              const QString &filterName, const QStringList &patterns);

signals:
    void selected(const QList<QUrl> &urls);
    void canceled();
    void failed();

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    bool request(const QString &method, const QString &title, QVariantMap options);
    void disconnectPending();

    QString m_pending;
};
