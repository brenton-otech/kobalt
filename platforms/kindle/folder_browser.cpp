#include "folder_browser.h"
#include "support/main_thread.h"
#include "support/strings.h"
#include "library/scanner.h"
#include <QDir>
#include <QFileInfo>

namespace kobalt::kindle {
namespace {
bool inside(const QString &root, const QString &path) {
    return !root.isEmpty() && !path.isEmpty() && (path == root || path.startsWith(root + '/'));
}
}
library::FolderLocation FolderBrowser::list(const QString &root, const QString &path, bool books) {
    library::FolderLocation result;
    result.path = QDir::cleanPath(path);
    const auto base = QFileInfo(root).canonicalFilePath();
    const QFileInfo folder(path);
    const auto canonical = folder.canonicalFilePath();
    // Keep a way back from a folder removed between listing and opening.
    const auto parent = QFileInfo(folder.absolutePath()).canonicalFilePath();
    if (inside(base, parent) && result.path != base) result.parent = parent;
    if (!folder.isDir() || !folder.isReadable() || base.isEmpty()) {
        result.error = strings::get("library.folder_picker_unavailable");
        return result;
    }
    if (!inside(base, canonical)) {
        result.parent = base;
        result.error = strings::get("library.folder_outside_storage");
        return result;
    }
    result.path = canonical;
    result.parent = canonical == base ? QString() : QFileInfo(canonical).absolutePath();
    const auto entries = QDir(canonical).entryInfoList(QDir::Dirs | (books ? QDir::Files : QDir::Filter(0)) | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                                      QDir::Name | QDir::IgnoreCase);
    for (const auto &entry : entries) {
        // KOReader's book sidecars are metadata, not browsable book folders.
        if (entry.isDir() && !entry.fileName().endsWith(QStringLiteral(".sdr"), Qt::CaseInsensitive))
            result.folders.append(entry.absoluteFilePath());
        else if (entry.isFile() && library::isBookName(entry.fileName()))
            result.books.append(entry.absoluteFilePath());
    }
    return result;
}
library::FolderLocation FolderBrowser::book(const QString &root, const QString &path) {
    library::FolderLocation result;
    const QFileInfo file(path);
    result.path = file.canonicalFilePath();
    if (!file.isFile() || !file.isReadable() || file.isSymLink() || !library::isBookName(file.fileName()))
        result.error = strings::get("library.book_picker_unavailable");
    else if (!inside(QFileInfo(root).canonicalFilePath(), result.path))
        result.error = strings::get("library.folder_outside_storage");
    return result;
}
FolderBrowser::FolderBrowser(QString root, QObject *parent) : QObject(parent), root(std::move(root)) {
    qRegisterMetaType<library::FolderLocation>();
    auto *worker = new QObject;
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &FolderBrowser::requested, worker, [this](const QString &path, quint64 token, int action) {
        emit completed(action == SelectBook ? book(this->root, path) : list(this->root, path, action == BrowseBooks), token, action);
    });
    connect(this, &FolderBrowser::completed, this, [this](const library::FolderLocation &location, quint64 token, int action) {
        if (token != generation) return;
        if (action == BrowseFolders || action == BrowseBooks) emit listed(location);
        else if (!location.error.isEmpty()) emit failed(location.error);
        else if (action == SelectBook) emit bookSelected(location.path);
        else emit selected(location.path);
    });
    thread.setObjectName(QStringLiteral("kindle-folder-browser"));
    thread.start();
}
FolderBrowser::~FolderBrowser() {
    // The host is exiting; no further reader input can arrive.
    mainthread::AllowBlocking allow("closing Kindle folder browser");
    mainthread::blocking("kindle-folder-browser-stop");
    thread.quit();
    thread.wait();
}
void FolderBrowser::navigate(QString path, bool books) {
    // The loading signal can replace the UI model that supplied this path.
    // Keep our own value through that synchronous update.
    emit loading(path);
    emit requested(path, ++generation, books ? BrowseBooks : BrowseFolders);
}
void FolderBrowser::select(QString path) {
    emit requested(path, ++generation, SelectFolder);
}
void FolderBrowser::selectBook(QString path) {
    emit requested(path, ++generation, SelectBook);
}
}
