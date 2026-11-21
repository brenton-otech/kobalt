#include "browser.h"
#include "client.h"
#include "support/strings.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRunnable>
#include <QSaveFile>

namespace kobalt::opds {
namespace {
// An account file is a few short fields; anything larger is not one.
constexpr qint64 maxAccountBytes = 64 * 1024;

// The feed an entry leads to: its first Atom link that is not an acquisition.
QUrl sectionUrl(const Entry &entry)
{
    for (const auto &link : entry.links)
        if (link.type.startsWith(QLatin1String("application/atom+xml"))
            && !link.rel.startsWith(QLatin1String("http://opds-spec.org/acquisition")))
            return link.href;
    return { };
}
} // namespace

QUrl catalogUrl(const QString &text)
{
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return { };
    QUrl url(trimmed.contains(QLatin1String("://")) ? trimmed
                                                    : QStringLiteral("https://") + trimmed);
    const auto scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
        return { };
    return url;
}

bool readAccount(const QString &path, Account *account)
{
    QFile file(path);
    if (file.size() > maxAccountBytes || !file.open(QIODevice::ReadOnly))
        return false;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    Account result;
    result.url = catalogUrl(object.value(QStringLiteral("url")).toString());
    result.username = object.value(QStringLiteral("username")).toString();
    result.password = object.value(QStringLiteral("password")).toString();
    result.folder = object.value(QStringLiteral("folder")).toString();
    if (!result.url.isValid())
        return false;
    *account = result;
    return true;
}

Browser::Browser(QString accountPath, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), accountPath(std::move(accountPath)), network(network)
{
    qRegisterMetaType<Listing>("kobalt::opds::Listing");
    writer.setMaxThreadCount(1);
}

// The writer pool finishes a pending account save before it is destroyed.
Browser::~Browser() = default;

const Account &Browser::account()
{
    if (!std::exchange(loaded, true))
        readAccount(accountPath, &current);
    return current;
}

void Browser::save()
{
    const QJsonObject object{ { QStringLiteral("url"), current.url.toString() },
                              { QStringLiteral("username"), current.username },
                              { QStringLiteral("password"), current.password },
                              { QStringLiteral("folder"), current.folder } };
    // Storage writes run on a worker thread, never on the GUI thread.
    writer.start(QRunnable::create([path = accountPath, object] {
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)
            && file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0)
            file.commit();
    }));
}

void Browser::setAccount(const QUrl &url, const QString &username, const QString &password)
{
    account();
    current.url = url;
    current.username = username;
    current.password = password;
    save();
}

Listing Browser::listing(const QString &error) const
{
    Listing result;
    result.title = title;
    result.folder = current.folder;
    result.error = error;
    result.more = nextPage.isValid();
    for (const auto &entry : entries) {
        Item item;
        item.title = entry.title;
        item.book = entry.acquisition;
        item.detail = entry.acquisition ? entry.author : entry.summary;
        if (entry.acquisition && !epubLink(entry))
            item.detail = strings::get("catalog.no_epub");
        result.items.append(item);
    }
    return result;
}

void Browser::open()
{
    close();
    title.clear();
    if (!account().url.isValid()) {
        emit listed(listing(strings::get("catalog.no_account")), false);
        return;
    }
    delete feeds;
    feeds = new Client(current.url, current.username, current.password, network, this);
    // A running download finishes with the client that started it.
    if (downloading < 0) {
        delete downloads;
        downloads = nullptr;
    }
    if (!downloads) {
        downloads = new Client(current.url, current.username, current.password, network, this);
        connect(downloads, &Client::downloaded, this, [this](const QString &path) {
            downloading = -1;
            if (downloadShown == shown)
                emit itemChanged(downloadRow, strings::get("catalog.downloaded"));
            emit downloaded(path);
        });
        connect(downloads, &Client::failed, this, [this](const QString &message) {
            downloading = -1;
            if (downloadShown == shown)
                emit itemChanged(downloadRow, message);
        });
    }
    connect(feeds, &Client::feedLoaded, this, [this](const Feed &feed) {
        const bool appended = appending;
        appending = false;
        if (!appended) {
            entries.clear();
            ++shown;
        }
        entries += feed.entries;
        if (!feed.title.isEmpty())
            title = feed.title;
        nextPage = feed.next();
        emit listed(listing(), appended);
    });
    connect(feeds, &Client::failed, this, [this](const QString &message) {
        // A failed further page keeps the pages already shown.
        if (appending) {
            appending = false;
            emit listed(listing(), true);
            return;
        }
        entries.clear();
        nextPage.clear();
        ++shown;
        emit listed(listing(message), false);
    });
    trail = { current.url };
    fetch(current.url);
}

void Browser::fetch(const QUrl &url)
{
    feeds->cancel();
    appending = false;
    emit loading();
    feeds->fetch(url);
}

void Browser::select(int index)
{
    if (!feeds || index < 0 || index >= entries.size())
        return;
    const auto &entry = entries[index];
    if (!entry.acquisition) {
        const auto url = sectionUrl(entry);
        if (!url.isValid())
            return;
        waiting = -1;
        trail.append(url);
        fetch(url);
        return;
    }
    if (!epubLink(entry) || downloading >= 0)
        return;
    if (current.folder.isEmpty() || !QFileInfo(current.folder).isDir()) {
        waiting = index;
        emit folderNeeded();
        return;
    }
    download(index);
}

void Browser::download(int index)
{
    downloading = downloadRow = index;
    downloadShown = shown;
    emit itemChanged(index, strings::get("catalog.downloading"));
    downloads->download(entries[index], current.folder);
}

bool Browser::back()
{
    if (!feeds || trail.size() < 2)
        return false;
    waiting = -1;
    trail.removeLast();
    fetch(trail.last());
    return true;
}

void Browser::more()
{
    if (!feeds || !nextPage.isValid() || appending)
        return;
    appending = true;
    feeds->fetch(nextPage);
}

void Browser::close()
{
    if (feeds)
        feeds->cancel();
    appending = false;
    waiting = -1;
    ++shown;
    entries.clear();
    trail.clear();
    nextPage.clear();
}

void Browser::setFolder(const QString &folder)
{
    account();
    current.folder = folder;
    save();
    emit listed(listing(), true);
    if (waiting >= 0 && waiting < entries.size() && downloading < 0)
        download(std::exchange(waiting, -1));
}
} // namespace kobalt::opds
