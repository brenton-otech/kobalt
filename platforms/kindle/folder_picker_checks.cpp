// Focused assertions in the existing isolated --smoke-test lane. They drive
// the same library widgets, directory worker and store as the device app.
#include "folder_picker_checks.h"
#include "folder_browser.h"
#include "window.h"
#include "platforms/android/start_screen.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/style/scale.h"
#include "support/main_thread.h"
#include "support/strings.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QScopeGuard>

namespace kobalt::kindle {
namespace {
bool wait(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 15000)
        QCoreApplication::processEvents();
    return done();
}
} // namespace

bool prepareFolderPickerChecks(const QString &root, const QString &book)
{
    mainthread::blocking("folder-picker-check-fixture");
    for (const auto &name :
         { QString("Books/Novels"), QString("Empty"), QString(".hidden"), QString("book.sdr") })
        if (!QDir().mkpath(root + '/' + name))
            return false;
    for (int i = 0; i < 14; ++i)
        if (!QDir().mkpath(root + QString("/Shelf %1").arg(i, 2, 10, QLatin1Char('0'))))
            return false;
    if (!QDir().mkpath(root + "-other"))
        return false;
    QFile notes(root + "/notes.txt"), hidden(root + "/.hidden.epub");
    if (!notes.open(QIODevice::WriteOnly) || notes.write("not a book") < 0
        || !hidden.open(QIODevice::WriteOnly) || hidden.write("hidden") < 0)
        return false;
    return QFile::copy(book, root + "/Books/Novels/fixture.epub")
            && QFile::copy(book, root + "/SINGLE.EPUB")
            && QFile::copy(book, root + "-other/outside.epub")
            && QFile::link(root + "/SINGLE.EPUB", root + "/linked.epub")
            && QFile::link(root + "-other", root + "/outside-link");
}

bool checkFolderPicker(Window &window, const QString &root, const QString &captures)
{
    const auto originalSize = window.size();
    const auto restoreSize = qScopeGuard([&] { window.resize(originalSize); });
    window.resize(1072, 1448);
    QCoreApplication::processEvents();
    auto fail = [](const char *message) {
        qCritical() << "Folder picker check:" << message;
        return false;
    };
    auto *start = window.findChild<android::StartScreen *>();
    if (!start)
        return fail("missing library");
    auto *view = start->library();
    auto button = [view](const char *name) { return view->findChild<QPushButton *>(name); };
    auto press = [&](const char *name) {
        auto *target = button(name);
        if (!target || !target->isVisible() || !target->isEnabled())
            return false;
        target->click();
        QCoreApplication::processEvents();
        return true;
    };
    auto save = [&](const char *name) {
        if (captures.isEmpty())
            return true;
        mainthread::blocking("folder-picker-check-capture");
        auto image = view->grab().toImage();
        image.setDotsPerMeterX(qRound(300 / .0254));
        image.setDotsPerMeterY(qRound(300 / .0254));
        return QDir().mkpath(captures) && image.save(captures + '/' + name + ".png");
    };
    const auto listing = FolderBrowser::list(root, root);
    if (!listing.error.isEmpty() || !listing.parent.isEmpty() || listing.folders.size() != 16)
        return fail("root listing included hidden, sidecar or symlink folders");
    if (FolderBrowser::list(root, root + "-other").error.isEmpty()
        || FolderBrowser::list(root, root + "/outside-link").error.isEmpty()
        || FolderBrowser::list(root, root + "/missing").error.isEmpty())
        return fail("outside or unavailable folder accepted");
    const auto empty = FolderBrowser::list(root, root + "/Empty");
    if (!empty.error.isEmpty() || !empty.folders.isEmpty() || empty.parent != root)
        return fail("empty directory is not selectable");
    // Superseded reads must never replace the most recent navigation.
    {
        FolderBrowser browser(root);
        int deliveries = 0;
        QString path;
        QObject::connect(&browser, &FolderBrowser::listed, &window,
                         [&](const library::FolderLocation &value) {
                             ++deliveries;
                             path = value.path;
                         });
        browser.navigate(root);
        browser.navigate(root + "/Empty");
        if (!wait([&] { return !path.isEmpty(); }) || path != root + "/Empty" || deliveries != 1)
            return fail("superseded directory listing delivered");
    }
    if (!press("libraryMore") || !press("libraryMoreFolders") || !save("01-library-folders"))
        return fail("cannot open Library folders");
    if (!press("libraryFoldersAdd"))
        return fail("Add folder is not connected");
    auto ready = [&] {
        return button("libraryFolderPickerUse") && button("libraryFolderPickerUse")->isVisible()
                && button("libraryFolderPickerUse")->isEnabled();
    };
    if (!wait(ready) || !save("02-folder-picker"))
        return fail("root listing never became ready");
    if (button("libraryFolderPickerUp")->isVisible())
        return fail("root permits navigation above storage");
    if (!press("libraryFolderPickerNext"))
        return fail("long folder list has no next page");
    auto *row = view->findChild<QWidget *>("libraryFolderPickerRow0");
    if (!row || row->property("folderPath").toString() == root + "/Books")
        return fail("paging retained the first directory");
    if (!press("libraryFolderPickerPrevious") || !press("libraryFolderPickerRow0") || !wait(ready))
        return fail("cannot navigate into Books");
    if (!press("libraryFolderPickerRow0") || !wait(ready))
        return fail("cannot navigate nested folders");
    if (view->findChild<QWidget *>("libraryFolderPickerRow0")->isVisible())
        return fail("empty folder retained stale rows");
    if (!press("libraryFolderPickerUp") || !wait(ready))
        return fail("cannot return to parent");
    if (!press("libraryFolderPickerUse"))
        return fail("cannot select directory");
    if (!wait([&] { return window.libraryContains(root + "/Books/Novels/fixture.epub"); }))
        return fail("selected folder did not scan nested EPUB");
    if (!press("libraryFoldersAdd") || !wait(ready) || !press("libraryFolderPickerBack"))
        return fail("cannot cancel picker");
    if (!view->findChild<QWidget *>("libraryFolders")->isVisible())
        return fail("cancel lost Library folders");
    view->dismiss();
    if (!captures.isEmpty()) {
        const auto textScale = ui::textScale();
        const auto restoreText = qScopeGuard([textScale] { ui::setLargeText(textScale); });
        for (const auto &size : { QSize(1072, 1448), QSize(1236, 1648) }) {
            for (int variant = 0; variant < 3; ++variant) {
                ui::setLargeText(variant == 2 ? 1.5 : 1);
                library::Index sample;
                sample.addFolder({ "/mnt/us/epubs", "epubs" });
                library::LibraryView preview(sample, {});
                preview.setDark(variant == 1);
                preview.resize(size);
                preview.show();
                QCoreApplication::processEvents();
                const auto prefix =
                        (size.width() == 1072 ? QString("six-inch-") : QString("seven-inch-"))
                        + (variant == 0           ? "light-"
                                   : variant == 1 ? "dark-"
                                                  : "large-");
                auto capture = [&](const char *name) {
                    mainthread::blocking("folder-picker-check-capture");
                    auto image = preview.grab().toImage();
                    image.setDotsPerMeterX(qRound(300 / .0254));
                    image.setDotsPerMeterY(qRound(300 / .0254));
                    return image.save(captures + '/' + prefix + name + ".png");
                };
                preview.findChild<QPushButton *>("libraryMore")->click();
                preview.findChild<QPushButton *>("libraryMoreFolders")->click();
                if (!capture("entry"))
                    return fail("cannot capture entry");
                preview.showFolderPicker("/mnt/us");
                preview.setFolderPickerLocation(
                        { "/mnt/us",
                          {},
                          { "/mnt/us/Books", "/mnt/us/documents", "/mnt/us/epubs" },
                          {} });
                if (!capture("picker"))
                    return fail("cannot capture picker");
                preview.setFolderPickerLocation(
                        { "/mnt/us/Books",
                          "/mnt/us",
                          { "/mnt/us/Books/Classics", "/mnt/us/Books/Novels" },
                          {} });
                if (!capture("nested"))
                    return fail("cannot capture nested picker");
                preview.setFolderPickerLocation({ "/mnt/us/Books/Empty", "/mnt/us/Books", {}, {} });
                if (!capture("empty"))
                    return fail("cannot capture empty picker");
                preview.setFolderPickerLocation(
                        { "/mnt/us/Books/Unavailable",
                          "/mnt/us/Books",
                          {},
                          strings::get("library.folder_picker_unavailable") });
                if (!capture("error"))
                    return fail("cannot capture picker error");
                preview.closeFolderPicker();
                preview.findChild<QPushButton *>("libraryFoldersBack")->click();
                preview.findChild<QPushButton *>("libraryMore")->click();
                if (!capture("open-book-entry"))
                    return fail("cannot capture single-book entry");
                preview.showBookPicker("/mnt/us/epubs");
                preview.setFolderPickerLocation(
                        { "/mnt/us/epubs",
                          "/mnt/us",
                          { "/mnt/us/epubs/Classics" },
                          {},
                          { "/mnt/us/epubs/Trevelyan's Type Tester.epub",
                            "/mnt/us/epubs/A Book with a Longer Filename.EPUB" } });
                if (!capture("open-book-picker"))
                    return fail("cannot capture single-book picker");
                preview.setFolderPickerLocation({ "/mnt/us/Empty", "/mnt/us", {}, {} });
                if (!capture("open-book-empty"))
                    return fail("cannot capture empty single-book picker");
            }
        }
    }
    qInfo("Kindle folder picker passed: storage boundary, stale reads, paging, navigation, selection, recursive scan, cancel");
    return true;
}

bool openBookFromPicker(Window &window, const QString &root, const QString &captures)
{
    auto fail = [](const char *message) {
        qCritical() << "Single-book picker check:" << message;
        return false;
    };
    const auto book = root + "/SINGLE.EPUB";
    const auto listing = FolderBrowser::list(root, root, true);
    if (!listing.error.isEmpty() || listing.books != QStringList{ book }
        || !FolderBrowser::list(root, root).books.isEmpty())
        return fail("EPUB filtering or mode isolation failed");
    if (!FolderBrowser::book(root, book).error.isEmpty()
        || FolderBrowser::book(root, root + "/notes.txt").error.isEmpty()
        || FolderBrowser::book(root, root + "/Empty").error.isEmpty()
        || FolderBrowser::book(root, root + "/missing.epub").error.isEmpty()
        || FolderBrowser::book(root, root + "/linked.epub").error.isEmpty()
        || FolderBrowser::book(root, root + "/outside-link/outside.epub").error.isEmpty())
        return fail("file selection failed to validate type, readability or storage boundary");
    auto *start = window.findChild<android::StartScreen *>();
    auto *view = start->library();
    auto button = [view](const char *name) { return view->findChild<QPushButton *>(name); };
    auto press = [&](const char *name) {
        auto *target = button(name);
        if (!target || !target->isVisible() || !target->isEnabled())
            return false;
        target->click();
        QCoreApplication::processEvents();
        return true;
    };
    auto ready = [&] {
        auto *picker = view->findChild<QWidget *>("libraryFolderPicker");
        return picker && picker->isVisible() && !picker->property("loading").toBool();
    };
    // Cover both entry points: the empty-library action uses the same host
    // signal as More, whose production connection is exercised here.
    if (!press("libraryMore") || !press("libraryMoreOpenBook") || !wait(ready))
        return fail("Open book is not connected");
    if (button("libraryFolderPickerUse")->isVisible())
        return fail("file picker exposes folder selection");
    if (!press("libraryFolderPickerBack"))
        return fail("file picker cannot cancel");
    if (!press("libraryMore") || !press("libraryMoreOpenBook") || !wait(ready))
        return fail("file picker cannot reopen");
    for (int page = 0; page < 20; ++page) {
        for (int i = 0; i < 20; ++i) {
            auto *row = button(qPrintable(QString("libraryFolderPickerRow%1").arg(i)));
            if (!row || !row->isVisible() || row->property("folderPath").toString() != book)
                continue;
            if (!row->property("isBook").toBool())
                return fail("EPUB row is treated as a folder");
            if (!captures.isEmpty()) {
                mainthread::blocking("single-book-picker-check-capture");
                auto image = view->grab().toImage();
                image.setDotsPerMeterX(qRound(300 / .0254));
                image.setDotsPerMeterY(qRound(300 / .0254));
                if (!image.save(captures + "/03-single-book-picker.png"))
                    return fail("cannot capture file selection");
            }
            row->click();
            qInfo("Kindle single-book picker passed: file filtering, storage boundary, cancel and EPUB selection; opening through the reader smoke lane");
            return true;
        }
        if (!press("libraryFolderPickerNext"))
            break;
    }
    return fail("selected EPUB never appeared in the paged list");
}
} // namespace kobalt::kindle
