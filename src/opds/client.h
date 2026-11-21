#pragma once
#include "feed.h"
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace kobalt::opds {
// Fetches catalog feeds and downloads EPUB acquisitions into a folder. The
// network manager belongs to the caller and must outlive the client. Results
// are delivered on the GUI thread; the file write runs on a worker thread.
class Client final : public QObject
{
    Q_OBJECT
public:
    Client(QUrl root, QString username, QString password, QNetworkAccessManager *network,
           QObject *parent = nullptr);
    ~Client() override;

    // Requests a feed. The URL may be the root or a link from a feed.
    void fetch(const QUrl &url);
    // Downloads the entry's EPUB into folder. The folder must already exist.
    void download(const Entry &entry, const QString &folder);
    // Cancels pending requests. Results that arrive afterwards are dropped.
    void cancel();

signals:
    void feedLoaded(kobalt::opds::Feed feed);
    void downloaded(QString path);
    void failed(QString message);

private:
    QUrl root;
    QString username, password;
    QNetworkAccessManager *network;
    quint64 generation = 0;
    QNetworkRequest request(const QUrl &url) const;
};
} // namespace kobalt::opds
