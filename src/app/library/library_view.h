#pragma once
#include "ui/controls/panels.h"
#include "library/library_index.h"
#include "platform/api/keyboard_controller.h"
#include "opds/browser.h"
#include <functional>
#include <optional>
#include <utility>

class QLineEdit;
class QPushButton;

namespace kobalt {
class SavedWords;
class WordList;
class WordPage;
struct SavedWord;
} // namespace kobalt

namespace kobalt::ui {
class CellButton;
class CaptionDropdown;
class PagerBar;
class ModalOverlay;
class ActionMenu;
class LoadingIndicator;
} // namespace kobalt::ui

namespace kobalt::library {
class AboutScreen;
class BookPages;
class FolderPage;
class SortSheet;
class FolderPicker;
class CatalogPage;
class CatalogAccountPage;
class WifiCell;
class MoreCell;

struct FolderLocation
{
    QString path, parent;
    QStringList folders;
    QString error;
    QStringList books;

    FolderLocation(QString path = {}, QString parent = {}, QStringList folders = {},
                   QString error = {}, QStringList books = {})
        : path(std::move(path)),
          parent(std::move(parent)),
          folders(std::move(folders)),
          error(std::move(error)),
          books(std::move(books))
    {
    }
};

// The Android and desktop library. It draws in device pixels on the reader's
// canvas, with the reader's header, rows, pager and dialogs. The host owns the
// index and every file operation; the view only reports what was chosen.
class LibraryView final : public ui::Panel
{
public:
    LibraryView(const Index &index, std::function<QString(const QString &)> coverPath,
                QWidget *parent = nullptr);
    ~LibraryView() override;
    // Call after the index changes. While scanning, the view redraws at most
    // once a second, which keeps e-paper refreshes down.
    void refresh();
    void setScanning(bool scanning, int found);
    void setDark(bool dark);
    void setKeyboardFactory(KeyboardFactory factory);
    void showNotice(const QString &title, const QString &message);
    // A question with Cancel and one action, for example before an update.
    void showChoice(const QString &title, const QString &message, const QString &action,
                    std::function<void()> accepted);
    // A set checkForUpdates adds Check for updates to the menu.
    std::function<void()> checkForUpdates;
    // A set chooseSleepWallpapers adds Sleep wallpapers to the menu, for hosts
    // that let the reader pick the folder of their sleep pictures.
    std::function<void()> chooseSleepWallpapers;
    // Marks the menu and renames its update entry when a newer build is known.
    void setUpdateAvailable(bool available);
    // The system back action: closes the topmost layer. False when only the
    // library itself is left.
    bool dismiss();
    void pageButton(int direction);
    // With download set, the picker chooses the catalog download folder and
    // reports it through chooseDownloadFolder.
    // The Wi-Fi switch in the header, for hosts that control the radio. The
    // host reports the radio state; a tap asks for the opposite state.
    void setWifi(bool enabled, bool connected, int strength);
    void hideWifi();
    std::function<void(bool enabled)> setWifiEnabled;
    // A set setDarkMode adds the dark mode switch left of Search. The host
    // saves the choice and answers through setDark.
    std::function<void(bool enabled)> setDarkMode;
    void showFolderPicker(const QString &path, bool books = false, bool download = false);

    void showBookPicker(const QString &path) { showFolderPicker(path, true); }

    // A folder picker for a host setting, with its own title and description.
    // Use folder reports the folder to chosen instead of chooseFolder.
    void showFolderPicker(const QString &path, const QString &title, const QString &description,
                          std::function<void(const QString &path)> chosen);

    void setFolderPickerLocation(const FolderLocation &location);
    void setFolderPickerLoading(const QString &path);
    void closeFolderPicker();
    std::function<void(const QString &path)> browseFolder, chooseFolder, chooseBook;
    // The catalog of an OPDS server. The host owns the catalog browser and the
    // downloads; a set openCatalog adds Catalog to the menu. The view reports
    // the chosen row, the way back and the request for a further page.
    void showCatalog();
    void setCatalogLoading();
    void setCatalogListing(const opds::Listing &listing, bool keepPage = false);
    void setCatalogItemDetail(int index, const QString &detail);
    void closeCatalog();
    // The account screen: address, username and password, each edited with
    // the on-screen keyboard. Saving reports the three values as entered.
    void showCatalogAccount(const QString &address, const QString &username,
                            const QString &password);
    void closeCatalogAccount();
    std::function<void()> catalogEditAccount;
    std::function<void(const QString &address, const QString &username, const QString &password)>
            catalogAccountSaved;
    // Makes the keyboard of the account fields. setKeyboardFactory sets it too.
    KeyboardFactory editorKeyboard;
    std::function<void()> openCatalog, catalogBack, catalogMore, catalogChooseFolder;
    std::function<void(int index)> catalogSelect;
    std::function<void(const QString &path)> chooseDownloadFolder;

    std::function<void(const QString &key)> openBook, removeFolder, removeBook;
    // A set deleteBook adds Delete book to a book's menu. After the user
    // confirms, the host deletes the book file itself.
    std::function<void(const QString &key)> deleteBook;
    // Opens a book at a saved word's position.
    std::function<void(const QString &key, const QString &position)> openPassage;
    // Adds My Words to the menu. Words live in the profile database there.
    void setWordsDirectory(const QString &directory);

    // The host supplies the current settings when the reader returns to the Library.
    void setReaderPreferences(const QJsonObject &preferences) { savedPreferences = preferences; }

    const QJsonObject &readerPreferences();
    // The Library book a saved word came from, found by its path first and
    // then by the content fingerprint of books with the same title. Empty when
    // the book is not in the Library.
    QString bookFor(const SavedWord &word) const;
    std::function<void(const QString &key, Book::Mark mark)> markBook;
    std::function<void()> addFolder, openFile, rescan, exitApp;
    std::function<void(const Settings &settings)> settingsChanged;
    // On Android a book opened on its own is Kobalt's copy, which removal
    // deletes. The confirmation says so.
    bool singleBooksAreCopies = false;
    // Optional host folder pickers: dictionaries, fonts, custom pins, in that order.
    std::function<QStringList()> resourceFolderLabels;
    std::function<void(int kind, bool clear)> chooseResourceFolder;

protected:
    void resizeEvent(QResizeEvent *) override;
    void hideEvent(QHideEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    const Index &index;
    std::function<QString(const QString &)> coverPath;
    ui::CellButton *search, *closeSearch, *night;
    MoreCell *more;
    bool updateAvailable = false;
    WifiCell *wifi;
    bool wifiShown = false, wifiEnabled = false;
    QVector<ui::CellButton *> modes;
    QLineEdit *query;
    std::unique_ptr<KeyboardController> keyboard;
    ui::CaptionDropdown *sort;
    ui::LoadingIndicator *progress;
    BookPages *pages;
    ui::PagerBar *pager;
    QWidget *empty;
    SortSheet *sortSheet = nullptr;
    ui::ActionMenu *moreSheet = nullptr;
    FolderPage *folderPage = nullptr;
    FolderPicker *folderPicker = nullptr;
    // Set while the picker chooses a folder for a host setting.
    std::function<void(const QString &)> hostFolderChosen;
    CatalogPage *catalogPage = nullptr;
    CatalogAccountPage *catalogAccount = nullptr;
    AboutScreen *about = nullptr;
    std::unique_ptr<SavedWords> savedWords;
    QString savedWordsDirectory;
    std::optional<QJsonObject> savedPreferences;
    bool moreSheetWords = false;
    WordList *wordList = nullptr;
    WordPage *wordPage = nullptr;
    void showWords();
    void openWord(const SavedWord &word);
    ui::ModalOverlay *overlay = nullptr;
    QTimer *throttle;
    bool scanning = false, searching = false, dark = false;
    int found = 0;
    void arrange();
    void rebuild(bool keepPage = true);
    void applySettings(const Settings &settings);
    void showBookMenu(const QString &key);
    void showMoreSheet();
    void showMenu(ui::ActionMenu *menu, const char *name);
    void confirmRemove(const QString &key);
    void showFolders();
    void showResourceFolders();
    void showAbout();
    void closeOverlay();
    QWidget *buildEmpty();
    void setSearching(bool value);
};
} // namespace kobalt::library
