#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

namespace kobalt::opds {
// One Atom <link>. Hrefs are resolved against the feed's own URL.
struct Link
{
    QString rel, type, title;
    QUrl href;
};

// An entry is a navigation entry (it opens another feed) or an acquisition
// entry (it offers a book). The parser keeps both kinds in the feed order.
struct Entry
{
    QString id, title, author, summary, updated;
    QVector<Link> links;
    bool acquisition = false;
};

struct Feed
{
    QString title;
    QUrl self;
    QVector<Link> links;
    QVector<Entry> entries;

    // The rel="next" link, when the server pages a long catalog.
    QUrl next() const;
};

// Accepts Atom feeds with the OPDS 1.x namespaces. Returns false and sets
// error for malformed XML, a document that is not an Atom feed, or a DOCTYPE
// (external entities are refused). Relative hrefs resolve against base.
bool parseFeed(const QByteArray &xml, const QUrl &base, Feed *feed, QString *error);

// The first EPUB offered by an acquisition entry, or null when it offers none.
// Only links that OPDS names as acquisition links with an EPUB type count.
const Link *epubLink(const Entry &entry);

// A file name from the title and author, with path separators and control
// characters removed and the length capped. Never empty.
QString fileName(const Entry &entry);

// True when an entry's URL may receive the account's credentials: the same
// scheme, host and port as the catalog root.
bool sameServer(const QUrl &root, const QUrl &target);
} // namespace kobalt::opds
