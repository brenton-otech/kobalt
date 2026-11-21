#pragma once
#include "feed.h"
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;

namespace kobalt::opds {
class Client;

// The catalog account, kept in the profile as opds.json:
// { "url": "...", "username": "...", "password": "...", "folder": "..." }.
// The account screen sets the address and credentials, and the catalog screen
// the folder where downloaded books go.
struct Account
{
    QUrl url;
    QString username, password, folder;
};

// Accepts an address without a scheme and assumes https for it.
QUrl catalogUrl(const QString &text);
// Returns false when the file is missing, unreadable or has no usable address.
bool readAccount(const QString &path, Account *account);

// One row of the catalog screen.
struct Item
{
    QString title, detail;
    bool book = false;
};

// What the catalog screen shows: one feed, with all of its pages loaded so far.
struct Listing
{
    QString title, folder, error;
    QVector<Item> items;
    bool more = false;
};

// Walks a catalog for the Library's catalog screen: it opens feeds, keeps the
// way back, loads further pages and downloads books into the chosen folder.
// The network manager belongs to the caller and must outlive the browser.
class Browser final : public QObject
{
    Q_OBJECT
public:
    Browser(QString accountPath, QNetworkAccessManager *network, QObject *parent = nullptr);
    ~Browser() override;

    // The saved account. Its address is invalid until one is saved.
    const Account &account();
    // Saves the address and credentials, and keeps the download folder.
    void setAccount(const QUrl &url, const QString &username, const QString &password);
    // Loads the catalog's first feed with the current account.
    void open();
    // Opens a section, or downloads a book.
    void select(int index);
    // Returns to the previous feed. False at the first feed.
    bool back();
    // Loads the feed's next page and appends it.
    void more();
    // Leaves the catalog. A running download still completes.
    void close();
    // Remembers the download folder and resumes a waiting download.
    void setFolder(const QString &folder);

    QString folder() { return account().folder; }

signals:
    void loading();
    void listed(kobalt::opds::Listing listing, bool appended);
    // The status of one row changed, for example while its book downloads.
    void itemChanged(int index, QString detail);
    void downloaded(QString path);
    // A book was chosen before a download folder.
    void folderNeeded();

private:
    QString accountPath;
    QNetworkAccessManager *network;
    Account current;
    bool loaded = false;
    // One worker writes the account file, so saves keep their order.
    QThreadPool writer;
    Client *feeds = nullptr, *downloads = nullptr;
    QVector<QUrl> trail;
    QVector<Entry> entries;
    QString title;
    QUrl nextPage;
    bool appending = false;
    int downloading = -1, waiting = -1;
    // Counts the feeds shown, so a download reports only on the rows it started from.
    int shown = 0, downloadShown = -1, downloadRow = -1;
    Listing listing(const QString &error = { }) const;
    void fetch(const QUrl &url);
    void download(int index);
    void save();
};
} // namespace kobalt::opds

Q_DECLARE_METATYPE(kobalt::opds::Listing)
