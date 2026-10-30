// 可视化：把检测框和框编号画到图上（GDI，无第三方依赖）
#include "common.h"
#include <windows.h>

namespace {

// 框线配色（按顺序循环，便于区分相邻框）
const COLORREF kColors[] = {
    RGB(0, 190, 90),    RGB(230, 90, 60),   RGB(40, 130, 240),
    RGB(200, 150, 0),   RGB(150, 70, 200),  RGB(0, 170, 180),
};
const int kColorCount = sizeof(kColors) / sizeof(kColors[0]);

int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

Image vis_draw(const Image& src, const std::vector<OcrItem>& items) {
    Image out = src;  // 失败时原样返回
    if (src.w <= 0 || src.h <= 0) return out;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = src.w;
    bi.bmiHeader.biHeight = -src.h;  // 负值 = 自顶向下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        return out;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    // 原图灌进 DIB（BGR -> BGRA）
    uint8_t* dst = (uint8_t*)bits;
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* s = src.row(y);
        uint8_t* q = dst + (size_t)y * src.w * 4;
        for (int x = 0; x < src.w; ++x) {
            q[x * 4 + 0] = s[x * 3 + 0];
            q[x * 4 + 1] = s[x * 3 + 1];
            q[x * 4 + 2] = s[x * 3 + 2];
            q[x * 4 + 3] = 255;
        }
    }

    // 线宽 / 字号随图大小走，小截图不至于糊成一团
    const int scale = clamp_int((src.w < src.h ? src.w : src.h) / 300, 1, 4);
    const int pen_w = 1 + scale;
    const int font_h = -clamp_int((src.h > src.w ? src.h : src.w) / 40, 12, 40);

    HFONT font = CreateFontW(font_h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    for (size_t i = 0; i < items.size(); ++i) {
        const OcrItem& it = items[i];
        const COLORREF cr = kColors[i % kColorCount];

        HPEN pen = CreatePen(PS_SOLID, pen_w, cr);
        HGDIOBJ oldPen = SelectObject(dc, pen);

        POINT pts[5];
        for (int k = 0; k < 4; ++k) {
            pts[k].x = (LONG)it.box[k][0];
            pts[k].y = (LONG)it.box[k][1];
        }
        pts[4] = pts[0];
        Polyline(dc, pts, 5);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        // 编号：框左上角一个小色块 + 白字
        wchar_t label[16];
        int n = 0;
        {
            size_t v = i + 1;
            wchar_t tmp[16];
            while (v && n < 15) { tmp[n++] = (wchar_t)(L'0' + v % 10); v /= 10; }
            for (int k = 0; k < n; ++k) label[k] = tmp[n - 1 - k];
            label[n] = 0;
        }
        if (!n) { label[0] = L'0'; label[1] = 0; n = 1; }

        SIZE sz{0, 0};
        GetTextExtentPoint32W(dc, label, n, &sz);
        int pad_x = 2 * scale;
        int box_h = sz.cy + scale;
        int bw = sz.cx + pad_x * 2;
        int bx = (int)it.box[0][0];
        int by = (int)it.box[0][1];
        // 编号位置：优先框左侧空白 → 框上方 → 框内左上
        int x0 = bx - bw - 2 * scale;
        int y0 = by;
        if (x0 < 0) {
            x0 = bx;
            y0 = by - box_h;
            if (y0 < 0) y0 = by;
        }
        if (x0 + bw > src.w) x0 = src.w - bw;
        if (x0 < 0) x0 = 0;
        RECT rc{x0, y0, x0 + bw, y0 + box_h};

        HBRUSH br = CreateSolidBrush(cr);
        FillRect(dc, &rc, br);
        DeleteObject(br);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, rc.left + pad_x, rc.top + scale / 2, label, n);
    }

    if (oldFont) SelectObject(dc, oldFont);
    if (font) DeleteObject(font);

    GdiFlush();
    // 读回 BGR
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* s = dst + (size_t)y * src.w * 4;
        uint8_t* q = out.row(y);
        for (int x = 0; x < src.w; ++x) {
            q[x * 3 + 0] = s[x * 4 + 0];
            q[x * 3 + 1] = s[x * 4 + 1];
            q[x * 3 + 2] = s[x * 4 + 2];
        }
    }

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return out;
}
