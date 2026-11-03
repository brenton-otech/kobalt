#include "mtk_display.h"
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstring>
#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <unistd.h>
#include <zlib.h>
#endif

namespace kindle::mtk {
#ifdef __linux__
namespace {
QByteArray readWaveformHeader(const QByteArray &path) {
    const auto read = [](const QByteArray &file) {
        QByteArray header;
        if (auto *waveform = gzopen(file.constData(), "rb")) {
            header.resize(0x51);
            const int count = gzread(waveform, header.data(), unsigned(header.size()));
            gzclose(waveform);
            if (count != header.size())
                header.clear();
        }
        return header;
    };
    if (QFile::exists(QString::fromLocal8Bit(path)))
        return read(path);
    if (!path.startsWith("/mnt/wfm/"))
        return {};
    const auto relative = path.mid(9);
    if (relative.isEmpty() || relative.startsWith('/') || relative.split('/').contains(".."))
        return {};
    // Firmware unmounts its waveform volume after loading it. Read the header
    // through a private, read-only mount. Do not call wfm_mount: that stock
    // helper formats the partition when mounting fails.
    // The supervisor owns this directory and can unmount it after SIGKILL.
    const auto runDirectory = qEnvironmentVariable("KOBALT_KINDLE_RUN_DIR");
    if (!QDir::isAbsolutePath(runDirectory) || !QFileInfo(runDirectory).isDir())
        return {};
    QTemporaryDir directory(QDir(runDirectory).filePath(QStringLiteral("waveform-XXXXXX")));
    if (!directory.isValid())
        return {};
    const auto point = QFile::encodeName(directory.path());
    if (::mount("/dev/mmcblk0p2", point.constData(), "vfat", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) < 0)
        return {};
    auto header = read(point + '/' + relative);
    if (::umount2(point.constData(), 0) < 0 && ::umount2(point.constData(), MNT_DETACH) < 0) {
        directory.setAutoRemove(false);
        qCritical("kindle: could not detach read-only waveform mount %s", point.constData());
        header.clear();
    }
    return header;
}
}
#endif
QImage initialFrame(const QSize &size, bool isColor) {
    if (isColor) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(0xffffffff);
        return image;
    }
    QImage image(size, QImage::Format_Grayscale8);
    // QPA construction precedes font initialization. The QColor overload uses
    // a painter for grayscale images and cannot run at this point.
    image.fill(uint(255));
    return image;
}
bool supportsSwipe(const QByteArray &header, const QByteArray &loadedName) {
    if (header.size() < 0x51 || loadedName.isEmpty() || loadedName.size() >= 0x50)
        return false;
    const auto end = header.indexOf('\0');
    return end == loadedName.size() && header.left(end) == loadedName && uchar(header[0x50]) == 0x59;
}
bool validLayout(const Layout &layout) {
    const int bpp = layout.bytesPerPixel > 0 ? layout.bytesPerPixel : 1;
    return layout.width > 0 && layout.height > 0 && layout.width <= 8192 && layout.height <= 8192 &&
        (bpp == 1 || bpp == 4) &&
        layout.stride >= size_t(layout.width) * size_t(bpp) && layout.stride <= 65536 &&
        layout.bytes >= layout.stride * size_t(layout.height);
}
bool copyPixels(unsigned char *destination, const Layout &layout, const QImage &pixels, const QRect &area) {
    if (!destination || !validLayout(layout) || area.isEmpty() ||
        pixels.size() != QSize(layout.width, layout.height) || !pixels.rect().contains(area))
        return false;
    const int bpp = layout.bytesPerPixel > 0 ? layout.bytesPerPixel : 1;
    if (bpp == 4) {
        if (pixels.format() != QImage::Format_RGB32 && pixels.format() != QImage::Format_ARGB32_Premultiplied)
            return false;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            auto *out = reinterpret_cast<uint32_t *>(destination + size_t(y) * layout.stride) + area.left();
            const auto *in = reinterpret_cast<const uint32_t *>(pixels.constScanLine(y)) + area.left();
            if (layout.inverted) {
                for (int x = 0; x < area.width(); ++x)
                    out[x] = in[x] ^ 0x00FFFFFF;
            } else {
                std::memcpy(out, in, size_t(area.width()) * 4);
            }
        }
        return true;
    }
    if (pixels.format() != QImage::Format_Grayscale8)
        return false;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        auto *out = destination + size_t(y) * layout.stride + size_t(area.left());
        const auto *in = pixels.constScanLine(y) + area.left();
        if (layout.inverted) {
            // Preserve the firmware's global grayscale mode. The driver will
            // invert this buffer in night mode; pixels already include theme.
            for (int x = 0; x < area.width(); ++x)
                out[x] = 255 - in[x];
        } else {
            std::memcpy(out, in, size_t(area.width()));
        }
    }
    return true;
}
Update makeUpdate(const QRect &area, bool full, uint32_t marker, int direction, bool swipeAvailable, bool isColor) {
    Update update{};
    update.region = {uint32_t(area.y()), uint32_t(area.x()), uint32_t(area.width()), uint32_t(area.height())};
    if (isColor) {
        update.waveform = full ? 14 : 257; // 14 = MTK_WAVEFORM_MODE_GCC16 for full color; 257 = AUTO
    } else {
        update.waveform = full ? 2 : 257; // 2 = GC16; 257 = AUTO
    }
    update.mode = full ? 1 : 0;
    update.marker = marker;
    update.temperature = 0x1000; // Ambient sensor.
    if (!full && swipeAvailable && (direction == -1 || direction == 1) &&
        area.width() >= 12 && area.height() >= 12) {
        update.flags = 0x10000;
        update.swipeDirection = direction > 0 ? 2 : 3; // Logical left/right; kernel rotates.
        update.swipeSteps = 12;
    }
    return update;
}
QRect alignEpdcDamage(const QRect &area, int screenWidth) {
    if (area.isEmpty())
        return {};
    const int left = area.left() & ~7;
    const int right = qMin(screenWidth, (area.right() + 8) & ~7);
    return QRect(left, area.top(), right - left, area.height());
}
EpdcUpdate makeEpdcUpdate(const QRect &area, bool full, uint32_t marker) {
    EpdcUpdate update{};
    update.region = {uint32_t(area.top()), uint32_t(area.left()), uint32_t(area.width()), uint32_t(area.height())};
    update.waveform_mode = full ? 2 : 0x101; // GC16 or AUTO
    update.update_mode = full ? 1 : 0;
    update.update_marker = marker;
    update.hist_bw_waveform_mode = 1; // DU for AUTO's black/white histogram
    update.hist_gray_waveform_mode = 3; // GC16_FAST for AUTO's gray histogram
    update.temp = 0x1000; // TEMP_USE_AMBIENT
    update.flags = 0;
    return update;
}
Display::Display() = default;
Display::~Display() {
#ifdef __linux__
    if (mapping)
        ::munmap(mapping, layout_.bytes);
    if (fd >= 0)
        ::close(fd);
#endif
}
bool Display::open() {
#ifdef __linux__
    if (fd >= 0)
        return mapping != nullptr;
    fd = ::open("/dev/fb0", O_RDWR | O_CLOEXEC);
    fb_var_screeninfo variable{};
    fb_fix_screeninfo fixed{};
    if (fd < 0 || ::ioctl(fd, FBIOGET_VSCREENINFO, &variable) < 0 ||
        ::ioctl(fd, FBIOGET_FSCREENINFO, &fixed) < 0) {
        error_ = QStringLiteral("Cannot inspect Kindle framebuffer: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    layout_ = {int(variable.xres), int(variable.yres), fixed.line_length, fixed.smem_len,
               variable.bits_per_pixel == 32 ? 4 : 1, variable.grayscale == 2};
    rotation_ = variable.rotate;
    if (std::strncmp(fixed.id, "hwtcon_v2", sizeof(fixed.id)) == 0) {
        controllerType_ = ControllerType::Mtk;
    } else if (std::strncmp(fixed.id, "mxc_epdc_fb", sizeof(fixed.id)) == 0) {
        controllerType_ = ControllerType::Epdc;
    } else {
        error_ = QStringLiteral("Unsupported framebuffer layout; expected hwtcon_v2 or mxc_epdc_fb");
        return false;
    }
    const bool validColor = (variable.bits_per_pixel == 32 && fixed.type == FB_TYPE_PACKED_PIXELS);
    const bool validGray = (variable.bits_per_pixel == 8 && (variable.grayscale == 1 || variable.grayscale == 2) &&
                            fixed.type == FB_TYPE_PACKED_PIXELS);
    if ((!validColor && !validGray) || variable.xoffset || variable.yoffset ||
        rotation_ > 3 || !validLayout(layout_)) {
        error_ = QStringLiteral("Unsupported framebuffer layout; expected 8-bit grayscale or 32-bit color, first plane");
        return false;
    }
    void *memory = ::mmap(nullptr, layout_.bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        error_ = QStringLiteral("Cannot map Kindle framebuffer: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    mapping = static_cast<unsigned char *>(memory);
    if (controllerType_ == ControllerType::Mtk) {
        // Match the file header to the waveform the kernel actually loaded. A
        // controller name alone does not prove that swipe waveforms are available.
        QFile pathFile(QStringLiteral("/sys/module/hwtcon_v2/parameters/waveform_path"));
        QFile nameFile(QStringLiteral("/proc/eink/waveform/human_version"));
        if (pathFile.open(QIODevice::ReadOnly) && nameFile.open(QIODevice::ReadOnly)) {
            const auto path = pathFile.read(4096).trimmed();
            const auto loadedName = nameFile.read(256).trimmed();
            if (path.startsWith('/') && !path.contains('\0')) {
                swipeAvailable_ = supportsSwipe(readWaveformHeader(path), loadedName);
            }
        }
    } else {
        swipeAvailable_ = false;
    }
    qInfo("kindle: native %s %dx%d (%s) stride=%zu rotation=%u inverted=%d swipe=%d",
          controllerType_ == ControllerType::Mtk ? "hwtcon_v2" : "mxc_epdc_fb",
          layout_.width, layout_.height, layout_.bytesPerPixel == 4 ? "32-bit color" : "8-bit gray",
          layout_.stride, rotation_, layout_.inverted, swipeAvailable_);
    return true;
#else
    error_ = QStringLiteral("Kindle framebuffer requires Linux");
    return false;
#endif
}
RefreshResult Display::present(const RefreshFrame &frame) {
#ifdef __linux__
    QRect area;
    if (controllerType_ == ControllerType::Epdc) {
        const auto unaligned = frame.full ? frame.pixels.rect() : frame.damage.boundingRect().intersected(frame.pixels.rect());
        area = frame.full ? unaligned : alignEpdcDamage(unaligned, layout_.width);
    } else {
        area = frame.full ? frame.pixels.rect() : frame.damage.boundingRect().intersected(frame.pixels.rect());
    }
    if (!copyPixels(mapping, layout_, frame.pixels, area))
        return {false, true, QStringLiteral("Invalid framebuffer snapshot or damage")};
    if (!++marker)
        ++marker;
    QElapsedTimer elapsed;
    elapsed.start();
    int submitResult = -1;
    int submitErrno = 0;
    if (controllerType_ == ControllerType::Epdc) {
        auto update = makeEpdcUpdate(area, frame.full, marker);
        submitResult = ::ioctl(fd, epdcSendUpdate, &update);
        submitErrno = errno;
    } else {
        auto update = makeUpdate(area, frame.full, marker, frame.animationDirection, swipeAvailable_, isColor());
        submitResult = ::ioctl(fd, sendUpdate, &update);
        submitErrno = errno;
    }
    if (submitResult < 0)
        return {false, submitErrno != EBUSY && submitErrno != EAGAIN,
                QStringLiteral("Kindle refresh submission failed: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(submitErrno)))};
    const auto submitted = elapsed.elapsed();
    Completion completion{marker, 0};
    const int result = ::ioctl(fd, waitComplete, &completion);
    const auto finished = elapsed.elapsed();
    if (qEnvironmentVariableIsSet("KOBALT_KINDLE_DISPLAY_TRACE"))
        qInfo("kindle: refresh marker=%u full=%d area=%dx%d controller=%s submit=%lldms wait=%lldms",
              marker, frame.full, area.width(), area.height(),
              controllerType_ == ControllerType::Mtk ? "hwtcon_v2" : "mxc_epdc_fb",
              static_cast<long long>(submitted), static_cast<long long>(finished - submitted));
    // This firmware returns zero even when its five-second wait expires.
    // Never overwrite the buffer after an uncertain completion.
    if (result < 0 || finished - submitted >= 4900)
        return {false, true, QStringLiteral("Kindle refresh completion is uncertain")};
    return {};
#else
    Q_UNUSED(frame);
    return {false, true, QStringLiteral("Kindle framebuffer requires Linux")};
#endif
}
}
