#include "sleep_wallpapers.h"
#include "storage/sleep_snapshot.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>

namespace kobalt::kindle {
namespace {
const auto folderKey = QStringLiteral("sleep_wallpaper_folder");
}

SleepWallpapers::SleepWallpapers(QString settingsPath, QString defaultFolder, QObject *parent)
    : QObject(parent), settingsPath(std::move(settingsPath))
{
    folder_ = readFolder(this->settingsPath);
    if (folder_.isEmpty())
        folder_ = std::move(defaultFolder);
}

QString SleepWallpapers::readFolder(const QString &settingsPath)
{
    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(folderKey).toString();
}

bool SleepWallpapers::writeFolder(const QString &settingsPath, const QString &folder)
{
    // Keep any other field a later build adds to the same file.
    QJsonObject settings;
    {
        QFile file(settingsPath);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 64 * 1024)
            settings = QJsonDocument::fromJson(file.readAll()).object();
    }
    settings[folderKey] = folder;
    QSaveFile file(settingsPath);
    return file.open(QIODevice::WriteOnly)
            && file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented)) >= 0
            && file.commit();
}

void SleepWallpapers::setFolder(const QString &folder)
{
    if (folder.isEmpty() || folder == folder_)
        return;
    folder_ = folder;
    QThreadPool::globalInstance()->start(
            [path = settingsPath, folder] { writeFolder(path, folder); });
    if (lastScreen.isValid())
        prepare(lastScreen, lastGrayscale);
}

void SleepWallpapers::prepare(QSize screen, bool grayscale)
{
    lastScreen = screen;
    lastGrayscale = grayscale;
    const auto id = ++generation;
    QThreadPool::globalInstance()->start([guard = QPointer<SleepWallpapers>(this), id,
                                          directories = directories(), screen, grayscale] {
        auto image = storage::loadSleepWallpaper(directories, {}, screen);
        if (!image.isNull()) {
            // Fill the screen and crop the longer side, as the reader's wallpaper does.
            image = image.scaled(screen, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            image = image.copy(QRect(QPoint((image.width() - screen.width()) / 2,
                                            (image.height() - screen.height()) / 2),
                                     screen));
            image = image.convertToFormat(grayscale ? QImage::Format_Grayscale8
                                                    : QImage::Format_RGB32);
        }
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [guard, id, image = std::move(image)]() mutable {
                    if (!guard || guard->generation != id)
                        return;
                    guard->image_ = std::move(image);
                    emit guard->prepared();
                },
                Qt::QueuedConnection);
    });
}
} // namespace kobalt::kindle
