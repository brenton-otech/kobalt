#include "feed.h"
#include <QXmlStreamReader>
#include <utility>

namespace kobalt::opds {
namespace {
constexpr auto atomNamespace = "http://www.w3.org/2005/Atom";
constexpr auto acquisitionPrefix = "http://opds-spec.org/acquisition";
constexpr auto epubType = "application/epub+zip";
constexpr int maxNameLength = 120;

Link readLink(QXmlStreamReader &xml, const QUrl &base)
{
    const auto attributes = xml.attributes();
    Link link;
    link.rel = attributes.value(QString(), QStringLiteral("rel")).toString();
    link.type = attributes.value(QString(), QStringLiteral("type")).toString();
    link.title = attributes.value(QString(), QStringLiteral("title")).toString();
    link.href = base.resolved(QUrl(attributes.value(QString(), QStringLiteral("href")).toString()));
    return link;
}

// Reads the text of an element that holds no child elements.
QString readText(QXmlStreamReader &xml)
{
    return xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

// Reads the name of an author element and skips its other children.
QString readAuthor(QXmlStreamReader &xml)
{
    QString name;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("name"))
            name = readText(xml);
        else
            xml.skipCurrentElement();
    }
    return name;
}

Entry readEntry(QXmlStreamReader &xml, const QUrl &base)
{
    Entry entry;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QLatin1String("title"))
            entry.title = readText(xml);
        else if (name == QLatin1String("id"))
            entry.id = readText(xml);
        else if (name == QLatin1String("updated"))
            entry.updated = readText(xml);
        else if (name == QLatin1String("summary") || name == QLatin1String("content"))
            entry.summary = readText(xml);
        else if (name == QLatin1String("author"))
            entry.author = readAuthor(xml);
        else if (name == QLatin1String("link")) {
            entry.links.append(readLink(xml, base));
            xml.skipCurrentElement();
        } else
            xml.skipCurrentElement();
    }
    for (const auto &link : std::as_const(entry.links))
        entry.acquisition = entry.acquisition || link.rel.startsWith(acquisitionPrefix);
    return entry;
}
} // namespace

QUrl Feed::next() const
{
    for (const auto &link : links)
        if (link.rel == QLatin1String("next"))
            return link.href;
    return { };
}

bool parseFeed(const QByteArray &xml, const QUrl &base, Feed *feed, QString *error)
{
    QXmlStreamReader reader(xml);
    reader.setNamespaceProcessing(true);
    Feed result;
    result.self = base;
    bool rootSeen = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD()) {
            if (error)
                *error = QStringLiteral("The feed contains a DOCTYPE, which is refused");
            return false;
        }
        if (!reader.isStartElement())
            continue;
        if (!rootSeen) {
            rootSeen = true;
            if (reader.namespaceUri() != QLatin1String(atomNamespace)
                || reader.name() != QLatin1String("feed")) {
                if (error)
                    *error = QStringLiteral("The response is not an Atom feed");
                return false;
            }
            while (reader.readNextStartElement()) {
                const auto name = reader.name();
                if (name == QLatin1String("title"))
                    result.title = readText(reader);
                else if (name == QLatin1String("link")) {
                    result.links.append(readLink(reader, base));
                    reader.skipCurrentElement();
                } else if (name == QLatin1String("entry"))
                    result.entries.append(readEntry(reader, base));
                else
                    reader.skipCurrentElement();
            }
            break;
        }
    }
    if (reader.hasError() || !rootSeen) {
        if (error)
            *error = reader.hasError() ? reader.errorString()
                                       : QStringLiteral("The response is empty");
        return false;
    }
    *feed = std::move(result);
    return true;
}

const Link *epubLink(const Entry &entry)
{
    for (const auto &link : entry.links)
        if (link.rel.startsWith(acquisitionPrefix) && link.type == QLatin1String(epubType))
            return &link;
    return nullptr;
}

QString fileName(const Entry &entry)
{
    QString base = entry.author.isEmpty() ? entry.title
                                          : entry.author + QStringLiteral(" - ") + entry.title;
    for (auto &character : base) {
        if (character.unicode() < 0x20 || QStringLiteral("/\\:*?\"<>|").contains(character))
            character = QLatin1Char('_');
    }
    base = base.trimmed();
    // Leading and trailing dots would hide the file or make it ambiguous.
    while (base.startsWith(QLatin1Char('.')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('.')))
        base.chop(1);
    base = base.trimmed();
    if (base.size() > maxNameLength)
        base.truncate(maxNameLength);
    if (base.trimmed().isEmpty())
        base = QStringLiteral("Book");
    return base + QStringLiteral(".epub");
}

bool sameServer(const QUrl &root, const QUrl &target)
{
    // A missing port means the scheme's default, so https://host and https://host:443 match.
    const auto effectivePort = [](const QUrl &url) {
        if (url.port() != -1)
            return url.port();
        return url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 ? 443 : 80;
    };
    return !root.host().isEmpty()
            && root.scheme().compare(target.scheme(), Qt::CaseInsensitive) == 0
            && root.host().compare(target.host(), Qt::CaseInsensitive) == 0
            && effectivePort(root) == effectivePort(target);
}
} // namespace kobalt::opds
