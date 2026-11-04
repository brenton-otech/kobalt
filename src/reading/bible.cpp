#include "bible.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace kobalt {
namespace {
bool integer(const QJsonValue &value, int minimum, int maximum) {
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() == value.toInt(-1) &&
           value.toDouble() >= minimum && value.toDouble() <= maximum;
}
}
bool BibleInfo::contains(BibleReference ref) const {
    return ref.book >= 0 && ref.book < books.size() && ref.chapter >= 0 &&
           ref.chapter < books[ref.book].chapters.size() && ref.verse >= 0 &&
           ref.verse <= books[ref.book].chapters[ref.chapter].verses;
}
QString BibleInfo::key(BibleReference ref) const {
    if (!contains(ref)) return {};
    auto result = books[ref.book].code + '.' + QString::number(books[ref.book].chapters[ref.chapter].number);
    return ref.verse ? result + '.' + QString::number(ref.verse) : result;
}
QString BibleInfo::label(BibleReference ref) const {
    if (!contains(ref)) return {};
    auto result = books[ref.book].name + ' ' + books[ref.book].chapters[ref.chapter].label;
    return ref.verse ? result + ':' + QString::number(ref.verse) : result;
}
BibleReference BibleInfo::reference(const QString &key) const {
    const auto parts = key.split('.');
    if (parts.size() != 2 && parts.size() != 3) return {};
    bool chapterOk = false, verseOk = true;
    const int chapter = parts[1].toInt(&chapterOk);
    const int verse = parts.size() == 3 ? parts[2].toInt(&verseOk) : 0;
    if (!chapterOk || !verseOk || (parts.size() == 3 && verse <= 0)) return {};
    for (int b = 0; b < books.size(); ++b) if (books[b].code == parts[0])
        for (int c = 0; c < books[b].chapters.size(); ++c) if (books[b].chapters[c].number == chapter) {
            BibleReference ref{b, c, verse};
            return contains(ref) ? ref : BibleReference{};
        }
    return {};
}
BibleInfo BibleInfo::parse(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > maximumBytes) return {};
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) return {};
    const auto json = document.object();
    if (!integer(json.value("format"), 1, 2)) return {};
    BibleInfo result;
    result.id = json.value("id").toString();
    result.name = json.value("name").toString();
    const auto books = json.value("books").toArray();
    if (result.id.isEmpty() || result.id.size() > 32 || result.name.isEmpty() || result.name.size() > 256 ||
        books.isEmpty() || books.size() > 100) return {};
    QSet<QString> codes;
    int totalChapters = 0;
    for (const auto &value : books) {
        const auto item = value.toObject();
        BibleBook book{item.value("code").toString(), item.value("name").toString(), item.value("testament").toString(), {}};
        if (!QRegularExpression("^[A-Z0-9]{3}$").match(book.code).hasMatch() || codes.contains(book.code) ||
            book.name.isEmpty() || book.name.size() > 128 ||
            (book.testament != "OT" && book.testament != "NT" && book.testament != "AP")) return {};
        codes.insert(book.code);
        const auto counts = item.value("chapters").toArray();
        const auto numbers = item.value("chapter_numbers").toArray();
        const auto labels = item.value("chapter_labels").toArray();
        if (counts.isEmpty() || counts.size() > 200 || (totalChapters += counts.size()) > 2000 ||
            (item.contains("chapter_numbers") && numbers.size() != counts.size()) ||
            (item.contains("chapter_labels") && labels.size() != counts.size())) return {};
        QSet<int> seen;
        for (int c = 0; c < counts.size(); ++c) {
            if (!integer(counts[c], 1, 512) || (!numbers.isEmpty() && !integer(numbers[c], 0, 1000))) return {};
            const int number = numbers.isEmpty() ? c + 1 : numbers[c].toInt();
            const auto label = labels.isEmpty() ? QString::number(number) : labels[c].toString();
            if (seen.contains(number) || label.isEmpty() || label.size() > 32) return {};
            seen.insert(number);
            book.chapters.append({number, counts[c].toInt(), label});
        }
        result.books.append(book);
    }
    return result;
}
}
