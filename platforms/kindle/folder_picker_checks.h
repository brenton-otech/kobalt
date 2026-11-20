#pragma once
#include <QString>

namespace kobalt::kindle {
class Window;
bool checkWirelessControls(Window &window, const QString &captures);
// The reader page, its strips and the Library end exactly `expected` pixels
// above the bottom edge; PW3 and PW5 expect none.
bool checkBottomInset(Window &window, int expected);
bool prepareFolderPickerChecks(const QString &root, const QString &book);
bool checkFolderPicker(Window &window, const QString &root, const QString &captures);
bool openBookFromPicker(Window &window, const QString &root, const QString &captures);
} // namespace kobalt::kindle
