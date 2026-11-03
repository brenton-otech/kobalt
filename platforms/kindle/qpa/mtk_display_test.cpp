#include "mtk_display.h"
#include <QCoreApplication>
#include <QByteArray>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
}
int main(int argc, char **argv) {
    // The real QPA allocates this before QGuiApplication can provide fonts.
    const auto initial = kindle::mtk::initialFrame(QSize(5, 4));
    check(initial.format() == QImage::Format_Grayscale8 && initial.constScanLine(0)[0] == 255,
          "Framebuffer initialization requires an application or is not white");
    QCoreApplication app(argc, argv);
    using namespace kindle::mtk;
    QByteArray header(0x51, '\0');
    header.replace(0, 4, "TEST");
    header[0x50] = 0x59;
    check(supportsSwipe(header, "TEST"), "Matching TL waveform not detected");
    check(!supportsSwipe(header, "OTHER") && !supportsSwipe(header.left(0x50), "TEST") &&
          !supportsSwipe(header, {}), "Unverified waveform capability accepted");
    header[0x50] = 0x58;
    check(!supportsSwipe(header, "TEST"), "Non-TL waveform advertised swipe");
    Layout layout{5, 4, 8, 32, false};
    QImage pixels(5, 4, QImage::Format_Grayscale8);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            pixels.scanLine(y)[x] = uchar(y * 10 + x);
    QByteArray memory(40, char(0x77));
    const auto original = memory;
    const QRect area(1, 1, 3, 2);
    check(copyPixels(reinterpret_cast<uchar *>(memory.data()), layout, pixels, area), "Valid copy rejected");
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 8; ++x)
            check(uchar(memory[y * 8 + x]) == (area.contains(x, y) ? y * 10 + x : 0x77),
                  "Copy corrupted stride padding, untouched pixels or guard bytes");
    layout.inverted = true;
    check(copyPixels(reinterpret_cast<uchar *>(memory.data()), layout, pixels, area), "Inverted copy rejected");
    check(uchar(memory[9]) == 244 && uchar(memory[18]) == 233, "Firmware inversion not compensated");
    memory = original;
    layout.bytes = 31;
    check(!copyPixels(reinterpret_cast<uchar *>(memory.data()), layout, pixels, area) && memory == original,
          "Short mapping accepted");
    layout.bytes = 32;
    check(!copyPixels(reinterpret_cast<uchar *>(memory.data()), layout, pixels, QRect(-1, 0, 5, 4)),
          "Negative damage accepted");
    check(!copyPixels(reinterpret_cast<uchar *>(memory.data()), layout, pixels, QRect(0, 0, 6, 4)),
          "Out-of-bounds damage accepted");
    check(!copyPixels(nullptr, layout, pixels, area), "Null mapping accepted");
    check(!copyPixels(reinterpret_cast<uchar *>(memory.data()), layout,
                      pixels.convertToFormat(QImage::Format_RGB32), area), "RGB buffer accepted as gray bytes");
    layout.stride = 4;
    check(!validLayout(layout), "Short scanline accepted");
    layout = {8193, 1, 8193, 8193, false};
    check(!validLayout(layout), "Unbounded dimensions accepted");
    const auto full = makeUpdate(QRect(0, 0, 1236, 1648), true, 42, 1, true);
    check(full.mode == 1 && full.waveform == 2 && full.marker == 42 && !full.flags &&
          full.temperature == 0x1000, "Full refresh ABI incorrect");
    const auto forward = makeUpdate(QRect(2, 3, 1200, 1600), false, 43, 1, true);
    check(forward.flags == 0x10000 && forward.swipeDirection == 2 && forward.swipeSteps == 12 &&
          forward.region.top == 3 && forward.region.left == 2 && forward.waveform == 257,
          "Forward swipe ABI incorrect");
    check(makeUpdate(QRect(0, 0, 1200, 1600), false, 44, -1, true).swipeDirection == 3,
          "Backward swipe direction incorrect");
    check(!makeUpdate(QRect(0, 0, 11, 1600), false, 44, 1, true).flags &&
          !makeUpdate(QRect(0, 0, 1200, 1600), false, 44, 1, false).flags &&
          !makeUpdate(QRect(0, 0, 1200, 1600), false, 44, 2, true).flags,
          "Unsafe or unsupported swipe accepted");

    // EPDC (Kindle PW3) alignment and ABI tests
    const auto emptyDamage = alignEpdcDamage({}, 1072);
    check(emptyDamage.isEmpty(), "Empty EPDC damage not empty");

    const auto aligned = alignEpdcDamage(QRect(3, 10, 15, 20), 1072);
    check(aligned.left() == 0 && aligned.top() == 10 && aligned.width() == 24 && aligned.height() == 20,
          "EPDC damage 8-pixel alignment incorrect");
    check((aligned.left() % 8 == 0) && (aligned.width() % 8 == 0),
          "EPDC left or width not multiple of 8");

    const auto clamped = alignEpdcDamage(QRect(1060, 50, 12, 100), 1072);
    check(clamped.left() == 1056 && clamped.right() == 1071 && clamped.width() == 16,
          "EPDC damage screen width boundary clamp incorrect");

    const auto epdcFull = makeEpdcUpdate(QRect(0, 0, 1072, 1448), true, 101);
    check(epdcFull.region.top == 0 && epdcFull.region.left == 0 &&
          epdcFull.region.width == 1072 && epdcFull.region.height == 1448 &&
          epdcFull.waveform_mode == 2 && epdcFull.update_mode == 1 &&
          epdcFull.update_marker == 101 && epdcFull.temp == 0x1000 &&
          epdcFull.flags == 0 && epdcFull.hist_bw_waveform_mode == 1 &&
          epdcFull.hist_gray_waveform_mode == 3, "EPDC full update ABI incorrect");

    const auto epdcPartial = makeEpdcUpdate(QRect(8, 16, 64, 32), false, 102);
    check(epdcPartial.waveform_mode == 0x101 && epdcPartial.update_mode == 0 &&
          epdcPartial.update_marker == 102 && epdcPartial.temp == 0x1000,
          "EPDC partial update ABI incorrect");

    std::puts("Kindle MTK and EPDC ABI, damage bounds, stride, inversion and swipe guards passed.");
}
