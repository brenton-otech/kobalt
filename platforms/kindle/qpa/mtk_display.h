#pragma once
#include "refresh_queue.h"
#include <cstddef>
#include <cstdint>

namespace kindle::mtk {
// Linux userspace ABI, independently declared from Amazon's 5.19.2 source
// release (include/linux/hwtcon_ioctl_cmd.h). All fields are 32 bits on ARM.
// https://s3.amazonaws.com/kindledownloads/Kindle_src_5.19.2_4670760040.tar.gz
struct Rect { uint32_t top, left, width, height; };
struct Update {
    Rect region;
    uint32_t waveform, mode, marker;
    int32_t temperature;
    uint32_t flags;
    int32_t dither, quantization;
    uint32_t alternateAddress, alternateWidth, alternateHeight;
    Rect alternateRegion;
    uint32_t swipeDirection, swipeSteps;
    uint32_t histogramBlackWhite, histogramGray, timestampPxp, timestampEpdc;
};
struct Completion { uint32_t marker, collision; };
static_assert(sizeof(Update) == 96 && offsetof(Update, swipeDirection) == 72 &&
              offsetof(Update, histogramBlackWhite) == 80);
static_assert(sizeof(Completion) == 8);
constexpr unsigned long sendUpdate = 0x4060462e;
constexpr unsigned long waitComplete = 0xc008462f;

// Freescale / NXP i.MX6 EPDC ABI for Kindle PW3 (mxc_epdc_fb).
// Independently declared from include/linux/mxcfb.h in Amazon's PW3
// firmware 5.13.5 source. Histogram fields precede temperature.
// https://s3.amazonaws.com/kindledownloads/Kindle_src_5.13.5_3716750030.tar.gz
// All fields are 32 bits on ARM.
struct EpdcRect { uint32_t top, left, width, height; };
struct EpdcAltBufferData {
    uint32_t phys_addr;
    uint32_t width;
    uint32_t height;
    EpdcRect alt_update_region;
};
struct EpdcUpdate {
    EpdcRect region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t update_marker;
    uint32_t hist_bw_waveform_mode;
    uint32_t hist_gray_waveform_mode;
    int32_t temp;
    uint32_t flags;
    EpdcAltBufferData alt;
};
static_assert(sizeof(EpdcUpdate) == 72 && offsetof(EpdcUpdate, hist_bw_waveform_mode) == 28 &&
              offsetof(EpdcUpdate, hist_gray_waveform_mode) == 32 && offsetof(EpdcUpdate, temp) == 36 &&
              offsetof(EpdcUpdate, flags) == 40 && offsetof(EpdcUpdate, alt) == 44);
constexpr unsigned long epdcSendUpdate = 0x4048462e;

enum class ControllerType {
    Unknown,
    Mtk,  // hwtcon_v2 (Paperwhite 5, Colorsoft / MediaTek)
    Epdc  // mxc_epdc_fb (Paperwhite 3 / NXP i.MX6)
};

struct Layout {
    int width = 0, height = 0;
    size_t stride = 0, bytes = 0;
    int bytesPerPixel = 1;
    bool inverted = false;
    // 32-bit only: the framebuffer stores red in the low byte (R, G, B, X),
    // the reverse of QImage::Format_RGB32 on little-endian ARM (B, G, R, X).
    bool swapRedBlue = false;
};
// Waveform modes from Amazon's hwtcon_v2 ABI. The color modes were added with
// the Colorsoft (Bellatrix4); see doc/colorsoft.md.
constexpr uint32_t waveformGc16 = 2, waveformGl16 = 3, waveformGcc16 = 14, waveformAuto = 257;
// FBIOGET_VSCREENINFO grayscale values on Bellatrix4 color panels.
constexpr uint32_t grayscaleColor = 0, grayscaleColorNight = 3;
bool validLayout(const Layout &layout);
QImage initialFrame(const QSize &size, bool isColor = false);
bool copyPixels(unsigned char *destination, const Layout &layout, const QImage &pixels, const QRect &area);
QRect alignEpdcDamage(const QRect &area, int screenWidth);
Update makeUpdate(const QRect &area, bool full, uint32_t marker, int direction, bool swipeAvailable, bool isColor = false);
EpdcUpdate makeEpdcUpdate(const QRect &area, bool full, uint32_t marker);
bool supportsSwipe(const QByteArray &header, const QByteArray &loadedName);

// One worker owns present(). Destruction follows RefreshQueue::stop(), so the
// mapping cannot disappear while the kernel is consuming its pixels.
class Display final {
public:
    Display();
    ~Display();
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;
    bool open();
    RefreshResult present(const RefreshFrame &frame);
    ControllerType controllerType() const { return controllerType_; }
    const Layout &layout() const { return layout_; }
    bool isColor() const { return layout_.bytesPerPixel == 4; }
    QString error() const { return error_; }
    unsigned rotation() const { return rotation_; }
    bool swipeAvailable() const { return swipeAvailable_; }
private:
#ifdef __linux__
    int fd = -1;
    unsigned char *mapping = nullptr;
    uint32_t marker = 0;
#endif
    ControllerType controllerType_ = ControllerType::Unknown;
    Layout layout_;
    unsigned rotation_ = 0;
    bool swipeAvailable_ = false;
    QString error_;
};
}
