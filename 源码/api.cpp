#include "common.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>

// 由 image.cpp 提供
void ensure_com();

// ================================================================ 小型 JSON 解析
namespace {

struct JVal {
    bool is_str = false;
    bool is_num = false;
    double num = 0;
    std::string str;
};

static void skip_ws(const char*& p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
}

static bool parse_string(const char*& p, std::string& out) {
    if (*p != '"') return false;
    ++p;
    out.clear();
    while (*p) {
        if (*p == '\\') {
            ++p;
            switch (*p) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'u': {
                    ++p;
                    char hex[5] = {0};
                    for (int i = 0; i < 4 && p[i]; ++i) hex[i] = p[i];
                    unsigned cp = fmt::parse_hex4(hex);
                    p += 3;
                    // UTF-8 编码（仅处理 BMP）
                    std::string u8;
                    if (cp < 0x80) u8 += (char)cp;
                    else if (cp < 0x800) {
                        u8 += (char)(0xC0 | (cp >> 6));
                        u8 += (char)(0x80 | (cp & 0x3F));
                    } else {
                        u8 += (char)(0xE0 | (cp >> 12));
                        u8 += (char)(0x80 | ((cp >> 6) & 0x3F));
                        u8 += (char)(0x80 | (cp & 0x3F));
                    }
                    out += u8;
                    break;
                }
                default: out += *p; break;
            }
            if (*p) ++p;
        } else if (*p == '"') {
            ++p;
            return true;
        } else {
            out += *p;
            ++p;
        }
    }
    return false;
}

// 解析扁平对象 {"k": v, ...}
static void parse_flat(const char* p, std::map<std::string, JVal>& out) {
    skip_ws(p);
    if (*p != '{') return;
    ++p;
    while (*p) {
        skip_ws(p);
        if (*p == '}') return;
        if (*p == ',') { ++p; continue; }
        std::string key;
        if (!parse_string(p, key)) return;
        skip_ws(p);
        if (*p != ':') return;
        ++p;
        skip_ws(p);
        JVal v;
        if (*p == '"') {
            v.is_str = true;
            parse_string(p, v.str);
            // 容错：AHK 的 cJson 会把浮点数序列化成字符串（{"det_thresh":"0.95"}），
            // 这里对"整串都是数字"的字符串同时打上数值标记，get_n / get_s 都能取到
            if (!v.str.empty()) {
                const char* e = nullptr;
                double d = fmt::parse(v.str.c_str(), &e);
                if (e && *e == '\0' && e != v.str.c_str()) {
                    v.num = d;
                    v.is_num = true;
                }
            }
        } else {
            const char* end = nullptr;
            const char* start = p;
            if (!strncmp(p, "true", 4)) { v.is_num = true; v.num = 1; p += 4; }
            else if (!strncmp(p, "false", 5)) { v.is_num = true; v.num = 0; p += 5; }
            else if (!strncmp(p, "null", 4)) { p += 4; }
            else {
                v.num = fmt::parse(start, &end);
                v.is_num = end != start;
                p = end;
            }
        }
        out[key] = v;
    }
}

static std::string get_s(std::map<std::string, JVal>& m, std::initializer_list<const char*> keys,
                         const std::string& def) {
    for (auto k : keys) {
        auto it = m.find(k);
        if (it != m.end() && it->second.is_str) return it->second.str;
    }
    return def;
}

static double get_n(std::map<std::string, JVal>& m, std::initializer_list<const char*> keys, double def) {
    for (auto k : keys) {
        auto it = m.find(k);
        if (it != m.end() && it->second.is_num) return it->second.num;
    }
    return def;
}

}  // namespace

extern "C" {

// 线程内的错误串（OCR_LastError 返回它）
static std::string t_last_err;

// 版本串 / 最近一次错误
__declspec(dllexport) const char* __cdecl OCR_Version() {
    return "RapidOCR.dll 1.8.1 (PP-OCRv6 / onnxruntime)";
}

__declspec(dllexport) const char* __cdecl OCR_LastError() {
    return t_last_err.c_str();
}

// 把 json 里出现的键覆盖到 cfg 上；没出现的键保留 cfg 现值（增量合并，供 OCR_SetConfig 用）
static void parse_cfg(const char* cfg_json, OcrConfig& cfg) {
    std::map<std::string, JVal> m;
    parse_flat(cfg_json, m);

    std::string size = get_s(m, {"model_size", "size"}, cfg.model_size);
    for (auto& c : size) c = (char)tolower(c);
    if (size != "tiny" && size != "medium") size = "small";
    cfg.model_size = size;

    // 只有 PP-OCRv6：det/rec 由 model_size 决定（tiny / small / medium），
    // v4 那一套（含韩文模型组）已移除；要韩文请用旧版封装。
    cfg.models_dir = get_s(m, {"models", "models_dir"}, cfg.models_dir);
    cfg.det = get_s(m, {"det"}, cfg.det);
    cfg.rec = get_s(m, {"rec"}, cfg.rec);
    cfg.cls = get_s(m, {"cls"}, cfg.cls);
    cfg.dict = get_s(m, {"dict", "keys"}, cfg.dict);

    cfg.num_threads = (int)get_n(m, {"num_threads", "numThread"}, cfg.num_threads);
    if (cfg.num_threads < 1) cfg.num_threads = 1;

    cfg.use_det = (int)get_n(m, {"use_det"}, cfg.use_det);
    cfg.use_rec = (int)get_n(m, {"use_rec"}, cfg.use_rec);
    cfg.use_cls = (int)get_n(m, {"use_cls", "do_angle"}, cfg.use_cls);

    cfg.det_limit_side_len = (int)get_n(m, {"det_limit_side_len", "limit_side_len"}, cfg.det_limit_side_len);
    cfg.det_limit_type = get_s(m, {"det_limit_type", "limit_type"}, cfg.det_limit_type);
    cfg.det_max_upscale = get_n(m, {"det_max_upscale"}, cfg.det_max_upscale);
    cfg.alpha_bg = get_s(m, {"alpha_bg", "alpha"}, cfg.alpha_bg);
    cfg.alpha_retry = (int)get_n(m, {"alpha_retry"}, cfg.alpha_retry);
    cfg.det_thresh = get_n(m, {"det_thresh", "thresh", "box_thresh"}, cfg.det_thresh);
    cfg.det_box_thresh = get_n(m, {"det_box_thresh", "box_score_thresh"}, cfg.det_box_thresh);
    cfg.det_unclip_ratio = get_n(m, {"det_unclip_ratio", "unclip_ratio"}, cfg.det_unclip_ratio);
    cfg.det_max_candidates = (int)get_n(m, {"det_max_candidates", "max_candidates"}, cfg.det_max_candidates);
    cfg.det_use_dilation = (int)get_n(m, {"det_use_dilation", "use_dilation"}, cfg.det_use_dilation);

    cfg.rec_img_h = (int)get_n(m, {"rec_img_h"}, cfg.rec_img_h);
    cfg.rec_img_w = (int)get_n(m, {"rec_img_w"}, cfg.rec_img_w);
    cfg.rec_batch_num = (int)get_n(m, {"rec_batch_num"}, cfg.rec_batch_num);
    if (cfg.rec_batch_num < 1) cfg.rec_batch_num = 1;

    cfg.cls_img_h = (int)get_n(m, {"cls_img_h"}, cfg.cls_img_h);
    cfg.cls_img_w = (int)get_n(m, {"cls_img_w"}, cfg.cls_img_w);
    cfg.cls_thresh = get_n(m, {"cls_thresh"}, cfg.cls_thresh);

    cfg.text_score = get_n(m, {"text_score"}, cfg.text_score);
    cfg.min_side_len = (int)get_n(m, {"min_side_len"}, cfg.min_side_len);
    cfg.max_side_len = (int)get_n(m, {"max_side_len"}, cfg.max_side_len);
    cfg.use_vertical_padding = (int)get_n(m, {"use_vertical_padding"}, cfg.use_vertical_padding);
    cfg.min_height = (int)get_n(m, {"min_height"}, cfg.min_height);
    cfg.width_height_ratio = get_n(m, {"width_height_ratio"}, cfg.width_height_ratio);
    cfg.padding = (int)get_n(m, {"padding"}, cfg.padding);
}

// 返回引擎句柄；失败返回 NULL，错误信息见 OCR_LastError()
__declspec(dllexport) void* __cdecl OCR_Create(const char* cfg_json) {
    t_last_err.clear();
    ensure_com();
    OcrConfig cfg;
    if (cfg_json && *cfg_json) parse_cfg(cfg_json, cfg);

    OcrEngine* eng = new OcrEngine();
    std::string err;
    if (!eng->init(cfg, err)) {
        t_last_err = err;
        delete eng;
        return nullptr;
    }
    return eng;
}

// 运行时改参数：json 里出现的键覆盖当前配置，没出现的保持不变。
// 只改到模型 / 线程 / 开关时才重建 session（0.1~0.5 秒），阈值、预处理之类立即生效（微秒级）。
// 返回 0 成功，非 0 失败（原因见 OCR_LastError）。
__declspec(dllexport) int __cdecl OCR_SetConfig(void* h, const char* cfg_json) {
    t_last_err.clear();
    if (!h) { t_last_err = "句柄为空"; return 1; }
    OcrEngine* e = (OcrEngine*)h;
    OcrConfig cfg = e->raw_cfg;
    if (cfg_json && *cfg_json) parse_cfg(cfg_json, cfg);
    std::string err;
    if (!e->set_config(cfg, err)) { t_last_err = err; return 1; }
    return 0;
}

__declspec(dllexport) void __cdecl OCR_Destroy(void* h) {
    if (!h) return;
    delete (OcrEngine*)h;
}

__declspec(dllexport) void __cdecl OCR_FreeString(char* s) {
    free(s);
}

static char* make_result(const OcrResult& res) {
    std::string o;
    o.reserve(1024 + res.items.size() * 128);
    // 手写数字格式化（fmt.h），不碰 printf 家族，DLL 少 ~19 KB 静态 libc
    static const char kBox[] = "{\"box\":[";
    static const char kScore[] = "],\"score\":";
    static const char kText[] = ",\"text\":\"";
    static const char kElapse[] = "],\"elapse\":{\"det\":";
    static const char kCls[] = ",\"cls\":";
    static const char kRec[] = ",\"rec\":";
    char buf[128];
    char* w;
    o += res.items.empty() ? "{\"code\":101,\"data\":[" : "{\"code\":100,\"data\":[";
    for (size_t i = 0; i < res.items.size(); ++i) {
        const OcrItem& it = res.items[i];
        if (i) o += ',';
        o += kBox;
        for (int k = 0; k < 4; ++k) {
            w = buf;
            *w++ = '[';
            w = fmt::int_dec(w, lround(it.box[k][0]));
            *w++ = ',';
            w = fmt::int_dec(w, lround(it.box[k][1]));
            *w++ = ']';
            *w = 0;
            o += buf;
            if (k < 3) o += ',';
        }
        w = buf;
        memcpy(w, kScore, sizeof(kScore) - 1);
        w += sizeof(kScore) - 1;
        w = fmt::fixed(w, it.score, 5);
        memcpy(w, kText, sizeof(kText) - 1);
        w += sizeof(kText) - 1;
        *w = 0;
        o += buf;
        o += json_escape(it.text);
        o += "\"}";
    }
    w = buf;
    memcpy(w, kElapse, sizeof(kElapse) - 1);
    w += sizeof(kElapse) - 1;
    w = fmt::fixed(w, res.det_ms, 2);
    memcpy(w, kCls, sizeof(kCls) - 1);
    w += sizeof(kCls) - 1;
    w = fmt::fixed(w, res.cls_ms, 2);
    memcpy(w, kRec, sizeof(kRec) - 1);
    w += sizeof(kRec) - 1;
    w = fmt::fixed(w, res.rec_ms, 2);
    *w++ = '}';
    *w = 0;
    o += buf;
    o += "}";
    char* p = (char*)malloc(o.size() + 1);
    if (!p) return nullptr;
    memcpy(p, o.c_str(), o.size() + 1);
    return p;
}

__declspec(dllexport) char* __cdecl OCR_RunFile(void* h, const wchar_t* path) {
    t_last_err.clear();
    if (!h) { t_last_err = "句柄为空"; return nullptr; }
    if (!path) { t_last_err = "路径为空"; return nullptr; }
    OcrResult res;
    std::string err;
    if (!((OcrEngine*)h)->run_file(path, res, err)) {
        t_last_err = err;
        return nullptr;
    }
    return make_result(res);
}

// 唯一的图像入口：32bpp BGRA（非预乘）像素，top-down，stride >= w*4
// 直接用 ImagePutBuffer(image) 得到的 ptr / width / height / stride
__declspec(dllexport) char* __cdecl OCR_RunBuffer(void* h, const void* bgra, int w, int hgt,
                                                  int stride) {
    t_last_err.clear();
    if (!h) { t_last_err = "句柄为空"; return nullptr; }
    if (!bgra) { t_last_err = "像素指针为空"; return nullptr; }
    OcrResult res;
    std::string err;
    if (!((OcrEngine*)h)->run_buffer(bgra, w, hgt, stride, res, err)) {
        t_last_err = err;
        return nullptr;
    }
    return make_result(res);
}

// 把最近一次识别的标注图（框 + 编号）画出来，以 HBITMAP 交出。
// 只在调用时绘制，结果缓存；没有识别过 / 最近一次识别失败时返回 NULL。
__declspec(dllexport) void* __cdecl OCR_VisBitmap(void* h) {
    t_last_err.clear();
    if (!h) return nullptr;
    OcrEngine* e = (OcrEngine*)h;

    if (e->vis_dirty) {
        if (e->last_src.w <= 0 || e->last_src.h <= 0) {
            t_last_err = "没有可画的图（最近一次识别没有成功）";
            return nullptr;
        }
        e->last_vis = vis_draw(e->last_src, e->last_items);
        e->last_src = Image();   // 画完就不留原图，省 3 字节/像素
        e->vis_dirty = false;
    }

    const Image& v = e->last_vis;
    if (v.w <= 0 || v.h <= 0) {
        t_last_err = "没有可画的图（最近一次识别没有成功）";
        return nullptr;
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = v.w;
    bi.bmiHeader.biHeight = -v.h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    DeleteDC(dc);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        t_last_err = "创建 HBITMAP 失败";
        return nullptr;
    }
    const int dst_stride = ((v.w * 3 + 3) & ~3);  // DIB 每行按 4 字节对齐
    for (int y = 0; y < v.h; ++y)
        memcpy((uint8_t*)bits + (size_t)y * dst_stride, v.row(y), (size_t)v.w * 3);
    return bmp;
}

}  // extern "C"
