#include "window.h"
#include "services.h"
#include "device.h"
#include "folder_browser.h"
#include "updater.h"
#include "sleep_wallpapers.h"
#include "pointer_cancellation.h"
#include "qpa/sleep_frame.h"
#include "platforms/standalone/canvas.h"
#include "platforms/standalone/start_screen.h"
#include "app/library/library_view.h"
#include "app/reader/views/annotation_exporter.h"
#include "platform/api/kobo_refresh.h"
#include "platform/api/safe_area.h"
#include "ui/style/fonts.h"
#include "support/strings.h"
#include "reading/features.h"
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QtMath>
#include <QPushButton>
#include <QStackedWidget>
#include <QThreadPool>
#include <QPointer>
#include <QFile>
#include <QJsonObject>
#include <QVBoxLayout>
#include <utility>

namespace kobalt::kindle {
namespace {
class SleepView final : public QLabel
{
public:
    explicit SleepView(QWidget *parent) : QLabel(parent) { }

    void paintEvent(QPaintEvent *event) override
    {
        auto *surface = parentWidget()->windowHandle();
        const auto generation =
                surface ? surface->property("kobalt.sleepRequested").toULongLong() : 0;
        QLabel::paintEvent(event);
        if (surface)
            ::kindle::sleepFramePainted(*surface, generation);
    }
};
} // namespace

Window::Window(storage::Paths paths, EngineFontInputs fonts, QString placeholder, QString root,
               QString folderStorageRoot)
    : device(makeDevice()),
      controller(
              paths, std::move(fonts), std::move(placeholder),
              [this, device = device, exports = paths.directory + QStringLiteral("/exports")] {
                  auto services = readerServices(
                          device, wallpapers ? wallpapers->directories() : QStringList());
                  if (bottomBand > 0)
                      services.safeArea = [band = bottomBand](QSize) {
                          return SafeArea{ 0, band, 0 };
                      };
                  services.annotationExporter = [exports](QWidget *reader) {
                      return reader::directoryAnnotationExporter(reader, exports);
                  };
                  return services;
              },
              [](Qt::ScreenOrientation) {}),
      store(paths.directory, std::make_shared<library::LocalSource>()),
      libraryRoot(QFileInfo(root).absoluteFilePath())
{
    setWindowTitle(QStringLiteral("Kobalt"));
    new PointerCancellation(this);
    setObjectName("kindleWindow");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    stack = new QStackedWidget(this);
    layout->addWidget(stack, 1);
    sleepView = new SleepView(this);
    sleepView->setObjectName("kindleSleepView");
    sleepView->setAutoFillBackground(true);
    auto sleepFont = ui::interfaceFont();
    sleepFont.setPixelSize(64);
    sleepView->setFont(sleepFont);
    sleepView->setAlignment(Qt::AlignCenter);
    sleepView->hide();
    retirementView = new QLabel(this);
    retirementView->setObjectName("kindleRetirementView");
    retirementView->setScaledContents(true);
    retirementView->hide();
    connect(device.get(), &Device::sleepingChanged, this, &Window::sleepingChanged);
    connect(device.get(), &Device::sleepFrameChanged, this, &Window::updateSleepView);
    connect(device.get(), &Device::usbRequested, this, &QWidget::close);
    connect(device.get(), &Device::lifecycleFailed, this, [this](const QString &error) {
        deviceFailure = true;
        qCritical().noquote() << error;
        close();
    });
    standby.setInterval(100);
    connect(&standby, &QTimer::timeout, this, [this] {
        auto *reader = controller.window();
        if (!reader || reader->flushBeforeStandby())
            standby.stop();
    });
    qApp->installEventFilter(this);
    start = new standalone::StartScreen(readerServices(device).presentation, store, {}, stack);
    connect(start, &standalone::StartScreen::libraryExitRequested, this, &QWidget::close);
    start->applyLibraryTheme(controller.preferences(), controller.fontSources());
    canvas = new standalone::WidgetCanvas(stack);
    canvas->setObjectName("kindleReaderCanvas");
    stack->addWidget(start);
    stack->addWidget(canvas);
    platform::setReaderRefreshWindow(this);
    canvas->viewportResized = [this] { sizeCanvas(); };
    canvas->framePainted = [this](const QRegion &) {
        auto *reader = controller.window();
        int direction = 0;
        if (reader) {
            direction = reader->property("reader_animation_direction").toInt();
            reader->setProperty("reader_animation_direction", 0);
            if (!reader->property("reader_page_turn").toBool())
                direction = 0;
        }
        if (windowHandle()) {
            // Forward the reader's cleaning request with the painted frame.
            // Leave it pending until a native surface exists, and never clear
            // a full refresh already waiting for the QPA flush.
            if (reader && reader->property("reader_full_flash").toBool()) {
                windowHandle()->setProperty("kobalt.fullRefresh", true);
                reader->setProperty("reader_full_flash", false);
            }
            if (closing || device->state().sleeping || stack->currentWidget() != canvas
                || QApplication::activeModalWidget() || retirementView->isVisible())
                direction = 0;
            windowHandle()->setProperty("kobalt.pageTurnDirection", direction);
        }
    };
    connect(start, &standalone::StartScreen::bookRequested, this,
            [this](const QString &path) { openBook(path); });
    connect(start, &standalone::StartScreen::passageRequested, this,
            [this](const QString &key, const QString &position) {
                openBook(key, position);
            });
    connect(start, &standalone::StartScreen::exitRequested, this, &Window::closeBook);
    folderBrowser = new FolderBrowser(folderStorageRoot, this);
    // The reader's own sleep pictures. A sleep in the Library shows one at random.
    wallpapers = new SleepWallpapers(
            QFileInfo(paths.directory).dir().filePath(QStringLiteral("sleep-wallpapers.json")),
            QDir(folderStorageRoot).filePath(QStringLiteral("kobalt-wallpapers")), this);
    prepareWallpaper();
    auto *libraryView = start->library();
    libraryView->chooseSleepWallpapers = [this, folderStorageRoot] {
        const auto current = wallpapers->folder();
        start->library()->showFolderPicker(
                QFileInfo(current).isDir() ? current : folderStorageRoot,
                strings::get("library.sleep_wallpapers"),
                strings::get("sleep.kindle_wallpaper_folder_description"),
                [this](const QString &path) { wallpapers->setFolder(path); });
    };
    libraryView->browseFolder = [this](const QString &path) {
        const auto *picker = start->library()->findChild<QWidget *>("libraryFolderPicker");
        folderBrowser->navigate(path, picker && picker->property("selectBook").toBool());
    };
    libraryView->chooseFolder = [this](const QString &path) { folderBrowser->select(path); };
    libraryView->chooseBook = [this](const QString &path) { folderBrowser->selectBook(path); };
    connect(folderBrowser, &FolderBrowser::bookSelected, this,
            [this](const QString &path) { openBook(path); });
    connect(folderBrowser, &FolderBrowser::loading, this,
            [this](const QString &path) { start->library()->setFolderPickerLoading(path); });
    connect(folderBrowser, &FolderBrowser::listed, this,
            [this](const library::FolderLocation &location) {
                start->library()->setFolderPickerLocation(location);
            });
    connect(folderBrowser, &FolderBrowser::selected, this, [this](const QString &path) {
        if (closing)
            return;
        if (std::exchange(catalogFolderPending, false))
            catalog->setFolder(path);
        else
            store.addFolder({ path, path.section('/', -1) });
    });
    connect(folderBrowser, &FolderBrowser::failed, this, [this] { catalogFolderPending = false; });
    connect(folderBrowser, &FolderBrowser::failed, start,
            &standalone::StartScreen::showLibraryError);
    catalog = new opds::Browser(
            QFileInfo(paths.directory).dir().filePath(QStringLiteral("opds.json")), &catalogNetwork,
            this);
    const auto chooseCatalogFolder = [this, folderStorageRoot] {
        const auto current = catalog->folder();
        start->library()->showFolderPicker(QFileInfo(current).isDir() ? current : folderStorageRoot,
                                           false, true);
    };
    const auto editCatalogAccount = [this] {
        const auto &account = catalog->account();
        start->library()->showCatalogAccount(account.url.toString(), account.username,
                                             account.password);
    };
    libraryView->editorKeyboard = readerServices(device).keyboard;
    // Check for updates: the supervisor installs the newer package after Kobalt has exited.
    updater = new Updater(qEnvironmentVariable("KOBALT_KINDLE_HOME"),
                          qEnvironmentVariable("KOBALT_KINDLE_RUN_DIR"), &catalogNetwork, this);
    if (updater->available())
        libraryView->checkForUpdates = [this] {
            if (!device->state().wifi.enabled) {
                start->library()->showNotice(strings::get("update.title"),
                                             strings::get("update.wifi_off"));
                return;
            }
            start->library()->showNotice(strings::get("update.title"),
                                         strings::get("update.checking"));
            // This replaces a quiet check that is still running.
            quietUpdateCheck = false;
            updateChecking = true;
            updater->check();
        };
    connect(updater, &Updater::upToDate, this, [this] {
        updateChecking = false;
        start->library()->setUpdateAvailable(false);
        if (!std::exchange(quietUpdateCheck, false))
            start->library()->showNotice(strings::get("update.title"),
                                         strings::get("update.up_to_date"));
    });
    connect(updater, &Updater::failed, this, [this](const QString &message) {
        updateChecking = false;
        if (!std::exchange(quietUpdateCheck, false))
            start->library()->showNotice(strings::get("update.title"), message);
    });
    connect(updater, &Updater::found, this, [this](const QString &description) {
        updateChecking = false;
        start->library()->setUpdateAvailable(true);
        // A quiet check only marks the menu; the entry there offers the update.
        if (std::exchange(quietUpdateCheck, false))
            return;
        start->library()->showChoice(
                strings::get("update.title"), strings::get("update.found").arg(description),
                strings::get("update.install"), [this] {
                    // The supervisor installs the update after this exit and starts
                    // Kobalt again. This notice stays on the panel meanwhile, so
                    // leave time for it to be drawn.
                    if (updater->start()) {
                        start->library()->showNotice(strings::get("update.title"),
                                                     strings::get("update.installing"));
                        QTimer::singleShot(1500, this, [this] { close(); });
                    } else
                        start->library()->showNotice(strings::get("update.title"),
                                                     strings::get("update.not_started"));
                });
    });
    // The header switch is the stock airplane mode, as in the reader's System settings.
    // Delete book: the file goes, on a worker; saved reading data stays. Only
    // a file inside Kindle storage that the Library lists is deleted.
    libraryView->deleteBook = [this, folderStorageRoot](const QString &key) {
        const QFileInfo file(key);
        const auto path = file.canonicalFilePath();
        const auto root = QFileInfo(folderStorageRoot).canonicalFilePath();
        if (!controller.idle() || !store.index().books.contains(key) || path.isEmpty()
            || root.isEmpty() || !path.startsWith(root + QLatin1Char('/')) || !file.isFile()) {
            start->library()->showNotice(strings::get("library.delete_book"),
                                         strings::get("library.delete_failed"));
            return;
        }
        QThreadPool::globalInstance()->start([this, guard = QPointer<Window>(this), key, path] {
            const bool removed = QFile::remove(path) || !QFileInfo::exists(path);
            QMetaObject::invokeMethod(
                    QCoreApplication::instance(),
                    [this, guard, key, removed] {
                        if (!guard)
                            return;
                        if (removed)
                            store.removeBook(key);
                        else
                            start->library()->showNotice(strings::get("library.delete_book"),
                                                         strings::get("library.delete_failed"));
                    },
                    Qt::QueuedConnection);
        });
    };
    // The reader's dark mode setting, switched from the Library header. The
    // save runs on a worker; the next book opens with the saved value.
    libraryView->setDarkMode = [this, path = paths.preferences()](bool enabled) {
        // Keep the chosen palette; only the light or dark variant changes.
        start->setDark(enabled, start->property("kobalt_palette").toString());
        QThreadPool::globalInstance()->start([path, enabled] {
            storage::updatePreferences(path, QJsonObject{ { "dark_mode", !enabled } },
                                       QJsonObject{ { "dark_mode", enabled } });
        });
    };
    libraryView->setWifiEnabled = [device = device](bool enabled) {
        device->setWirelessEnabled(enabled);
    };
    const auto showWifi = [this] {
        // Settings can hide the switch; the reader's preferences are read when a book closes.
        if (!features::enabled(controller.preferences(), features::WifiSwitch)) {
            start->library()->hideWifi();
            return;
        }
        const auto &wifi = device->state().wifi;
        start->library()->setWifi(wifi.enabled, wifi.connected, wifi.strength);
    };
    connect(device.get(), &Device::wifiChanged, this, showWifi);
    connect(&controller, &standalone::ReaderController::closed, this, showWifi);
    showWifi();
    // Look for a newer build by itself while the Library shows and Wi-Fi is
    // already connected. It never turns Wi-Fi on, and a check that reached a
    // repository is not repeated until Kobalt starts again.
    const auto quietCheck = [this] {
        const auto &wifi = device->state().wifi;
        if (!updater->available() || !controller.idle() || !wifi.enabled || !wifi.connected
            || updateChecking || quietUpdateChecks >= maxQuietUpdateChecks)
            return;
        ++quietUpdateChecks;
        quietUpdateCheck = updateChecking = true;
        updater->check();
    };
    connect(updater, &Updater::upToDate, this,
            [this] { quietUpdateChecks = maxQuietUpdateChecks; });
    connect(updater, &Updater::found, this, [this] { quietUpdateChecks = maxQuietUpdateChecks; });
    connect(device.get(), &Device::wifiChanged, this, quietCheck);
    connect(&controller, &standalone::ReaderController::closed, this, quietCheck);
    QTimer::singleShot(0, this, quietCheck);
    libraryView->openCatalog = [this, editCatalogAccount] {
        // Without an account the catalog starts at its account screen.
        if (!catalog->account().url.isValid()) {
            editCatalogAccount();
            return;
        }
        start->library()->showCatalog();
        catalog->open();
    };
    libraryView->catalogEditAccount = editCatalogAccount;
    libraryView->catalogAccountSaved = [this](const QString &address, const QString &username,
                                              const QString &password) {
        const auto url = opds::catalogUrl(address);
        if (!url.isValid()) {
            start->library()->showNotice(strings::get("catalog.account"),
                                         strings::get("catalog.invalid_address"));
            return;
        }
        catalog->setAccount(url, username, password);
        start->library()->closeCatalogAccount();
        start->library()->showCatalog();
        catalog->open();
    };
    libraryView->catalogSelect = [this](int index) { catalog->select(index); };
    libraryView->catalogMore = [this] { catalog->more(); };
    libraryView->catalogBack = [this] {
        if (catalog->back())
            return;
        catalog->close();
        start->library()->closeCatalog();
    };
    libraryView->catalogChooseFolder = chooseCatalogFolder;
    libraryView->chooseDownloadFolder = [this](const QString &path) {
        catalogFolderPending = true;
        folderBrowser->select(path);
    };
    connect(catalog, &opds::Browser::loading, this,
            [this] { start->library()->setCatalogLoading(); });
    connect(catalog, &opds::Browser::listed, this,
            [this](const opds::Listing &listing, bool appended) {
                start->library()->setCatalogListing(listing, appended);
            });
    connect(catalog, &opds::Browser::itemChanged, this, [this](int index, const QString &detail) {
        start->library()->setCatalogItemDetail(index, detail);
    });
    connect(catalog, &opds::Browser::folderNeeded, this, chooseCatalogFolder);
    connect(catalog, &opds::Browser::downloaded, this, [this](const QString &path) {
        if (closing)
            return;
        // A download folder outside the Library becomes a Library folder, so the book appears.
        const auto folder = QFileInfo(path).absolutePath();
        bool known = false;
        for (const auto &entry : store.index().folders)
            known = known || folder == entry.key || folder.startsWith(entry.key + QLatin1Char('/'));
        if (known)
            store.scan();
        else
            store.addFolder({ folder, folder.section('/', -1) });
    });
    connect(start, &standalone::StartScreen::addFolderRequested, this,
            [this, folderStorageRoot] { start->library()->showFolderPicker(folderStorageRoot); });
    connect(start, &standalone::StartScreen::libraryOpenRequested, this,
            [this, folderStorageRoot] { start->library()->showBookPicker(folderStorageRoot); });
    connect(start, &standalone::StartScreen::removeFolderRequested, &store,
            &library::Store::removeFolder);
    connect(start, &standalone::StartScreen::removeBookRequested, &store,
            &library::Store::removeBook);
    connect(start, &standalone::StartScreen::markUnreadRequested, this, [this](const QString &key) {
        if (controller.resetBook(key))
            store.setMark(key, library::Book::Mark::Unread);
        else
            start->showLibraryError(
                    tr("The book's reading settings and position could not be reset."));
    });
    auto *query = start->library()->findChild<QLineEdit *>("librarySearchQuery");
    if (query) {
        libraryQuery = query;
        libraryKeyboard = readerServices(device).keyboard(start->library(), query);
    }
    connect(&controller, &standalone::ReaderController::openingProgress, start,
            &standalone::StartScreen::setProgress);
    connect(&controller, &standalone::ReaderController::openingCover, start,
            &standalone::StartScreen::setCover);
    connect(&controller, &standalone::ReaderController::opened, this, [this] {
        auto *reader = controller.window();
        canvas->setContent(reader);
        stack->setCurrentWidget(canvas);
        start->releaseOpening();
        sizeCanvas();
        reader->show();
        reader->checkpoint();
        if (device->state().sleeping) {
            reader->sleepReader();
            reader->sleepViewReady();
            updateSleepView();
        }
        platform::pageRefresh(this, true);
        emit bookOpened();
    });
    connect(&controller, &standalone::ReaderController::failed, this, [this](const QString &error) {
        failed = true;
        start->showLibraryError(error);
        stack->setCurrentWidget(start);
        emit bookFailed(error);
    });
    connect(&controller, &standalone::ReaderController::retiring, this, [this] {
        if (stack->currentWidget() != canvas || !controller.window())
            return;
        // Reader destruction precedes asynchronous engine/resource cleanup.
        // Retain its visible frame until the Library can replace it.
        retirementView->setPixmap(grab());
        retirementView->setGeometry(rect());
        retirementView->show();
        retirementView->raise();
        if (device->state().sleeping)
            sleepView->raise();
    });
    connect(&controller, &standalone::ReaderController::closed, this, [this] {
        recordProgress();
        start->applyLibraryTheme(controller.preferences(), controller.fontSources());
        if (!failed)
            start->showLibrary();
        stack->setCurrentWidget(start);
        this->layout()->activate();
        sizeCanvas();
        retirementView->hide();
        retirementView->setPixmap({});
        platform::pageRefresh(this, true);
        repaint();
        emit bookClosed();
        if (closing)
            close();
    });
    connect(device.get(), &Device::initializedChanged, this, [this] {
        if (!pendingOpen.isEmpty())
            openBook(std::exchange(pendingOpen, {}), std::exchange(pendingPosition, {}));
        if (device->initialized()) {
            platform::pageRefresh(this, true);
            update();
        }
    });
    QTimer::singleShot(0, this, [this] {
        if (!QFileInfo(libraryRoot).isDir()) {
            start->showLibraryError(tr("Library folder %1 does not exist.").arg(libraryRoot));
            return;
        }
        bool known = false;
        for (const auto &folder : store.index().folders)
            known = known || folder.key == libraryRoot;
        if (!known)
            store.addFolder({ libraryRoot, QFileInfo(libraryRoot).fileName() });
        else if (store.index().settings.checkAtStart)
            store.scan();
    });
}

void Window::startDeviceLifecycle()
{
    device->startLifecycle(qEnvironmentVariable("KOBALT_KINDLE_POWER_BRIDGE",
                                                QCoreApplication::applicationDirPath()
                                                        + QStringLiteral("/kobalt-power-bridge")));
}
#ifdef KOBALT_KINDLE_NATIVE_TESTING
void Window::simulatePowerEvent(const QByteArray &event)
{
    device->simulatePowerEvent(event);
}

void Window::simulateBattery(int percent, bool charging)
{
    device->simulateBattery(percent, charging);
}
#endif
bool Window::updateRequested() const
{
    return updater->requested();
}

void Window::updateSleepView()
{
    if (!device->state().sleeping)
        return;
    if (windowHandle())
        ::kindle::requestSleepFrame(*windowHandle());
    sleepView->setGeometry(rect());
    const auto &frame = device->sleepFrame();
    // Outside a book, the Library shows one of the reader's wallpapers.
    if (frame.isNull() && !controller.window() && !wallpapers->image().isNull()) {
        sleepView->setText({});
        sleepView->setPixmap(
                QPixmap::fromImage(wallpapers->image())
                        .scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else if (frame.isNull()) {
        sleepView->setPixmap({});
        sleepView->setText(tr("Kobalt\nSleeping"));
    } else {
        sleepView->setPixmap(QPixmap::fromImage(frame).scaled(size(), Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation));
    }
    sleepView->show();
    sleepView->raise();
    platform::pageRefresh(this, true);
}

void Window::prepareWallpaper()
{
    const auto *screen = QGuiApplication::primaryScreen();
    const auto pixels = screen ? screen->size() * screen->devicePixelRatio() : size();
    wallpapers->prepare(pixels.isEmpty() ? QSize(1072, 1448) : pixels,
                        !qApp->property("kobalt.colorDisplay").toBool());
}

void Window::sleepingChanged(bool sleeping)
{
    if (windowHandle()) {
        windowHandle()->setProperty("kobalt.inputBlocked", sleeping);
        QEvent cancel(QEvent::UngrabMouse);
        QCoreApplication::sendEvent(windowHandle(), &cancel);
    }
    if (sleeping) {
        if (libraryKeyboard)
            libraryKeyboard->hide();
        // The account editor owns its keyboard and closes with it.
        start->library()->closeCatalogAccount();
        if (auto *reader = controller.window()) {
            reader->sleepReader();
            reader->sleepViewReady();
            standby.start();
        }
        updateSleepView();
    } else {
        standby.stop();
        if (windowHandle())
            ::kindle::wakeDisplay(*windowHandle());
        sleepView->hide();
        // Random picks again after each wake, as the reader's Random wallpaper does.
        prepareWallpaper();
        if (auto *reader = controller.window()) {
            reader->prepareWake();
            reader->wakeReader();
        }
        sizeCanvas();
        platform::pageRefresh(this, true);
        update();
    }
}

Window::~Window()
{
    qApp->removeEventFilter(this);
    standby.stop();
    device->stop();
    libraryKeyboard.reset();
    platform::setReaderRefreshWindow(nullptr);
    // The start screen's destructor disconnects Store callbacks.
    delete stack;
}

bool Window::libraryContains(const QString &path) const
{
    return store.index().books.contains(QFileInfo(path).absoluteFilePath());
}

void Window::sizeCanvas()
{
    if (preview) {
        const QSize pixels = previewDimensions;
        const qreal scale =
                qMin(qreal(width()) / pixels.width(), qreal(height()) / pixels.height());
        if (auto *reader = controller.window())
            reader->resize(pixels);
        canvas->setTransform(QTransform::fromScale(scale, scale));
        canvas->setSceneRect(QRectF(QPointF(), pixels));
        start->setLibraryScale(pixels, scale, true, 1);
    } else {
        canvas->fitContent();
    }
    if (libraryKeyboard)
        libraryKeyboard->place();
}

void Window::setPreview(bool enabled)
{
    preview = enabled;
    sizeCanvas();
}

void Window::setPreviewResolution(const QSize &size)
{
    if (size.isValid()) {
        previewDimensions = size;
        sizeCanvas();
    }
}

void Window::setBottomInset(int pixels)
{
    bottomBand = qMax(0, pixels);
    if (bottomBand == 0)
        return;
    // The Library and the opening view end above the band; paper fills it.
    // A reader opened later leaves the band out through its safe area.
    start->setAutoFillBackground(true);
    start->setContentsMargins(0, 0, 0, qCeil(bottomBand / devicePixelRatioF()));
}

QWidget *Window::startScreen() const
{
    return start;
}

QSize Window::readerPixels() const
{
    if (preview)
        return previewDimensions;
    // The hidden canvas has not received its first resize yet. Open at the
    // space allocated to it, rather than paginating at its default size.
    const auto size = stack->contentsRect().size();
    const auto dpr = canvas->devicePixelRatioF();
    return { qRound(size.width() * dpr), qRound(size.height() * dpr) };
}

void Window::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    retirementView->setGeometry(rect());
    sizeCanvas();
    updateSleepView();
}

void Window::recordProgress()
{
    if (openedKey.isEmpty())
        return;
    const auto progress = library::savedProgress(controller.savedState(openedKey));
    store.setProgress(std::exchange(openedKey, {}), progress.percent, progress.finished);
    store.save();
}

void Window::openBook(const QString &path, const QString &position)
{
    if (closing || !controller.idle())
        return;
    if (!device->initialized()) {
        pendingOpen = path;
        pendingPosition = position;
        start->showOpening();
        return;
    }
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (store.index().books.contains(absolute) || store.addBook(absolute)) {
        openedKey = absolute;
        store.setOpened(absolute);
    }
    failed = false;
    if (libraryKeyboard)
        libraryKeyboard->hide();
    start->showOpening(absolute);
    stack->setCurrentWidget(start);
    // Use the allocated reading area before the hidden canvas is first shown.
    layout()->activate();
    controller.open(absolute, readerPixels(), {}, false, position);
}

void Window::closeBook()
{
    if (!pendingOpen.isEmpty()) {
        pendingOpen.clear();
        start->showLibrary();
    }
    controller.close();
}

void Window::closeEvent(QCloseEvent *event)
{
    if (controller.idle()) {
        if (QGuiApplication::platformName() == QStringLiteral("kindle") && !deviceFailure
            && !device->state().sleeping && !exitFrameReady) {
            event->ignore();
            if (!std::exchange(exitFramePending, true)) {
                closing = true;
                pendingOpen.clear();
                sleepView->setText(
                        QStringLiteral(
                                "<div>%1</div><div style='font-size:32px; margin-top:24px'>%2</div>")
                                .arg(tr("Returning to Kindle…").toHtmlEscaped(),
                                     tr("This may take a few minutes.").toHtmlEscaped()));
                sleepView->setGeometry(rect());
                sleepView->show();
                sleepView->raise();
                windowHandle()->setProperty("kobalt.inputBlocked", true);
                windowHandle()->setProperty("kobalt.exitRequested", true);
                platform::pageRefresh(this, true);
                repaint();
                // A failed driver must not prevent saved-state shutdown and
                // launcher recovery. The kernel's refresh wait is five seconds.
                QTimer::singleShot(6500, this, [this] {
                    if (!exitFrameReady) {
                        qWarning("kindle: exit feedback was not acknowledged before recovery");
                        exitFrameReady = true;
                        close();
                    }
                });
            }
            return;
        }
        closing = true;
        pendingOpen.clear();
        store.save();
        event->accept();
        return;
    }
    event->ignore();
    if (!std::exchange(closing, true))
        controller.close();
}

bool Window::eventFilter(QObject *object, QEvent *event)
{
    if (object == windowHandle() && event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()
                == "kobalt.exitPresented"
        && object->property("kobalt.exitPresented").toBool() && exitFramePending) {
        exitFrameReady = true;
        QTimer::singleShot(0, this, &QWidget::close);
    }
    if (object == qApp && event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()
                == "kobalt.displayFailure") {
        deviceFailure = true;
        close();
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick
        || event->type() == QEvent::MouseMove || event->type() == QEvent::TouchBegin
        || event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchEnd) {
        if (device->state().sleeping)
            return true;
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::TouchBegin)
            device->activity();
    }
    if (libraryKeyboard && object == libraryQuery) {
        if (event->type() == QEvent::Show || event->type() == QEvent::FocusIn
            || event->type() == QEvent::MouseButtonPress)
            libraryKeyboard->show();
        else if (event->type() == QEvent::Hide)
            libraryKeyboard->hide();
    }
    return QWidget::eventFilter(object, event);
}
} // namespace kobalt::kindle
