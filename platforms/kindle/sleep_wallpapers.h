#pragma once
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

namespace kobalt::kindle {
// The folder of the reader's own sleep pictures, and one picture prepared for
// a sleep outside a book. The reader's sleep-screen editor lists the same
// folder through ReaderHostServices::sleepWallpaperDirectories.
//
// The choice is kept in a small JSON file beside the profile. Reading it and
// listing the folder are reads; saving it and decoding a picture run on a
// worker, so the Library never waits for storage.
class SleepWallpapers final : public QObject
{
    Q_OBJECT
public:
    SleepWallpapers(QString settingsPath, QString defaultFolder, QObject *parent = nullptr);

    QString folder() const { return folder_; }

    QStringList directories() const { return { folder_ }; }

    // Saves the folder on a worker; a new picture is prepared from it.
    void setFolder(const QString &folder);
    // Decodes a random picture from the folder on a worker, covering `screen`.
    // `grayscale` keeps it at one byte a pixel for grayscale panels.
    void prepare(QSize screen, bool grayscale);

    // The prepared picture, or a null image when the folder has none yet.
    const QImage &image() const { return image_; }

    // The folder stored in a settings file; empty when there is none or it is unreadable.
    static QString readFolder(const QString &settingsPath);
    static bool writeFolder(const QString &settingsPath, const QString &folder);

signals:
    void prepared();

private:
    QString settingsPath, folder_;
    QImage image_;
    quint64 generation = 0;
    QSize lastScreen;
    bool lastGrayscale = false;
};
} // namespace kobalt::kindle
