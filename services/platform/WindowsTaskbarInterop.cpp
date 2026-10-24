#include "WindowsTaskbarInterop.h"
#ifdef Q_OS_WIN
#include <objbase.h>
namespace EchoMusic
{
ITaskbarList3 *taskbarListFrom(void *value)
{
    return static_cast<ITaskbarList3 *>(value);
}
ITaskbarList3 *acquireTaskbarList3()
{
    const auto create = [] {
        ITaskbarList3 *list = nullptr;
        const HRESULT hr =
            CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list));
        return SUCCEEDED(hr) ? list : nullptr;
    };
    ITaskbarList3 *list = create();
    if (!list) {
        // Qt's Windows plugin initializes COM for the GUI thread only in some
        // configurations, and another component may have chosen a different
        // apartment. An explicit apartment attempt is the only way to tell.
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            list = create();
    }
    if (!list)
        return nullptr;
    if (FAILED(list->HrInit())) {
        list->Release();
        return nullptr;
    }
    return list;
}
HBITMAP createTaskbarDib(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return nullptr;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = image.width();
    header.bV5Height = -image.height();
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x0000ff;
    header.bV5AlphaMask = 0xff000000;
    void *bits = nullptr;
    const HDC dc = GetDC(nullptr);
    const HBITMAP bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&header),
                                            DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!bitmap || !bits) {
        if (bitmap)
            DeleteObject(bitmap);
        return nullptr;
    }
    const auto stride = size_t(image.width()) * 4;
    auto *destination = static_cast<uchar *>(bits);
    for (int y = 0; y < image.height(); ++y)
        memcpy(destination + stride * size_t(y), image.constScanLine(y), stride);
    return bitmap;
}
HICON createTaskbarIcon(const QImage &image)
{
    const HBITMAP color = createTaskbarDib(image);
    if (!color)
        return nullptr;
    const HBITMAP mask = CreateBitmap(image.width(), image.height(), 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    const HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    if (mask)
        DeleteObject(mask);
    return icon;
}
} // namespace EchoMusic
#endif
