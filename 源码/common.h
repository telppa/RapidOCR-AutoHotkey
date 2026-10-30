// RapidOCR.dll - PP-OCRv6 ONNX 推理 (供 AutoHotkey 等调用)
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "fmt.h"
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <cmath>
#include <chrono>

// ---------------------------------------------------------------- 图像
struct Image {
    int w = 0, h = 0;
    bool has_alpha = false;    // 解码时含非全透明像素（会记录原始 alpha，供重试时换背景）
    std::vector<uint8_t> bgr;  // H*W*3, BGR 交错
    Image() {}
    Image(int W, int H) : w(W), h(H), bgr((size_t)W * H * 3, 0) {}
    inline uint8_t* row(int y) { return bgr.data() + (size_t)y * w * 3; }
    inline const uint8_t* row(int y) const { return bgr.data() + (size_t)y * w * 3; }
};

// ---------------------------------------------------------------- 配置
struct OcrConfig {
    std::string model_size = "small";   // tiny / small / medium，决定默认的 det / rec 文件名
    std::string models_dir;             // 模型根目录（默认 exe/dll 同级 models）
    std::string det, rec, cls;           // 模型文件名或绝对路径，空=按 model_size 自动
    std::string dict;                    // 字典文件（默认从 rec 模型内嵌元数据读取）

    int    num_threads      = 4;

    int    use_det          = 1;
    int    use_cls          = 1;
    int    use_rec          = 1;

    int    det_limit_side_len = 736;
    double det_max_upscale    = 3.0;   // 短边放大上限（0=不限制，完全等同 rapidocr 原始行为）
    std::string alpha_bg      = "auto";  // auto=rapidocr 对比度自动背景 / drop=丢弃 alpha / white / black
    int    alpha_retry        = 1;       // auto 模式检测全空且图带透明时，按 drop 再试一次
    std::string det_limit_type = "min";   // min / max
    double det_thresh         = 0.3;      // 像素阈值
    double det_box_thresh     = 0.5;      // 框平均分阈值
    double det_unclip_ratio   = 1.6;
    int    det_max_candidates = 1000;
    int    det_use_dilation   = 1;

    int    rec_img_h        = 48;
    int    rec_img_w        = 320;
    int    rec_batch_num    = 6;

    int    cls_img_h        = 48;
    int    cls_img_w        = 192;
    double cls_thresh       = 0.9;

    double text_score       = 0.5;
    int    min_side_len     = 30;
    int    max_side_len     = 2000;
    int    use_vertical_padding = 1;
    int    min_height       = 30;
    double width_height_ratio = 8.0;

    int    padding          = 0;          // 兼容旧版：图像外周加白边像素数
};

struct OcrItem {
    float box[4][2];   // tl, tr, br, bl
    float score = 0.f;
    std::string text;  // UTF-8
};

struct OcrResult {
    std::vector<OcrItem> items;
    double det_ms = 0, cls_ms = 0, rec_ms = 0;
};

// ---------------------------------------------------------------- 图像操作
// WIC 解码（文件 / 内存）。成功返回 true。
bool decode_image_file(const wchar_t* path, Image& out, std::string& err, int alpha_mode = 0);
// 直接从 32bpp BGRA（非预乘）像素构造 Image，stride 允许 > w*4；alpha_mode 同上
bool image_from_bgra(const void* bits, int w, int h, int stride, Image& out, int alpha_mode = 0);
bool decode_image_memory(const void* data, int size, Image& out, std::string& err, int alpha_mode = 0);

Image img_resize(const Image& src, int nw, int nh);                  // 双线性
Image img_add_border(const Image& src, int top, int bottom, int left, int right, uint8_t v);
Image img_rotate180(const Image& src);
Image img_rot90(const Image& src);                                   // 逆时针 90（同 np.rot90）
Image img_rot90_cw(const Image& src);                                // 顺时针 90
Image img_flip_h(const Image& src);                                  // 左右翻转
Image img_flip_v(const Image& src);                                  // 上下翻转
Image img_crop_perspective(const Image& src, const float pts[4][2]);  // 四点透视裁剪
Image img_fill_border(const Image& src, int t, int b, int l, int r, uint8_t v);

// 把检测框 + 框编号画到图上（GDI）
Image vis_draw(const Image& src, const std::vector<OcrItem>& items);
// BGR 图 -> PNG 字节流（WIC 编码器）
bool encode_png(const Image& src, std::string& out);

// ---------------------------------------------------------------- 引擎
struct EngineImpl;  // 定义在 ocr.cpp

class OcrEngine {
public:
    OcrEngine() {}
    ~OcrEngine();

    bool init(const OcrConfig& cfg, std::string& err);

    // 运行时改参数：只重建真正影响 session 的部分（模型 / 线程 / 开关），
    // 阈值、预处理之类的改动直接生效，不重新加载模型。
    bool set_config(const OcrConfig& raw, std::string& err);

    // 用户原样传进来的配置（model_size 与空文件名都保留），set_config 以它为基准增量合并
    OcrConfig raw_cfg;
    bool run_file(const wchar_t* path, OcrResult& res, std::string& err);
    bool run_buffer(const void* bgra, int w, int h, int stride, OcrResult& res, std::string& err);
    bool run_image(const Image& img, OcrResult& res, std::string& err);

    const OcrConfig& config() const { return cfg_; }

    // 标注图：识别后只留"原图 + 框"（各一份），AHK 调 OCR_VisBitmap 时才画；
    // 画完就释放原图，画好的图留作缓存，重复取不再重画。
    Image last_src;
    std::vector<OcrItem> last_items;
    Image last_vis;
    bool  vis_dirty = false;

    std::string info;

private:
    EngineImpl* impl_ = nullptr;

    bool load_all(const OcrConfig& cfg, std::string& err);
    bool load_models(std::string& err);
    bool det_and_crop(const Image& img, std::vector<Image>& crops,
                      std::vector<std::array<float, 8>>& boxes, std::string& err);
    bool cls_and_rotate(std::vector<Image>& imgs, std::string& err);
    bool rec_batch(const std::vector<Image>& imgs, std::vector<std::pair<std::string, float>>& out,
                   std::string& err);

    OcrConfig cfg_;
    bool inited_ = false;
};

std::string path_dir(const std::wstring& p);      // 提取目录（UTF-8）
std::wstring utf8_to_wide(const std::string& s);
std::string  wide_to_utf8(const std::wstring& s);
std::string  json_escape(const std::string& s);
void         ensure_com();
