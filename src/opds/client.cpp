#include "client.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QPointer>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRunnable>
#include <QSaveFile>
#include <QThreadPool>

namespace kobalt::opds {
namespace {
// A catalog entry larger than this is refused before it reaches memory.
constexpr qint64 maxDownloadBytes = 200LL * 1024 * 1024;
constexpr int transferTimeoutMilliseconds = 60000;

QString describe(QNetworkReply *reply)
{
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401)
        return QStringLiteral("The server rejected the username or password.");
    return reply->errorString();
}

// Adds " (2)", " (3)" and so on until the name is free.
QString uniquePath(const QString &folder, const QString &name)
{
    QString path = QDir(folder).filePath(name);
    const auto info = QFileInfo(name);
    for (int number = 2; QFileInfo::exists(path); ++number)
        path = QDir(folder).filePath(info.completeBaseName()
                                     + QStringLiteral(" (%1).epub").arg(number));
    return path;
}
} // namespace

Client::Client(QUrl root, QString username, QString password, QNetworkAccessManager *network,
               QObject *parent)
    : QObject(parent),
      root(std::move(root)),
      username(std::move(username)),
      password(std::move(password)),
      network(network)
{
    qRegisterMetaType<Feed>("kobalt::opds::Feed");
}

Client::~Client() = default;

QNetworkRequest Client::request(const QUrl &url) const
{
    QNetworkRequest result(url);
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    result.setTransferTimeout(transferTimeoutMilliseconds);
    result.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kobalt OPDS"));
    // Credentials go only to the catalog's own server, never to another host a link names.
    if (!username.isEmpty() && sameServer(root, url))
        result.setRawHeader(
                "Authorization",
                "Basic "
                        + QByteArray((username + QLatin1Char(':') + password).toUtf8()).toBase64());
    return result;
}

void Client::fetch(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        emit failed(QStringLiteral("The catalog address is not valid."));
        return;
    }
    const auto id = generation;
    auto *reply = network->get(request(url));
    // A reply ends with its client.
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, url] {
        reply->deleteLater();
        if (id != generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(describe(reply));
            return;
        }
        Feed feed;
        QString error;
        if (!parseFeed(reply->readAll(), url, &feed, &error))
            emit failed(error);
        else
            emit feedLoaded(feed);
    });
}

void Client::download(const Entry &entry, const QString &folder)
{
    const auto *link = epubLink(entry);
    if (!link) {
        emit failed(QStringLiteral("This entry does not offer an EPUB."));
        return;
    }
    if (!QFileInfo(folder).isDir()) {
        emit failed(QStringLiteral("The download folder is missing."));
        return;
    }
    const auto id = generation;
    const auto target = uniquePath(folder, fileName(entry));
    auto *reply = network->get(request(link->href));
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, target] {
        reply->deleteLater();
        if (id != generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(describe(reply));
            return;
        }
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maxDownloadBytes) {
            emit failed(QStringLiteral("The book is too large to download."));
            return;
        }
        const auto bytes = reply->readAll();
        if (bytes.size() > maxDownloadBytes) {
            emit failed(QStringLiteral("The book is too large to download."));
            return;
        }
        // Storage writes run on a worker thread, never on the GUI thread.
        // The client can be destroyed while the worker writes. The result goes
        // through the application object, and the guard is read on the GUI thread only.
        QRunnable *job = QRunnable::create([guard = QPointer<Client>(this), id, target, bytes] {
            QSaveFile file(target);
            const bool written = file.open(QIODevice::WriteOnly)
                    && file.write(bytes) == bytes.size() && file.commit();
            const auto error = written ? QString() : file.errorString();
            QMetaObject::invokeMethod(
                    QCoreApplication::instance(),
                    [guard, id, target, written, error] {
                        if (!guard || id != guard->generation)
                            return;
                        if (written)
                            emit guard->downloaded(target);
                        else
                            emit guard->failed(
                                    error.isEmpty() ? QStringLiteral("The book could not be saved.")
                                                    : error);
                    },
                    Qt::QueuedConnection);
        });
        QThreadPool::globalInstance()->start(job);
    });
}

void Client::cancel()
{
    ++generation;
}
} // namespace kobalt::opds
