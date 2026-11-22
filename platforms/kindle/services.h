#pragma once
#include "platform/api/reader_host_services.h"

namespace kobalt::kindle {
class Device;
// wallpaperDirectories enables the Wallpaper background of the sleep-screen editor.
ReaderHostServices readerServices(std::shared_ptr<Device> device,
                                  QStringList wallpaperDirectories = {});
} // namespace kobalt::kindle
