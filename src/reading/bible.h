#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

namespace kobalt {
struct BibleReference {
    int book = -1, chapter = -1, verse = 0;
    bool valid() const { return book >= 0 && chapter >= 0; }
};
struct BibleChapter {
    int number = 0, verses = 0;
    QString label;
};
struct BibleBook {
    QString code, name, testament;
    QVector<BibleChapter> chapters;
};
// Immutable publication metadata. This is not saved reading data; source
// positions and annotations keep using the existing reader storage formats.
struct BibleInfo {
    static constexpr int maximumBytes = 1024 * 1024;
    QString id, name;
    QVector<BibleBook> books;
    bool valid() const { return !books.isEmpty(); }
    bool contains(BibleReference ref) const;
    QString key(BibleReference ref) const;
    QString label(BibleReference ref) const;
    BibleReference reference(const QString &key) const;
    static BibleInfo parse(const QByteArray &bytes);
};
}
