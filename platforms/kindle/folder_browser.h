#pragma once
#include "app/library/library_view.h"
#include <QThread>

Q_DECLARE_METATYPE(kobalt::library::FolderLocation)

namespace kobalt::kindle {
// One worker owns directory reads. Closing the picker never joins a worker;
// only final host shutdown waits for it, after reader input has stopped.
class FolderBrowser final : public QObject
{
    Q_OBJECT
public:
    explicit FolderBrowser(QString root = QStringLiteral("/mnt/us"), QObject *parent = nullptr);
    ~FolderBrowser() override;
    void navigate(QString path, bool books = false);
    void select(QString path);
    void selectBook(QString path);
    static library::FolderLocation list(const QString &root, const QString &path,
                                        bool books = false);
    static library::FolderLocation book(const QString &root, const QString &path);
signals:
    void loading(QString path);
    void listed(kobalt::library::FolderLocation location);
    void selected(QString path);
    void bookSelected(QString path);
    void failed(QString message);
    void requested(QString path, quint64 generation, int action);
    void completed(kobalt::library::FolderLocation location, quint64 generation, int action);

private:
    enum Request { BrowseFolders, SelectFolder, BrowseBooks, SelectBook };

    QString root;
    QThread thread;
    quint64 generation = 0;
};
} // namespace kobalt::kindle
