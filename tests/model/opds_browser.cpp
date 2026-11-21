#include "opds/browser.h"
#include "check.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <functional>

using namespace kobalt::opds;

namespace {
const QByteArray bookBytes("PK\x03\x04 synthetic book");

QByteArray feed(const QByteArray &title, const QByteArray &body)
{
    return "<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\"><title>" + title
            + "</title>" + body + "</feed>";
}

QByteArray book(const QByteArray &title, const QByteArray &href)
{
    return "<entry><title>" + title
            + "</title><author><name>Ann Author</name></author>"
              "<link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" href=\""
            + href + "\"/></entry>";
}

// A catalog that asks for Basic credentials on every path.
class Server final : public QTcpServer
{
public:
    QByteArray expected = "Basic " + QByteArray("admin:secret").toBase64();
    QList<QByteArray> requests;

    Server()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                if (!socket->property("request").isValid())
                    socket->setProperty("request", QByteArray());
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n"))
                    return;
                const auto target = request.split(' ').value(1);
                requests.append(target);
                const bool allowed = request.contains("uthorization: " + expected + "\r\n");
                QByteArray status = "200 OK", type = "application/atom+xml", body;
                if (!allowed) {
                    status = "401 Unauthorized";
                    body = "Unauthorized Access";
                } else if (target == "/opds")
                    body = feed("Home",
                                "<entry><title>New books</title><content>Latest</content>"
                                "<link rel=\"subsection\" href=\"/opds/new\" "
                                "type=\"application/atom+xml;profile=opds-catalog\"/></entry>");
                else if (target == "/opds/new")
                    body = feed("New books",
                                "<link rel=\"next\" href=\"/opds/new?page=2\"/>"
                                        + book("First", "/download/1.epub"));
                else if (target == "/opds/new?page=2")
                    body = feed("New books", book("Second", "/download/2.epub"));
                else if (target == "/download/1.epub") {
                    type = "application/epub+zip";
                    body = bookBytes;
                } else
                    status = "404 Not Found";
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type
                              + "\r\nContent-Length: " + QByteArray::number(body.size())
                              + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
};

// Runs the event loop until the condition holds. The limit only guards against a hang.
void waitFor(const std::function<bool()> &done, const char *message)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 20);
    require(done(), message);
}

} // namespace

// Checks the catalog browser against a local server: sign-in failure, sections, further pages, the way back, the download folder and a download.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    require(catalogUrl("books.example.test/opds") == QUrl("https://books.example.test/opds"),
            "An address without a scheme did not become https");
    require(!catalogUrl("ftp://books.example.test").isValid() && !catalogUrl("  ").isValid(),
            "An unusable address was accepted");

    Server server;
    require(server.listen(QHostAddress::LocalHost), "The local catalog did not start");
    const auto url = QStringLiteral("http://127.0.0.1:%1/opds").arg(server.serverPort());
    QTemporaryDir directory;
    const auto accountPath = directory.filePath("opds.json");
    const auto folder = directory.filePath("books");
    require(QDir().mkpath(folder), "The download folder could not be created");

    QNetworkAccessManager network;
    Browser browser(accountPath, &network);
    QVector<Listing> listings;
    QVector<bool> appendedFlags;
    QStringList details, files;
    int folderRequests = 0;
    QObject::connect(&browser, &Browser::listed, &app, [&](const Listing &listing, bool appended) {
        listings.append(listing);
        appendedFlags.append(appended);
    });
    QObject::connect(&browser, &Browser::itemChanged, &app, [&](int index, const QString &detail) {
        details.append(QString::number(index) + ':' + detail);
    });
    QObject::connect(&browser, &Browser::downloaded, &app,
                     [&](const QString &path) { files.append(path); });
    QObject::connect(&browser, &Browser::folderNeeded, &app, [&] { ++folderRequests; });

    require(!browser.account().url.isValid(), "A missing account file gave an account");
    // A missing account is an error on the catalog screen, not a request.
    browser.open();
    require(listings.size() == 1 && !listings[0].error.isEmpty() && server.requests.isEmpty(),
            "A missing account did not report an error");

    // A wrong password reports the sign-in failure.
    browser.setAccount(QUrl(url), "admin", "wrong");
    browser.open();
    waitFor([&] { return listings.size() == 2; }, "A rejected sign-in gave no listing");
    require(listings[1].error.contains("password"), "A rejected sign-in was not explained");

    // The first feed lists its section.
    browser.setAccount(QUrl(url), "admin", "secret");
    browser.open();
    waitFor([&] { return listings.size() == 3; }, "The catalog did not load");
    require(listings[2].error.isEmpty() && listings[2].title == "Home"
                    && listings[2].items.size() == 1 && !listings[2].items[0].book
                    && listings[2].items[0].detail == "Latest" && !listings[2].more,
            "The first feed was not listed");
    require(!browser.back(), "The first feed had a way back");

    // A section opens, and its next page is appended.
    browser.select(0);
    waitFor([&] { return listings.size() == 4; }, "The section did not load");
    require(listings[3].title == "New books" && listings[3].items.size() == 1
                    && listings[3].items[0].book && listings[3].items[0].detail == "Ann Author"
                    && listings[3].more && !appendedFlags[3],
            "The section was not listed");
    browser.more();
    waitFor([&] { return listings.size() == 5; }, "The next page did not load");
    require(listings[4].items.size() == 2 && listings[4].items[1].title == "Second"
                    && !listings[4].more && appendedFlags[4],
            "The next page was not appended");

    // A book waits for a download folder, then downloads into it.
    browser.select(0);
    require(folderRequests == 1 && files.isEmpty(), "A download started without a folder");
    browser.setFolder(folder);
    waitFor([&] { return files.size() == 1; }, "The book was not downloaded");
    require(files[0] == folder + "/Ann Author - First.epub", "The book got another file name");
    QFile saved(files[0]);
    require(saved.open(QIODevice::ReadOnly) && saved.readAll() == bookBytes,
            "The saved book differs from the served one");
    require(details.size() == 2 && details[0].startsWith("0:") && details[1].startsWith("0:"),
            "The row did not report its download");
    require(browser.folder() == folder, "The folder was not kept");
    waitFor(
            [&] {
                QFile account(accountPath);
                return account.open(QIODevice::ReadOnly)
                        && QJsonDocument::fromJson(account.readAll())
                                   .object()
                                   .value("folder")
                                   .toString()
                        == folder;
            },
            "The folder was not saved in the account file");
    QFile account(accountPath);
    require(account.open(QIODevice::ReadOnly)
                    && QJsonDocument::fromJson(account.readAll()).object().value("password")
                            == "secret",
            "Saving the folder lost the password");

    // A second download of the same book keeps the first file.
    browser.select(0);
    waitFor([&] { return files.size() == 2; }, "The second download did not finish");
    require(files[1] == folder + "/Ann Author - First (2).epub", "A download replaced a file");

    // A failed download reports on its row and frees the next one.
    browser.select(1);
    waitFor([&] { return details.size() == 6; }, "A failed download was not reported");
    require(files.size() == 2, "A missing book was saved");

    // Another browser reads the saved account back.
    Browser second(accountPath, &network);
    require(second.account().url == QUrl(url) && second.account().username == "admin"
                    && second.account().password == "secret" && second.account().folder == folder,
            "The saved account was not read back");

    // The way back returns to the first feed.
    require(browser.back(), "The section had no way back");
    waitFor([&] { return listings.last().title == "Home" && listings.last().items.size() == 1; },
            "The way back did not load the first feed");
    require(!browser.back(), "The first feed had a way back after returning");
    return 0;
}
