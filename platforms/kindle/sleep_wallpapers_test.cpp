#include "sleep_wallpapers.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

using kobalt::kindle::SleepWallpapers;

namespace {
void require(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

// Waits for one prepared() signal, bounded so a lost decode fails the check.
bool waitPrepared(SleepWallpapers &wallpapers)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&wallpapers, &SleepWallpapers::prepared, &loop, [&] {
        done = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return done;
}
} // namespace

// The folder setting, its file, and the Library's random picture.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir scratch;
    require(scratch.isValid(), "No scratch folder");
    const QDir root(scratch.path());
    const auto settings = root.filePath("sleep-wallpapers.json");

    // Without a saved choice the default folder is used.
    SleepWallpapers first(settings, root.filePath("default"));
    require(first.folder() == root.filePath("default")
                    && first.directories() == QStringList{ root.filePath("default") },
            "The default folder was not used");

    // The choice is saved and read back; other fields in the file survive.
    {
        QFile file(settings);
        require(file.open(QIODevice::WriteOnly) && file.write(R"({"other": 7})") > 0,
                "Could not seed the settings file");
    }
    require(SleepWallpapers::writeFolder(settings, root.filePath("pictures")),
            "The folder was not saved");
    require(SleepWallpapers::readFolder(settings) == root.filePath("pictures"),
            "The saved folder was not read back");
    {
        QFile file(settings);
        require(file.open(QIODevice::ReadOnly)
                        && QJsonDocument::fromJson(file.readAll()).object().value("other").toInt()
                                == 7,
                "Another field in the settings file was lost");
    }
    require(SleepWallpapers::readFolder(root.filePath("missing.json")).isEmpty(),
            "A missing settings file gave a folder");

    // A picture in the chosen folder is prepared to cover the screen exactly.
    QDir().mkpath(root.filePath("pictures"));
    QImage wide(400, 100, QImage::Format_RGB32);
    wide.fill(Qt::darkGray);
    require(wide.save(root.filePath("pictures/wide.png")), "Could not write a picture");
    SleepWallpapers wallpapers(settings, root.filePath("default"));
    require(wallpapers.folder() == root.filePath("pictures"), "The saved folder was not used");
    require(wallpapers.image().isNull(), "A picture existed before preparing");
    const QSize screen(150, 200);
    wallpapers.prepare(screen, true);
    require(waitPrepared(wallpapers), "No picture was prepared");
    require(wallpapers.image().size() == screen
                    && wallpapers.image().format() == QImage::Format_Grayscale8,
            "The prepared picture does not cover the screen in grayscale");
    wallpapers.prepare(screen, false);
    require(waitPrepared(wallpapers) && wallpapers.image().format() == QImage::Format_RGB32,
            "A color panel did not get a color picture");

    // A new folder without pictures leaves the Library on its text.
    QDir().mkpath(root.filePath("empty"));
    wallpapers.setFolder(root.filePath("empty"));
    require(waitPrepared(wallpapers) && wallpapers.image().isNull(),
            "An empty folder kept the previous picture");
    QThreadPool::globalInstance()->waitForDone();
    require(SleepWallpapers::readFolder(settings) == root.filePath("empty"),
            "Changing the folder did not save it");
    return 0;
}
