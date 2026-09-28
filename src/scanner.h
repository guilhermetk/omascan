#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSizeF>
#include <QStringList>
#include <QVariantList>

// The scanner, through SANE's own command line tool. scanimage is what every
// SANE backend is tested against, it runs out of process (a wedged USB driver
// cannot take the window with it) and cancelling is a signal away.
//
// OMASCAN_SCANIMAGE points at another program with the same interface; the
// fake in bin/ uses that to exercise the whole flow without hardware.
class Scanner : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)
    Q_PROPERTY(bool loadingOptions READ loadingOptions NOTIFY optionsChanged)
    Q_PROPERTY(QStringList modes READ modes NOTIFY optionsChanged)
    Q_PROPERTY(QStringList sources READ sources NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList resolutions READ resolutions NOTIFY optionsChanged)
    Q_PROPERTY(QStringList paperSizes READ paperSizes NOTIFY optionsChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY settingsChanged)
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY settingsChanged)
    Q_PROPERTY(QString paperSize READ paperSize WRITE setPaperSize NOTIFY settingsChanged)
    Q_PROPERTY(bool feeder READ feeder NOTIFY settingsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int pagesThisRun READ pagesThisRun NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    explicit Scanner(const QString &incomingDir, QObject *parent = nullptr);
    ~Scanner() override;

    bool available() const { return !m_program.isEmpty(); }
    bool discovering() const { return m_discover != nullptr; }
    QVariantList devices() const { return m_devices; }
    int deviceIndex() const { return m_deviceIndex; }
    void setDeviceIndex(int index);
    bool loadingOptions() const { return m_options != nullptr; }
    QStringList modes() const { return m_modes; }
    QStringList sources() const { return m_sources; }
    QVariantList resolutions() const;
    QStringList paperSizes() const;
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString source() const { return m_source; }
    void setSource(const QString &source);
    int resolution() const { return m_resolution; }
    void setResolution(int dpi);
    QString paperSize() const { return m_paperSize; }
    void setPaperSize(const QString &size);
    bool feeder() const;
    bool scanning() const { return m_scan != nullptr; }
    qreal progress() const { return m_progress; }
    int pagesThisRun() const { return m_pagesThisRun; }
    QString status() const { return m_status; }
    bool ready() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancel();

signals:
    void discoveringChanged();
    void devicesChanged();
    void deviceIndexChanged();
    void optionsChanged();
    void settingsChanged();
    void scanningChanged();
    void progressChanged();
    void statusChanged();
    void readyChanged();
    void pageScanned(const QString &path, qreal dpi);
    void failed(const QString &message);

private:
    QString deviceId() const;
    void loadOptions();
    void parseOptions(const QString &text);
    void readScanOutput();
    void readScanErrors();
    void scanFinished(int exitCode, QProcess::ExitStatus status);
    void deliver(const QString &path);
    void setStatus(const QString &status);
    void remember() const;
    static QString friendlyError(const QString &raw);

    QString m_program;
    QString m_incoming;
    QPointer<QProcess> m_discover;
    QPointer<QProcess> m_options;
    QPointer<QProcess> m_scan;

    QVariantList m_devices;
    int m_deviceIndex = -1;
    QStringList m_modes;
    QStringList m_sources;
    QList<int> m_resolutionList;
    int m_resolutionMin = 0;
    int m_resolutionMax = 0;
    QSizeF m_maxArea; // mm; empty when the device does not say
    bool m_hasGeometry = false;

    QString m_mode;
    QString m_source;
    int m_resolution = 300;
    QString m_paperSize;

    qreal m_progress = -1;
    int m_pagesThisRun = 0;
    int m_runResolution = 0;
    QString m_run;          // names this run's files in incoming/
    QString m_singleOutput;
    QString m_stderrTail;
    bool m_cancelled = false;
    QString m_status;
};
