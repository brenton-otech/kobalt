#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;

namespace kobalt::kindle {
// Looks for a newer Kobalt package in the repositories that KPM knows, and
// hands an update to the helper that runs after Kobalt has exited. KPM cannot
// replace a running Kobalt, so the app never updates itself in place.
class Updater final : public QObject
{
    Q_OBJECT
public:
    // The newest package of one repository manifest.
    struct Candidate
    {
        QVector<int> version;
        QString description;
    };

    // The folders come from the launcher. Without them, or without KPM,
    // available() is false and the Library offers no update entry.
    Updater(QString appDirectory, QString runDirectory, QNetworkAccessManager *network,
            QObject *parent = nullptr);

    bool available() const;
    // Asks KPM for its repositories and reads each manifest. Exactly one of
    // upToDate, found and failed follows.
    void check();
    // Starts the helper. The caller then closes Kobalt; the helper waits for that.
    bool start();

    // The manifest addresses in the output of "kpm list-repo".
    static QVector<QUrl> repositories(const QString &listing);
    // The newest artifact of a package for a platform; an empty version when none.
    static Candidate newest(const QByteArray &manifest, const QString &package,
                            const QString &platform);
    // Negative, zero or positive, like a three-way comparison of the numbers.
    static int compare(const QVector<int> &left, const QVector<int> &right);
    static QVector<int> installedVersion();

signals:
    void upToDate();
    void found(QString description);
    void failed(QString message);

private:
    QString appDirectory, runDirectory;
    QNetworkAccessManager *network;
    quint64 generation = 0;
    int pending = 0;
    bool reached = false;
    Candidate best;
    void finish();
};
} // namespace kobalt::kindle
