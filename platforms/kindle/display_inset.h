#pragma once
#include <QLatin1String>
#include <QString>

namespace kobalt::kindle {
// The Colorsoft's bezel covers the bottom edge of its framebuffer: firmware
// 5.20.1 reports 1272x1696 pixels for a 1264x1680 panel, and text in the last
// rows sits under the bezel. Text and controls stay above this band, and paper
// fills it. About 2 mm at the panel's 300 PPI; tune it here. PW3 and PW5 show
// their whole framebuffer.
inline constexpr int colorsoftBottomInset = 24;

inline int bottomInset(const QString &target)
{
    return target == QLatin1String("colorsoft") ? colorsoftBottomInset : 0;
}
} // namespace kobalt::kindle
