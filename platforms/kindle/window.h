#pragma once
#include "platforms/standalone/reader.h"
#include "library/library_store.h"
#include "opds/browser.h"
#include <QNetworkAccessManager>
#include <QWidget>

class QStackedWidget;
class QPushButton;
class QLabel;
class QLineEdit;

namespace kobalt::standalone {
class StartScreen;
class WidgetCanvas;
} // namespace kobalt::standalone

namespace kobalt::kindle {
class Device;
class FolderBrowser;
class Updater;
class SleepWallpapers;

class Window final : public QWidget
{
    Q_OBJECT
public:
    Window(storage::Paths paths, EngineFontInputs fonts, QString placeholder, QString libraryRoot,
           QString folderStorageRoot = QStringLiteral("/mnt/us"));
    ~Window() override;
    void openBook(const QString &path, const QString &position = {});
    void closeBook();

    ReaderWindow *readerWindow() const { return controller.window(); }

    QJsonObject savedState(const QString &path) const { return controller.savedState(path); }

    bool idle() const { return controller.idle(); }

    bool deviceFailed() const { return deviceFailure; }

    // The user chose to update; the supervisor acts on the exit status.
    bool updateRequested() const;

    bool libraryContains(const QString &path) const;
    void setPreview(bool enabled);
    void setPreviewResolution(const QSize &size);

    QSize previewResolution() const { return previewDimensions; }

    // Keep text and controls above a bottom band the bezel covers.
    void setBottomInset(int pixels);

    int bottomInset() const { return bottomBand; }

    QWidget *startScreen() const;

    void startDeviceLifecycle();
#ifdef KOBALT_KINDLE_NATIVE_TESTING
    void simulatePowerEvent(const QByteArray &event);
    void simulateBattery(int percent, bool charging);
#endif
signals:
    void bookOpened();
    void bookClosed();
    void bookFailed(QString error);

protected:
    void resizeEvent(QResizeEvent *) override;
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    std::shared_ptr<Device> device;
    standalone::ReaderController controller;
    library::Store store;
    QString openedKey, libraryRoot, pendingOpen, pendingPosition;
    QStackedWidget *stack;
    standalone::StartScreen *start;
    FolderBrowser *folderBrowser;
    // The OPDS catalog of the Library menu. Its account is opds.json in the profile.
    QNetworkAccessManager catalogNetwork;
    opds::Browser *catalog;
    bool catalogFolderPending = false;
    Updater *updater;
    // A check started by Kobalt itself reports nothing but a newer build.
    // Failed attempts, such as Wi-Fi without internet, are tried a few times.
    static constexpr int maxQuietUpdateChecks = 3;
    bool quietUpdateCheck = false, updateChecking = false;
    int quietUpdateChecks = 0;
    standalone::WidgetCanvas *canvas;
    QLabel *sleepView, *retirementView;
    SleepWallpapers *wallpapers = nullptr;
    QTimer standby;
    std::unique_ptr<KeyboardController> libraryKeyboard;
    QPointer<QLineEdit> libraryQuery;
    bool closing = false, failed = false;
    bool deviceFailure = false;
    bool exitFramePending = false, exitFrameReady = false;
    bool preview = false;
    int bottomBand = 0;
    QSize previewDimensions{ 1072, 1448 };
    QSize readerPixels() const;
    void sizeCanvas();
    void recordProgress();
    void sleepingChanged(bool sleeping);
    void updateSleepView();
    void prepareWallpaper();
};
} // namespace kobalt::kindle
