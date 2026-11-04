#include "reader_model.h"
#include "reader_source.h"
#include "column_view.h"
#include "engine_activity.h"
#include <lvdocview.h>

namespace kobalt {
namespace {
lString32 cre(const QString &s) { return Utf8ToUnicode(s.toUtf8().constData()); }
QString text(const lString32 &s) { return QString::fromUtf8(UnicodeToUtf8(s).c_str()); }
ldomNode *fragment(ldomNode *node) {
    while (node && !node->isNodeName("DocFragment")) node = node->getParentNode();
    return node;
}
}
struct ReaderModel::BibleNavigation {
    BibleInfo info;
    struct Chapter { BibleReference ref; QString position; };
    QVector<Chapter> chapters;
    QVector<QPair<int, QString>> verses;
    int cachedChapter = -1;
};
void ReaderModel::prepareBible() {
    if (bible_) return;
    bible_ = std::make_shared<BibleNavigation>();
    if (!engine_ || !source_ || source_->container.isNull()) return;
    const auto name = QStringLiteral("OEBPS/bible.json");
    QByteArray bytes;
    if (source_->resources) {
        const auto size = source_->sizes.value(name, -1);
        if (size <= 0 || size > BibleInfo::maximumBytes) return;
        bytes = source_->resources->readBounded(name, BibleInfo::maximumBytes);
    } else {
        const auto stream = source_->container->OpenStream(cre(name).c_str(), LVOM_READ);
        if (stream.isNull() || stream->GetSize() > BibleInfo::maximumBytes) return;
        bytes.resize(int(stream->GetSize()));
        lvsize_t read = 0;
        if (stream->Read(bytes.data(), bytes.size(), &read) != LVERR_OK || read != lvsize_t(bytes.size())) return;
    }
    bible_->info = BibleInfo::parse(bytes);
    for (int b = 0; b < bible_->info.books.size(); ++b)
        for (int c = 0; c < bible_->info.books[b].chapters.size(); ++c) {
            engineActivity().checkpoint();
            BibleReference ref{b, c, 0};
            const auto position = biblePosition(ref);
            if (!position.isEmpty()) bible_->chapters.append({ref, position});
        }
    if (bible_->chapters.isEmpty()) bible_->info = {};
}
BibleInfo ReaderModel::bibleInfo() { prepareBible(); return bible_->info; }
QString ReaderModel::biblePosition(BibleReference ref) {
    prepareBible();
    if (!engine_ || !bible_->info.contains(ref)) return {};
    auto *doc = engine_->getDocument();
    // kn's contract keeps book XHTML files first, in bible.json spine order.
    // CREngine namespaces each source ID with that zero-based fragment index.
    for (int verse = ref.verse; verse >= 0; --verse) {
        auto candidate = ref;
        candidate.verse = verse;
        const auto key = bible_->info.key(candidate);
        for (const auto &id : {QString("_doc_fragment_%1_ %2").arg(ref.book).arg(key), key}) {
            auto *node = doc->getNodeById(doc->getAttrValueIndex(cre(id).c_str()));
            if (node) return text(ldomXPointer(node, 0).toString());
        }
        // A chapter request has no verse fallback. A missing/merged verse
        // selects its preceding anchored verse, then the chapter start.
    }
    return {};
}
BibleReference ReaderModel::bibleReference() {
    prepareBible();
    if (!engine_ || !bible_->info.valid()) return {};
    auto *doc = engine_->getDocument();
    const ldomXPointerEx current(doc->createXPointer(cre(anchor_)));
    if (current.isNull()) return {};
    int low = 0, high = bible_->chapters.size() - 1, found = -1;
    while (low <= high) {
        const int mid = (low + high) / 2;
        const ldomXPointerEx target(doc->createXPointer(cre(bible_->chapters[mid].position)));
        if (!target.isNull() && target.compare(current) <= 0) { found = mid; low = mid + 1; }
        else high = mid - 1;
    }
    if (found < 0) found = 0;
    const auto chapter = bible_->chapters[found];
    const auto chapterPointer = doc->createXPointer(cre(chapter.position));
    if (fragment(current.getNode()) != fragment(chapterPointer.getNode())) return {};
    if (bible_->cachedChapter != found) {
        bible_->cachedChapter = found;
        bible_->verses.clear();
        const auto count = bible_->info.books[chapter.ref.book].chapters[chapter.ref.chapter].verses;
        QString previous;
        for (int v = 1; v <= count; ++v) {
            engineActivity().checkpoint();
            auto ref = chapter.ref; ref.verse = v;
            const auto position = biblePosition(ref);
            if (!position.isEmpty() && position != previous) bible_->verses.append({v, position});
            previous = position;
        }
    }
    auto result = chapter.ref;
    low = 0; high = bible_->verses.size() - 1;
    while (low <= high) {
        const int mid = (low + high) / 2;
        const ldomXPointerEx target(doc->createXPointer(cre(bible_->verses[mid].second)));
        if (!target.isNull() && target.compare(current) <= 0) { result.verse = bible_->verses[mid].first; low = mid + 1; }
        else high = mid - 1;
    }
    if (!result.verse && !bible_->verses.isEmpty()) result.verse = bible_->verses.first().first;
    return result;
}
}
