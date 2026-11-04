#pragma once
#include <QString>
namespace kobalt::kindle {
class Window;
bool prepareFolderPickerChecks(const QString &root, const QString &book);
bool checkFolderPicker(Window &window, const QString &root, const QString &captures);
bool openBookFromPicker(Window &window, const QString &root, const QString &captures);
}
