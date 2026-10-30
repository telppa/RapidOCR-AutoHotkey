#include "common.h"
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

// ------------------------------------------------------------------ 编码转换
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}

std::string path_dir(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::string();
    return wide_to_utf8(p.substr(0, pos));
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    buf[0] = '\\';
                    buf[1] = 'u';
                    fmt::hex4(buf + 2, (unsigned)c);  // \u00XX
                    buf[6] = 0;
                    o += buf;
                } else {
                    o += (char)c;
                }
        }
    }
    return o;
}

// ------------------------------------------------------------------ WIC
namespace {

// 读 EXIF 方向（1~8）；失败返回 1
static int read_exif_orientation(IWICBitmapFrameDecode* frame) {
    IWICMetadataQueryReader* reader = nullptr;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) return 1;
    int orient = 1;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v))) {
        if (v.vt == VT_UI2) orient = (int)v.uiVal;
        else if (v.vt == VT_UI4) orient = (int)v.ulVal;
        else if (v.vt == VT_I2) orient = (int)v.iVal;
    }
    PropVariantClear(&v);
    reader->Release();
    if (orient < 1 || orient > 8) orient = 1;
    return orient;
}

// 按 EXIF 方向纠正（与 PIL ImageOps.exif_transpose 等价）
static Image apply_orientation(const Image& src, int o) {
    switch (o) {
        case 2: return img_flip_h(src);
        case 3: return img_rotate180(src);
        case 4: return img_flip_v(src);
        case 5: return img_rot90_cw(img_flip_h(src));
        case 6: return img_rot90_cw(src);
        case 7: return img_rot90_cw(img_flip_v(src));
        case 8: return img_rot90_cw(img_rotate180(src));
        default: return src;
    }
}

// BGRA（非预乘）-> BGR。alpha_mode: 0=auto 1=drop 2=white 3=black 4=space(R/B 交换)
static void bgra_to_bgr(const uint8_t* src, size_t stride, int w, int h, int alpha_mode, Image& out) {
    const size_t npix = (size_t)w * h;
    bool has_alpha = false;
    double sum_lum = 0;
    size_t cnt = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = row + (size_t)x * 4;
            uint8_t a = p[3];
            if (a != 255) has_alpha = true;
            if (a > 0) {
                sum_lum += 0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2];  // BGR -> 亮度
                ++cnt;
            }
        }
    }
    uint8_t bg = 255;
    if (alpha_mode == 2) bg = 255;
    else if (alpha_mode == 3) bg = 0;
    else if (cnt > 0) bg = (sum_lum / cnt < 128.0) ? 255 : 0;

    out.w = w;
    out.h = h;
    out.has_alpha = has_alpha;
    out.bgr.resize(npix * 3);

    if (!has_alpha || alpha_mode == 1 || alpha_mode == 4) {
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = src + (size_t)y * stride;
            uint8_t* q = out.row(y);
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = row + (size_t)x * 4;
                if (alpha_mode == 4) {  // R/B 交换
                    q[x * 3 + 0] = p[2];
                    q[x * 3 + 1] = p[1];
                    q[x * 3 + 2] = p[0];
                } else {
                    q[x * 3 + 0] = p[0];
                    q[x * 3 + 1] = p[1];
                    q[x * 3 + 2] = p[2];
                }
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = src + (size_t)y * stride;
            uint8_t* q = out.row(y);
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = row + (size_t)x * 4;
                double a = p[3] / 255.0;
                for (int c = 0; c < 3; ++c) {
                    int v = (int)(p[c] * a + bg * (1.0 - a) + 0.5);
                    q[x * 3 + c] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
            }
        }
    }
}

bool wic_decode(IWICImagingFactory* fac, IWICBitmapDecoder* dec, Image& out, std::string& err,
                int alpha_mode) {
    IWICBitmapFrameDecode* frame = nullptr;
    HRESULT hr = dec->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { err = "GetFrame failed"; return false; }

    // 统一转 32bppBGRA：带透明通道的图像按 rapidocr 的做法合成到高对比背景上
    IWICFormatConverter* conv = nullptr;
    hr = fac->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) { frame->Release(); err = "CreateFormatConverter failed"; return false; }

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                          nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { conv->Release(); frame->Release(); err = "像素格式转换失败（不支持的图像）"; return false; }

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0) { conv->Release(); frame->Release(); err = "图像尺寸为 0"; return false; }
    if ((long long)w * h > 120000000LL) { conv->Release(); frame->Release(); err = "图像过大"; return false; }

    const UINT stride = w * 4;
    std::vector<uint8_t> raw((size_t)stride * h);
    hr = conv->CopyPixels(nullptr, stride, (UINT)raw.size(), raw.data());
    int orient = read_exif_orientation(frame);
    conv->Release();
    frame->Release();
    if (FAILED(hr)) { err = "CopyPixels failed"; return false; }

    bgra_to_bgr(raw.data(), (size_t)w * 4, (int)w, (int)h, alpha_mode, out);

    if (orient != 1) out = apply_orientation(out, orient);
    return true;
}

IWICImagingFactory* wic_factory(std::string& err) {
    static IWICImagingFactory* fac = nullptr;
    if (fac) return fac;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&fac));
    if (FAILED(hr) || !fac) { fac = nullptr; err = "创建 WIC 工厂失败"; return nullptr; }
    return fac;
}

}  // namespace

bool decode_image_file(const wchar_t* path, Image& out, std::string& err, int alpha_mode) {
    std::string e;
    IWICImagingFactory* fac = wic_factory(e);
    if (!fac) { err = e; return false; }

    IWICBitmapDecoder* dec = nullptr;
    HRESULT hr = fac->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr) || !dec) { err = "无法打开图像文件（路径错误或格式不支持）"; return false; }
    bool ok = wic_decode(fac, dec, out, err, alpha_mode);
    dec->Release();
    return ok;
}

bool decode_image_memory(const void* data, int size, Image& out, std::string& err, int alpha_mode) {
    if (!data || size <= 0) { err = "空的图像数据"; return false; }
    std::string e;
    IWICImagingFactory* fac = wic_factory(e);
    if (!fac) { err = e; return false; }

    IStream* stream = SHCreateMemStream((const BYTE*)data, (UINT)size);
    if (!stream) { err = "创建内存流失败"; return false; }

    IWICBitmapDecoder* dec = nullptr;
    HRESULT hr = fac->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr) || !dec) { stream->Release(); err = "无法解码图像数据（格式不支持）"; return false; }
    bool ok = wic_decode(fac, dec, out, err, alpha_mode);
    dec->Release();
    stream->Release();
    return ok;
}

// 从 32bpp BGRA（非预乘）像素直接构造 Image —— 给 ImagePut 的 Buffer 输入用
bool image_from_bgra(const void* bits, int w, int h, int stride, Image& out, int alpha_mode) {
    if (!bits || w <= 0 || h <= 0) return false;
    if (stride < w * 4) return false;
    if ((long long)w * h > 120000000LL) return false;
    bgra_to_bgr((const uint8_t*)bits, (size_t)stride, w, h, alpha_mode, out);
    return true;
}

// BGR 图编码成 PNG（WIC）。失败返回 false。
bool encode_png(const Image& src, std::string& out) {
    out.clear();
    if (src.w <= 0 || src.h <= 0) return false;
    std::string e;
    IWICImagingFactory* fac = wic_factory(e);
    if (!fac) return false;

    IStream* stream = SHCreateMemStream(nullptr, 0);
    if (!stream) return false;

    IWICBitmapEncoder* enc = nullptr;
    HRESULT hr = fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (FAILED(hr) || !enc) { stream->Release(); return false; }
    hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr)) hr = frame->Initialize(props);
    if (SUCCEEDED(hr)) hr = frame->SetSize((UINT)src.w, (UINT)src.h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr))
        hr = frame->WritePixels((UINT)src.h, (UINT)src.w * 3, (UINT)src.bgr.size(),
                                const_cast<BYTE*>(src.bgr.data()));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();

    if (frame) frame->Release();
    if (props) props->Release();
    if (enc) enc->Release();

    if (SUCCEEDED(hr)) {
        STATSTG st;
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) {
            ULONG n = (ULONG)st.cbSize.QuadPart;
            out.resize(n);
            LARGE_INTEGER zero;
            zero.QuadPart = 0;
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            ULONG got = 0;
            if (FAILED(stream->Read(out.data(), n, &got)) || got != n) out.clear();
        }
    }
    stream->Release();
    return !out.empty();
}

// ------------------------------------------------------------------ 基本操作
Image img_resize(const Image& src, int nw, int nh) {
    Image dst(nw, nh);
    if (nw <= 0 || nh <= 0) return dst;
    const double sx = (double)src.w / nw;
    const double sy = (double)src.h / nh;
    for (int y = 0; y < nh; ++y) {
        double fy = (y + 0.5) * sy - 0.5;
        int y0 = (int)floor(fy);
        double wy = fy - y0;
        int y0c = std::min(std::max(y0, 0), src.h - 1);
        int y1c = std::min(std::max(y0 + 1, 0), src.h - 1);
        const uint8_t* r0 = src.row(y0c);
        const uint8_t* r1 = src.row(y1c);
        uint8_t* d = dst.row(y);
        for (int x = 0; x < nw; ++x) {
            double fx = (x + 0.5) * sx - 0.5;
            int x0 = (int)floor(fx);
            double wx = fx - x0;
            int x0c = std::min(std::max(x0, 0), src.w - 1);
            int x1c = std::min(std::max(x0 + 1, 0), src.w - 1);
            for (int c = 0; c < 3; ++c) {
                double v = (1 - wy) * ((1 - wx) * r0[x0c * 3 + c] + wx * r0[x1c * 3 + c])
                         + wy * ((1 - wx) * r1[x0c * 3 + c] + wx * r1[x1c * 3 + c]);
                int iv = (int)(v + 0.5);
                d[x * 3 + c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
            }
        }
    }
    return dst;
}

Image img_add_border(const Image& src, int top, int bottom, int left, int right, uint8_t v) {
    if (top == 0 && bottom == 0 && left == 0 && right == 0) return src;
    Image dst(src.w + left + right, src.h + top + bottom);
    std::memset(dst.bgr.data(), v, dst.bgr.size());
    for (int y = 0; y < src.h; ++y)
        std::memcpy(dst.row(y + top) + (size_t)left * 3, src.row(y), (size_t)src.w * 3);
    return dst;
}

Image img_fill_border(const Image& src, int t, int b, int l, int r, uint8_t v) {
    return img_add_border(src, t, b, l, r, v);
}

Image img_rotate180(const Image& src) {
    Image dst(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* s = src.row(src.h - 1 - y);
        uint8_t* d = dst.row(y);
        for (int x = 0; x < src.w; ++x) {
            int sx = src.w - 1 - x;
            d[x * 3 + 0] = s[sx * 3 + 0];
            d[x * 3 + 1] = s[sx * 3 + 1];
            d[x * 3 + 2] = s[sx * 3 + 2];
        }
    }
    return dst;
}

// np.rot90: 逆时针 90 度(默认)，等价 cv2.rotate(img, ROTATE_90_COUNTERCLOCKWISE)
Image img_rot90(const Image& src) {
    Image dst(src.h, src.w);
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* s = src.row(y);
        for (int x = 0; x < src.w; ++x) {
            // dst(x', y') where x' = y, y' = src.w - 1 - x
            uint8_t* d = dst.row(src.w - 1 - x) + (size_t)y * 3;
            d[0] = s[x * 3 + 0]; d[1] = s[x * 3 + 1]; d[2] = s[x * 3 + 2];
        }
    }
    return dst;
}

// 顺时针 90 度
Image img_rot90_cw(const Image& src) {
    Image dst(src.h, src.w);
    for (int yd = 0; yd < dst.h; ++yd) {
        uint8_t* d = dst.row(yd);
        int xs = yd;
        for (int xd = 0; xd < dst.w; ++xd) {
            const uint8_t* s = src.row(src.h - 1 - xd) + (size_t)xs * 3;
            d[xd * 3 + 0] = s[0];
            d[xd * 3 + 1] = s[1];
            d[xd * 3 + 2] = s[2];
        }
    }
    return dst;
}

Image img_flip_h(const Image& src) {
    Image dst(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        const uint8_t* s = src.row(y);
        uint8_t* d = dst.row(y);
        for (int x = 0; x < src.w; ++x) {
            int sx = src.w - 1 - x;
            d[x * 3 + 0] = s[sx * 3 + 0];
            d[x * 3 + 1] = s[sx * 3 + 1];
            d[x * 3 + 2] = s[sx * 3 + 2];
        }
    }
    return dst;
}

Image img_flip_v(const Image& src) {
    Image dst(src.w, src.h);
    for (int y = 0; y < src.h; ++y)
        std::memcpy(dst.row(y), src.row(src.h - 1 - y), (size_t)src.w * 3);
    return dst;
}

// dst->src 单应矩阵（8x8 线性方程组，高斯消元）
static bool solve_homography(const double d[4][2], const double s[4][2], double H[9]) {
    double A[8][9];
    for (int i = 0; i < 4; ++i) {
        double x = d[i][0], y = d[i][1], u = s[i][0], v = s[i][1];
        double* r0 = A[i * 2];
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[3] = 0; r0[4] = 0; r0[5] = 0;
        r0[6] = -x * u; r0[7] = -y * u; r0[8] = u;
        double* r1 = A[i * 2 + 1];
        r1[0] = 0; r1[1] = 0; r1[2] = 0; r1[3] = x; r1[4] = y; r1[5] = 1;
        r1[6] = -x * v; r1[7] = -y * v; r1[8] = v;
    }
    for (int col = 0; col < 8; ++col) {
        int piv = col;
        for (int r = col + 1; r < 8; ++r)
            if (fabs(A[r][col]) > fabs(A[piv][col])) piv = r;
        if (fabs(A[piv][col]) < 1e-12) return false;
        if (piv != col) for (int k = col; k < 9; ++k) std::swap(A[piv][k], A[col][k]);
        double p = A[col][col];
        for (int k = col; k < 9; ++k) A[col][k] /= p;
        for (int r = 0; r < 8; ++r) {
            if (r == col) continue;
            double f = A[r][col];
            if (f == 0) continue;
            for (int k = col; k < 9; ++k) A[r][k] -= f * A[col][k];
        }
    }
    for (int i = 0; i < 8; ++i) H[i] = A[i][8];
    H[8] = 1.0;
    return true;
}

// OpenCV INTER_CUBIC 核（A = -0.75）
static inline double cubic_w(double x) {
    const double A = -0.75;
    x = fabs(x);
    if (x <= 1.0) return ((A + 2) * x - (A + 3)) * x * x + 1;
    if (x < 2.0) return (((x - 5) * x + 8) * x - 4) * A;
    return 0.0;
}

Image img_crop_perspective(const Image& src, const float pts[4][2]) {
    auto dist = [](float ax, float ay, float bx, float by) {
        return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
    };
    int cw = (int)std::max(dist(pts[0][0], pts[0][1], pts[1][0], pts[1][1]),
                           dist(pts[2][0], pts[2][1], pts[3][0], pts[3][1]));
    int ch = (int)std::max(dist(pts[0][0], pts[0][1], pts[3][0], pts[3][1]),
                           dist(pts[1][0], pts[1][1], pts[2][0], pts[2][1]));
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;

    double d[4][2] = {{0, 0}, {(double)cw, 0}, {(double)cw, (double)ch}, {0, (double)ch}};
    double s[4][2];
    for (int i = 0; i < 4; ++i) { s[i][0] = pts[i][0]; s[i][1] = pts[i][1]; }  // tl,tr,br,bl

    double H[9];
    Image dst(cw, ch);
    if (!solve_homography(d, s, H)) {
        // 退化：直接按包围盒裁剪
        return img_resize(src, cw, ch);
    }
    for (int y = 0; y < ch; ++y) {
        uint8_t* drow = dst.row(y);
        for (int x = 0; x < cw; ++x) {
            double den = H[6] * x + H[7] * y + H[8];
            double u = (H[0] * x + H[1] * y + H[2]) / den;
            double v = (H[3] * x + H[4] * y + H[5]) / den;
            int x0 = (int)floor(u), y0 = (int)floor(v);
            double fx = u - x0, fy = v - y0;
            double wx[4], wy[4];
            for (int k = 0; k < 4; ++k) { wx[k] = cubic_w(k - 1 - fx); wy[k] = cubic_w(k - 1 - fy); }
            for (int c = 0; c < 3; ++c) {
                double val = 0;
                for (int j = 0; j < 4; ++j) {
                    int yy = std::min(std::max(y0 - 1 + j, 0), src.h - 1);
                    const uint8_t* r = src.row(yy);
                    double srow = 0;
                    for (int i = 0; i < 4; ++i) {
                        int xx = std::min(std::max(x0 - 1 + i, 0), src.w - 1);
                        srow += wx[i] * r[xx * 3 + c];
                    }
                    val += wy[j] * srow;
                }
                int iv = (int)(val + 0.5);
                drow[x * 3 + c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
            }
        }
    }
    // 与 cv2 版一致：高宽比 >= 1.5 时逆时针旋转 90 度
    if (dst.h * 1.0 / dst.w >= 1.5) return img_rot90(dst);
    return dst;
}

// CoInitialize 辅助（DLL 首次调用时执行一次）
void ensure_com() {
    static bool done = false;
    if (done) return;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    done = true;
}
