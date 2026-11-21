#include "opds/feed.h"
#include "check.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

using namespace kobalt::opds;

namespace {
const char navigationAndNext[] = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opds="http://opds-spec.org/2010/catalog">
  <title>Library</title>
  <link rel="self" href="/opds/new" type="application/atom+xml"/>
  <link rel="next" href="/opds/new?page=2" type="application/atom+xml"/>
  <entry>
    <title>Recent additions</title>
    <id>urn:nav:recent</id>
    <link rel="subsection" href="recent/" type="application/atom+xml;profile=opds-catalog"/>
  </entry>
  <entry>
    <title>Dune</title>
    <author><name>Frank Herbert</name></author>
    <id>urn:book:1</id>
    <updated>2026-01-02T00:00:00Z</updated>
    <summary>Desert planet.</summary>
    <link rel="http://opds-spec.org/acquisition" href="/books/1.epub" type="application/epub+zip"/>
    <link rel="http://opds-spec.org/acquisition" href="/books/1.pdf" type="application/pdf"/>
    <link rel="http://opds-spec.org/image/thumbnail" href="/covers/1.jpg" type="image/jpeg"/>
  </entry>
  <entry>
    <title>PDF only</title>
    <id>urn:book:2</id>
    <link rel="http://opds-spec.org/acquisition" href="/books/2.pdf" type="application/pdf"/>
  </entry>
</feed>)";

// Returns the parsed feed, or fails the test with the parser's message.
Feed parse(const char *xml, const QUrl &base)
{
    Feed feed;
    QString error;
    const bool ok = parseFeed(QByteArray(xml), base, &feed, &error);
    require(ok, "A valid Atom feed did not parse");
    return feed;
}
} // namespace

// Checks OPDS feed parsing: navigation and acquisition entries, EPUB link choice, resolved links, paging and file names.
int main()
{
    const QUrl base(QStringLiteral("https://books.example.test/opds/new"));
    const auto feed = parse(navigationAndNext, base);

    require(feed.title == QLatin1String("Library"), "Feed title was not read");
    require(feed.entries.size() == 3, "Entries were not all read");
    require(feed.next() == QUrl(QStringLiteral("https://books.example.test/opds/new?page=2")),
            "The next link was not resolved against the feed URL");

    const auto &navigation = feed.entries[0];
    require(!navigation.acquisition, "A subsection entry was taken for a book");
    require(epubLink(navigation) == nullptr, "A navigation entry offered an EPUB");

    const auto &book = feed.entries[1];
    require(book.acquisition, "An acquisition entry was not recognized");
    require(book.author == QLatin1String("Frank Herbert"), "The author name was not read");
    require(book.summary == QLatin1String("Desert planet."), "The summary was not read");
    const auto *epub = epubLink(book);
    require(epub != nullptr, "The EPUB acquisition link was not found");
    require(epub->href == QUrl(QStringLiteral("https://books.example.test/books/1.epub")),
            "The EPUB link was not resolved against the feed URL");
    require(fileName(book) == QLatin1String("Frank Herbert - Dune.epub"),
            "The file name was not built from the author and title");

    const auto &pdfOnly = feed.entries[2];
    require(pdfOnly.acquisition, "A PDF-only acquisition entry was not recognized as acquisition");
    require(epubLink(pdfOnly) == nullptr, "A PDF link was offered as an EPUB");

    // Malformed, non-Atom and DOCTYPE responses fail without a feed.
    Feed unused;
    QString error;
    require(!parseFeed(QByteArray("<feed"), base, &unused, &error) && !error.isEmpty(),
            "Malformed XML was accepted");
    require(!parseFeed(QByteArray("<html><body>Login</body></html>"), base, &unused, &error),
            "A web page was accepted as a feed");
    require(!parseFeed(
                    QByteArray(
                            "<!DOCTYPE feed [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><feed xmlns=\"http://www.w3.org/2005/Atom\"><title>x</title></feed>"),
                    base, &unused, &error),
            "A DOCTYPE was accepted");
    require(!parseFeed(QByteArray(), base, &unused, &error), "An empty response was accepted");

    // File names drop path separators and control characters, and stay usable.
    Entry odd;
    odd.title = QStringLiteral("A/B:C?*\n..");
    odd.author = QStringLiteral("");
    require(fileName(odd) == QLatin1String("A_B_C___.epub"),
            "Unsafe characters survived the file name");
    Entry blank;
    require(fileName(blank) == QLatin1String("Book.epub"),
            "An empty title did not get a fallback name");
    Entry longTitle;
    longTitle.title = QString(300, QLatin1Char('x'));
    require(fileName(longTitle).size() <= 120 + 5, "A long title was not capped");

    // Credentials follow only the catalog's own origin.
    const QUrl root(QStringLiteral("https://books.example.test/opds"));
    require(sameServer(root, QUrl(QStringLiteral("https://books.example.test/books/1.epub"))),
            "The same server was refused");
    require(sameServer(root, QUrl(QStringLiteral("https://books.example.test:443/x"))),
            "The default port was not treated as the same server");
    require(!sameServer(root, QUrl(QStringLiteral("http://books.example.test/x"))),
            "A different scheme was treated as the same server");
    require(!sameServer(root, QUrl(QStringLiteral("https://cdn.example.test/x"))),
            "A different host received the same server");
    require(!sameServer(root, QUrl(QStringLiteral("https://books.example.test:8443/x"))),
            "A different port was treated as the same server");
    return 0;
}
