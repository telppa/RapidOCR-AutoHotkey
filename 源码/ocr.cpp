#include "common.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <map>
#include "onnxruntime_c_api.h"

// ==================================================================== ORT 封装
static const OrtApi* g_ort = nullptr;
static OrtEnv* g_env = nullptr;
static HMODULE g_self = nullptr;

static std::wstring self_dir() {
    wchar_t buf[MAX_PATH * 2] = {0};
    if (!g_self) {
        HMODULE h = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&self_dir, &h);
        g_self = h;
    }
    DWORD n = GetModuleFileNameW(g_self, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

static bool load_ort(std::string& err) {
    if (g_ort) return true;
    std::wstring dll = self_dir() + L"\\onnxruntime.dll";
    HMODULE h = LoadLibraryW(dll.c_str());
    if (!h) h = LoadLibraryW(L"onnxruntime.dll");
    if (!h) { err = "无法加载 onnxruntime.dll（应与 RapidOCR.dll 放在同一目录）"; return false; }
    auto getBase = (const OrtApiBase * (*)(void)) GetProcAddress(h, "OrtGetApiBase");
    if (!getBase) { err = "onnxruntime.dll 缺少 OrtGetApiBase"; return false; }
    g_ort = getBase()->GetApi(ORT_API_VERSION);
    if (!g_ort) { err = "onnxruntime.dll 版本与编译头不匹配"; return false; }
    OrtStatus* st = g_ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "rapidocr", &g_env);
    if (st) {
        err = g_ort->GetErrorMessage(st);
        g_ort->ReleaseStatus(st);
        g_env = nullptr;
        return false;
    }
    return true;
}

static bool ort_ok(OrtStatus* st, std::string& err) {
    if (!st) return true;
    err = g_ort->GetErrorMessage(st);
    g_ort->ReleaseStatus(st);
    return false;
}

namespace {
class OrtSessionWrap {
public:
    ~OrtSessionWrap() {
        if (sess && g_ort) g_ort->ReleaseSession(sess);
    }

    bool load(const std::wstring& path, int threads, std::string& err) {
        OrtSessionOptions* so = nullptr;
        if (!ort_ok(g_ort->CreateSessionOptions(&so), err)) return false;
        g_ort->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
        g_ort->SetSessionLogSeverityLevel(so, 3);
        if (threads > 0) {
            int ncpu = (int)std::thread::hardware_concurrency();
            int t = std::min(threads, ncpu > 0 ? ncpu : threads);
            g_ort->SetIntraOpNumThreads(so, t);
            g_ort->SetInterOpNumThreads(so, 1);
        }
        OrtStatus* st = g_ort->CreateSession(g_env, path.c_str(), so, &sess);
        g_ort->ReleaseSessionOptions(so);
        if (!ort_ok(st, err)) { sess = nullptr; err = "加载模型失败: " + err; return false; }

        OrtAllocator* alloc = nullptr;
        g_ort->GetAllocatorWithDefaultOptions(&alloc);

        size_t nin = 0, nout = 0;
        g_ort->SessionGetInputCount(sess, &nin);
        g_ort->SessionGetOutputCount(sess, &nout);
        if (nin < 1 || nout < 1) { err = "模型输入输出异常"; return false; }
        char* nm = nullptr;
        g_ort->SessionGetInputName(sess, 0, alloc, &nm);
        in_name = nm;
        g_ort->AllocatorFree(alloc, nm);
        g_ort->SessionGetOutputName(sess, 0, alloc, &nm);
        out_name = nm;
        g_ort->AllocatorFree(alloc, nm);
        return true;
    }

    // 读取模型内嵌自定义元数据（PP-OCRv6 的 character 字典）
    std::string metadata(const char* key) {
        std::string r;
        if (!sess) return r;
        OrtModelMetadata* meta = nullptr;
        OrtStatus* st = g_ort->SessionGetModelMetadata(sess, &meta);
        if (st) { g_ort->ReleaseStatus(st); return r; }
        if (meta) {
            OrtAllocator* alloc = nullptr;
            g_ort->GetAllocatorWithDefaultOptions(&alloc);
            char* val = nullptr;
            st = g_ort->ModelMetadataLookupCustomMetadataMap(meta, alloc, key, &val);
            if (!st && val) {
                r = val;
                g_ort->AllocatorFree(alloc, val);
            } else if (st) {
                g_ort->ReleaseStatus(st);
            }
            g_ort->ReleaseModelMetadata(meta);
        }
        return r;
    }

    bool run(const float* data, const std::vector<int64_t>& shape, std::vector<float>& out,
             std::vector<int64_t>& outshape, std::string& err) {
        if (mem_info == nullptr) {
            if (!ort_ok(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info), err))
                return false;
        }
        size_t count = 1;
        for (auto d : shape) count *= (size_t)d;
        OrtValue* in = nullptr;
        OrtStatus* st = g_ort->CreateTensorWithDataAsOrtValue(
            mem_info, (void*)data, count * sizeof(float), shape.data(), shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in);
        if (!ort_ok(st, err)) return false;

        const char* in_names[1] = {in_name.c_str()};
        const char* out_names[1] = {out_name.c_str()};
        OrtValue* ov = nullptr;
        st = g_ort->Run(sess, nullptr, in_names, (const OrtValue* const*)&in, 1, out_names, 1, &ov);
        g_ort->ReleaseValue(in);
        if (!ort_ok(st, err)) { err = "推理失败: " + err; return false; }

        OrtTensorTypeAndShapeInfo* info = nullptr;
        g_ort->GetTensorTypeAndShape(ov, &info);
        size_t nd = 0;
        g_ort->GetDimensionsCount(info, &nd);
        outshape.assign(nd, 0);
        g_ort->GetDimensions(info, outshape.data(), nd);
        size_t total = 0;
        g_ort->GetTensorShapeElementCount(info, &total);
        g_ort->ReleaseTensorTypeAndShapeInfo(info);

        float* p = nullptr;
        st = g_ort->GetTensorMutableData(ov, (void**)&p);
        if (!ort_ok(st, err)) { g_ort->ReleaseValue(ov); return false; }
        out.assign(p, p + total);
        g_ort->ReleaseValue(ov);
        return true;
    }

    bool valid() const { return sess != nullptr; }

    void fail() {
        if (sess && g_ort) g_ort->ReleaseSession(sess);
        sess = nullptr;
    }

private:
    OrtSession* sess = nullptr;
    OrtMemoryInfo* mem_info = nullptr;
    std::string in_name, out_name;
};
}  // namespace

// ==================================================================== 几何工具
namespace {

struct RotRect {
    double w = 0, h = 0;
    double pts[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};  // 顺序成环
};

// 凸包（单调链），输入为像素点
std::vector<std::pair<double, double>> convex_hull(std::vector<std::pair<double, double>> p) {
    size_t n = p.size();
    if (n < 3) return p;
    std::sort(p.begin(), p.end());
    std::vector<std::pair<double, double>> h(2 * n);
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        while (k >= 2 && ((h[k - 1].first - h[k - 2].first) * (p[i].second - h[k - 2].second) -
                          (h[k - 1].second - h[k - 2].second) * (p[i].first - h[k - 2].first)) <= 0)
            --k;
        h[k++] = p[i];
    }
    for (size_t i = n - 1, t = k + 1; i > 0; --i) {
        while (k >= t && ((h[k - 1].first - h[k - 2].first) * (p[i - 1].second - h[k - 2].second) -
                          (h[k - 1].second - h[k - 2].second) * (p[i - 1].first - h[k - 2].first)) <= 0)
            --k;
        h[k++] = p[i - 1];
    }
    h.resize(k > 1 ? k - 1 : k);
    return h;
}

// 最小外接矩形（旋转卡壳）
RotRect min_area_rect(const std::vector<std::pair<double, double>>& pts_in) {
    RotRect rr;
    auto hull = convex_hull(pts_in);
    if (hull.size() < 3) return rr;

    double best_area = 1e300, best_u = 0, best_v = 0, best_w = 0, best_h = 0, best_ou = 0, best_ov = 0;
    for (size_t i = 0; i < hull.size(); ++i) {
        auto a = hull[i], b = hull[(i + 1) % hull.size()];
        double ex = b.first - a.first, ey = b.second - a.second;
        double len = std::sqrt(ex * ex + ey * ey);
        if (len < 1e-9) continue;
        double ux = ex / len, uy = ey / len;
        double vx = -uy, vy = ux;
        double minu = 1e300, maxu = -1e300, minv = 1e300, maxv = -1e300;
        for (auto& q : hull) {
            double u = q.first * ux + q.second * uy;
            double v = q.first * vx + q.second * vy;
            minu = std::min(minu, u); maxu = std::max(maxu, u);
            minv = std::min(minv, v); maxv = std::max(maxv, v);
        }
        double area = (maxu - minu) * (maxv - minv);
        if (area < best_area) {
            best_area = area;
            best_u = ux; best_v = uy;
            best_w = maxu - minu; best_h = maxv - minv;
            best_ou = minu; best_ov = minv;
        }
    }
    if (best_area > 1e299) return rr;
    double ux = best_u, uy = best_v, vx = -uy, vy = ux;
    auto P = [&](double u, double v) {
        return std::pair<double, double>{u * ux + v * vx, u * uy + v * vy};
    };
    auto p0 = P(best_ou, best_ov);
    auto p1 = P(best_ou + best_w, best_ov);
    auto p2 = P(best_ou + best_w, best_ov + best_h);
    auto p3 = P(best_ou, best_ov + best_h);
    double xs[4] = {p0.first, p1.first, p2.first, p3.first};
    double ys[4] = {p0.second, p1.second, p2.second, p3.second};
    for (int i = 0; i < 4; ++i) { rr.pts[i][0] = xs[i]; rr.pts[i][1] = ys[i]; }
    rr.w = best_w; rr.h = best_h;
    return rr;
}

// 多边形填充掩码（奇偶规则，像素中心位于整数坐标）
void fill_poly(std::vector<uint8_t>& mask, int mw, int mh, const double pts[][2], int n,
               int ox, int oy) {
    for (int y = 0; y < mh; ++y) {
        double sy = y + oy;
        std::vector<double> xs;
        for (int i = 0; i < n; ++i) {
            double y0 = pts[i][1], y1 = pts[(i + 1) % n][1];
            double x0 = pts[i][0], x1 = pts[(i + 1) % n][0];
            if ((sy >= y0 && sy < y1) || (sy >= y1 && sy < y0)) {
                double t = (sy - y0) / (y1 - y0);
                xs.push_back(x0 + t * (x1 - x0));
            }
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int xa = (int)ceil(xs[k] - 0.5);
            int xb = (int)floor(xs[k + 1] - 0.5);
            if (xb < 0) continue;
            if (xa < 0) xa = 0;
            if (xb >= mw) xb = mw - 1;
            uint8_t* row = mask.data() + (size_t)y * mw;
            for (int x = xa; x <= xb; ++x) row[x] = 1;
        }
    }
}

float box_score_fast(const float* map, int W, int H, const double pts[4][2]) {
    double xmin_d = pts[0][0], xmax_d = pts[0][0], ymin_d = pts[0][1], ymax_d = pts[0][1];
    for (int i = 1; i < 4; ++i) {
        xmin_d = std::min(xmin_d, pts[i][0]); xmax_d = std::max(xmax_d, pts[i][0]);
        ymin_d = std::min(ymin_d, pts[i][1]); ymax_d = std::max(ymax_d, pts[i][1]);
    }
    int xmin = std::min(std::max((int)floor(xmin_d), 0), W - 1);
    int xmax = std::min(std::max((int)ceil(xmax_d), 0), W - 1);
    int ymin = std::min(std::max((int)floor(ymin_d), 0), H - 1);
    int ymax = std::min(std::max((int)ceil(ymax_d), 0), H - 1);
    int mw = xmax - xmin + 1, mh = ymax - ymin + 1;
    if (mw <= 0 || mh <= 0) return 0.f;
    std::vector<uint8_t> mask((size_t)mw * mh, 0);
    double local[4][2];
    for (int i = 0; i < 4; ++i) { local[i][0] = pts[i][0] - xmin; local[i][1] = pts[i][1] - ymin; }
    fill_poly(mask, mw, mh, local, 4, 0, 0);
    double sum = 0;
    long cnt = 0;
    for (int y = 0; y < mh; ++y) {
        const uint8_t* mrow = mask.data() + (size_t)y * mw;
        const float* prow = map + (size_t)(y + ymin) * W + xmin;
        for (int x = 0; x < mw; ++x) {
            if (mrow[x]) { sum += prow[x]; ++cnt; }
        }
    }
    return cnt ? (float)(sum / cnt) : 0.f;
}

// 排序：按行（y 差 >= 10 视为新行）再按 x
void sort_boxes(std::vector<std::array<float, 8>>& boxes) {
    if (boxes.empty()) return;
    std::stable_sort(boxes.begin(), boxes.end(),
                     [](const std::array<float, 8>& a, const std::array<float, 8>& b) {
                         return a[1] < b[1];
                     });
    std::vector<int> line(boxes.size(), 0);
    for (size_t i = 1; i < boxes.size(); ++i)
        line[i] = line[i - 1] + ((boxes[i][1] - boxes[i - 1][1]) >= 10.0f ? 1 : 0);
    std::vector<size_t> idx(boxes.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        if (line[a] != line[b]) return line[a] < line[b];
        return boxes[a][0] < boxes[b][0];
    });
    std::vector<std::array<float, 8>> out;
    out.reserve(boxes.size());
    for (size_t i : idx) out.push_back(boxes[i]);
    boxes.swap(out);
}

// 归一化四点顺序为 tl,tr,br,bl
void order_points_clockwise(const double in[4][2], float out[4][2]) {
    int idx[4] = {0, 1, 2, 3};
    std::sort(idx, idx + 4, [&](int a, int b) { return in[a][0] < in[b][0]; });
    int left[2] = {idx[0], idx[1]};
    int right[2] = {idx[2], idx[3]};
    if (in[left[0]][1] > in[left[1]][1]) std::swap(left[0], left[1]);
    if (in[right[0]][1] > in[right[1]][1]) std::swap(right[0], right[1]);
    int ord[4] = {left[0], right[0], right[1], left[1]};
    for (int i = 0; i < 4; ++i) {
        out[i][0] = (float)in[ord[i]][0];
        out[i][1] = (float)in[ord[i]][1];
    }
}

}  // namespace

// ==================================================================== DB 后处理
static void db_postprocess(const float* map, int H, int W, int destW, int destH,
                           const OcrConfig& cfg,
                           std::vector<std::array<float, 8>>& boxes,
                           std::vector<float>& scores) {
    std::vector<uint8_t> seg((size_t)W * H, 0);
    for (size_t i = 0; i < seg.size(); ++i) seg[i] = (map[i] > (float)cfg.det_thresh) ? 1 : 0;

    std::vector<uint8_t> mask = seg;
    if (cfg.det_use_dilation) {
        // cv2.dilate，2x2 核，anchor(1,1)：dst(y,x) = max(src(y-1..y, x-1..x))
        mask.assign((size_t)W * H, 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint8_t v = 0;
                for (int dy = -1; dy <= 0 && !v; ++dy)
                    for (int dx = -1; dx <= 0 && !v; ++dx) {
                        int yy = y + dy, xx = x + dx;
                        if (yy >= 0 && yy < H && xx >= 0 && xx < W && seg[(size_t)yy * W + xx]) v = 1;
                    }
                mask[(size_t)y * W + x] = v;
            }
        }
    }

    // 8 连通域
    std::vector<int32_t> label((size_t)W * H, -1);
    std::vector<std::vector<int32_t>> comps;
    std::vector<int32_t> stack;
    int cur = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t p = (size_t)y * W + x;
            if (!mask[p] || label[p] >= 0) continue;
            if (cur >= cfg.det_max_candidates) { y = H; break; }
            comps.push_back({});
            stack.clear();
            stack.push_back((int32_t)p);
            label[p] = cur;
            while (!stack.empty()) {
                int32_t q = stack.back();
                stack.pop_back();
                comps[cur].push_back(q);
                int qy = q / W, qx = q % W;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        int yy = qy + dy, xx = qx + dx;
                        if (yy < 0 || yy >= H || xx < 0 || xx >= W) continue;
                        size_t np = (size_t)yy * W + xx;
                        if (mask[np] && label[np] < 0) { label[np] = cur; stack.push_back((int32_t)np); }
                    }
            }
            ++cur;
        }
    }

    boxes.clear();
    scores.clear();
    for (auto& comp : comps) {
        if (comp.size() < 4) continue;
        std::vector<std::pair<double, double>> pts;
        pts.reserve(comp.size());
        for (int32_t p : comp) pts.push_back({(double)(p % W), (double)(p / W)});
        RotRect rr = min_area_rect(pts);
        double sside = std::min(rr.w, rr.h);
        if (sside < 3) continue;

        float score = box_score_fast(map, W, H, rr.pts);
        if (score < cfg.det_box_thresh) continue;

        // unclip：矩形按 miter 外扩，等价 pyclipper 对矩形 offset
        double perim = 2.0 * (rr.w + rr.h);
        if (perim <= 1e-9) continue;
        double dist = (rr.w * rr.h) * cfg.det_unclip_ratio / perim;
        double cw = rr.w + 2 * dist, chh = rr.h + 2 * dist;
        if (std::min(cw, chh) < 5) continue;

        double cx = 0, cy = 0;
        for (int i = 0; i < 4; ++i) { cx += rr.pts[i][0]; cy += rr.pts[i][1]; }
        cx /= 4; cy /= 4;
        double e0x = rr.pts[1][0] - rr.pts[0][0], e0y = rr.pts[1][1] - rr.pts[0][1];
        double e1x = rr.pts[3][0] - rr.pts[0][0], e1y = rr.pts[3][1] - rr.pts[0][1];
        double l0 = std::sqrt(e0x * e0x + e0y * e0y), l1 = std::sqrt(e1x * e1x + e1y * e1y);
        if (l0 < 1e-9 || l1 < 1e-9) continue;
        double ux = e0x / l0 * cw / 2, uy = e0y / l0 * cw / 2;
        double vx = e1x / l1 * chh / 2, vy = e1y / l1 * chh / 2;
        double ep[4][2] = {{cx - ux - vx, cy - uy - vy},
                           {cx + ux - vx, cy + uy - vy},
                           {cx + ux + vx, cy + uy + vy},
                           {cx - ux + vx, cy - uy + vy}};

        // 映射回原图坐标
        double mapped[4][2];
        for (int i = 0; i < 4; ++i) {
            double mx = std::nearbyint(ep[i][0] / W * destW);
            double my = std::nearbyint(ep[i][1] / H * destH);
            mapped[i][0] = std::min(std::max(mx, 0.0), (double)destW);
            mapped[i][1] = std::min(std::max(my, 0.0), (double)destH);
        }
        float ob[4][2];
        order_points_clockwise(mapped, ob);
        for (int i = 0; i < 4; ++i) {
            ob[i][0] = std::min(std::max(ob[i][0], 0.0f), (float)(destW - 1));
            ob[i][1] = std::min(std::max(ob[i][1], 0.0f), (float)(destH - 1));
        }
        double rw = std::sqrt(std::pow(ob[0][0] - ob[1][0], 2) + std::pow(ob[0][1] - ob[1][1], 2));
        double rh = std::sqrt(std::pow(ob[0][0] - ob[3][0], 2) + std::pow(ob[0][1] - ob[3][1], 2));
        if (rw <= 3 || rh <= 3) continue;

        std::array<float, 8> b;
        for (int i = 0; i < 4; ++i) { b[i * 2] = ob[i][0]; b[i * 2 + 1] = ob[i][1]; }
        boxes.push_back(b);
        scores.push_back(score);
    }
    sort_boxes(boxes);
}

// ==================================================================== 引擎
static std::wstring resolve_model(const OcrConfig& cfg, const std::string& name) {
    std::string p = name;
    bool abs = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (abs) return utf8_to_wide(p);
    std::string dir = cfg.models_dir;
    if (dir.empty()) dir = wide_to_utf8(self_dir()) + "\\models";
    return utf8_to_wide(dir + "\\" + p);
}

struct EngineImpl {
    OrtSessionWrap det, cls, rec;
    std::vector<std::string> charset;  // 已插入 blank/space
};

OcrEngine::~OcrEngine() { delete impl_; }

// 把"用户写的配置"补成"实际用的配置"：空文件名按 model_size 展开
static OcrConfig effective_cfg(const OcrConfig& raw) {
    OcrConfig c = raw;
    if (c.det.empty()) c.det = "PP-OCRv6_det_" + c.model_size + ".onnx";
    if (c.rec.empty()) c.rec = "PP-OCRv6_rec_" + c.model_size + ".onnx";
    if (c.cls.empty()) c.cls = "ch_ppocr_mobile_v2.0_cls_infer.onnx";
    if (!c.use_cls) c.cls.clear();
    return c;
}

// 只有这几项会改变 session 本身，其余参数（阈值 / 预处理 / 批大小）改了就生效
static bool same_sessions(const OcrConfig& a, const OcrConfig& b) {
    return a.det == b.det && a.rec == b.rec && a.cls == b.cls &&
           a.models_dir == b.models_dir && a.dict == b.dict &&
           a.num_threads == b.num_threads &&
           a.use_det == b.use_det && a.use_rec == b.use_rec && a.use_cls == b.use_cls;
}

bool OcrEngine::init(const OcrConfig& cfg, std::string& err) {
    if (!load_all(effective_cfg(cfg), err)) return false;
    raw_cfg = cfg;
    return true;
}

bool OcrEngine::set_config(const OcrConfig& raw, std::string& err) {
    OcrConfig eff = effective_cfg(raw);
    // 只改了阈值之类：直接替换，一次模型加载都不做（微秒级）
    if (inited_ && same_sessions(eff, cfg_)) {
        cfg_    = eff;
        raw_cfg = raw;
        return true;
    }
    // 改到模型 / 线程 / 开关：先拆旧的再建新的，避免峰值内存翻倍
    OcrConfig old = raw_cfg;
    if (load_all(eff, err)) {
        raw_cfg = raw;
        return true;
    }
    // 新配置加载失败（路径写错等）：尽量把旧配置装回去，别让对象直接废掉
    std::string e2;
    if (load_all(effective_cfg(old), e2)) err += "（已恢复原配置）";
    else                                  err += "（原配置也无法恢复：" + e2 + "）";
    return false;
}

bool OcrEngine::load_all(const OcrConfig& cfg, std::string& err) {
    delete impl_;
    impl_    = nullptr;
    inited_  = false;
    cfg_     = cfg;
    if (!load_ort(err)) return false;
    impl_ = new EngineImpl();

    if (cfg_.use_det) {
        std::wstring p = resolve_model(cfg_, cfg_.det);
        if (!impl_->det.load(p, cfg_.num_threads, err)) { err = "det 模型: " + err; return false; }
    }
    if (cfg_.use_cls && !cfg_.cls.empty()) {
        std::wstring p = resolve_model(cfg_, cfg_.cls);
        std::string e2;
        bool ok = impl_->cls.load(p, cfg_.num_threads, e2);
        if (!ok) {
            // 方向分类模型加载失败不致命，跳过 180 度纠正
            impl_->cls.fail();
        }
    }
    if (cfg_.use_rec) {
        std::wstring p = resolve_model(cfg_, cfg_.rec);
        if (!impl_->rec.load(p, cfg_.num_threads, err)) { err = "rec 模型: " + err; return false; }

        std::string dict = impl_->rec.metadata("character");
        std::vector<std::string> raw;
        if (!dict.empty()) {
            size_t s = 0;
            while (s <= dict.size()) {
                size_t e = dict.find('\n', s);
                if (e == std::string::npos) e = dict.size();
                std::string line = dict.substr(s, e - s);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                raw.push_back(line);
                if (e == dict.size()) break;
                s = e + 1;
            }
        } else if (!cfg_.dict.empty()) {
            std::string fp = wide_to_utf8(resolve_model(cfg_, cfg_.dict));
            FILE* f = fopen(fp.c_str(), "rb");
            if (!f) { err = "无法读取字典文件: " + cfg_.dict; return false; }
            std::string line;
            int c;
            while ((c = fgetc(f)) != EOF) {
                if (c == '\n') {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    raw.push_back(line);
                    line.clear();
                } else line.push_back((char)c);
            }
            if (!line.empty()) raw.push_back(line);
            fclose(f);
        } else {
            err = "rec 模型缺少内嵌字典，且未指定 dict 文件";
            return false;
        }
        impl_->charset.clear();
        impl_->charset.push_back("blank");
        for (auto& s : raw) impl_->charset.push_back(s);
        impl_->charset.push_back(" ");
    }
    inited_ = true;
    info = "RapidOCR-dll 1.8.1 (PP-OCRv6 / onnxruntime 1.31)";
    return true;
}

bool OcrEngine::det_and_crop(const Image& img, std::vector<Image>& crops,
                             std::vector<std::array<float, 8>>& boxes, std::string& err) {
    if (!impl_->det.valid()) { err = "det 模型未加载"; return false; }
    const OcrConfig& c = cfg_;

    // ---- 预处理：resize 到 32 的倍数
    int h = img.h, w = img.w;
    double ratio = 1.0;
    if (c.det_limit_type == "max") {
        if (std::max(h, w) > c.det_limit_side_len)
            ratio = (double)c.det_limit_side_len / std::max(h, w);
    } else {
        if (std::min(h, w) < c.det_limit_side_len) {
            ratio = (double)c.det_limit_side_len / std::min(h, w);
            // 小图放大过头反而检测不出（笔画被插值糊掉），限制放大倍率
            if (c.det_max_upscale > 0 && ratio > c.det_max_upscale) ratio = c.det_max_upscale;
        }
    }
    int rh = (int)(h * ratio), rw = (int)(w * ratio);
    rh = (int)(std::nearbyint((double)rh / 32) * 32);
    rw = (int)(std::nearbyint((double)rw / 32) * 32);
    if (rh <= 0 || rw <= 0) { err = "det 预处理尺寸非法"; return false; }
    if (rh > 4096) rh = 4096;
    if (rw > 4096) rw = 4096;

    Image resized = (rh == h && rw == w) ? img : img_resize(img, rw, rh);

    std::vector<float> input((size_t)3 * rh * rw);
    const double mean = 0.5, std = 0.5, scale = 1.0 / 255.0;
    for (int y = 0; y < rh; ++y) {
        const uint8_t* s = resized.row(y);
        for (int x = 0; x < rw; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                double v = (s[x * 3 + ch] * scale - mean) / std;
                input[(size_t)ch * rh * rw + (size_t)y * rw + x] = (float)v;
            }
        }
    }

    std::vector<float> out;
    std::vector<int64_t> shape;
    std::vector<int64_t> in_shape = {1, 3, rh, rw};
    if (!impl_->det.run(input.data(), in_shape, out, shape, err)) return false;
    if (shape.size() != 4) { err = "det 输出维度异常"; return false; }
    int oh = (int)shape[2], ow = (int)shape[3];

    boxes.clear();
    std::vector<float> scores;
    db_postprocess(out.data(), oh, ow, w, h, c, boxes, scores);

    crops.clear();
    for (auto& b : boxes) {
        float pts[4][2] = {{b[0], b[1]}, {b[2], b[3]}, {b[4], b[5]}, {b[6], b[7]}};
        crops.push_back(img_crop_perspective(img, pts));
    }
    return true;
}

bool OcrEngine::cls_and_rotate(std::vector<Image>& imgs, std::string& err) {
    if (!impl_->cls.valid() || imgs.empty()) return true;
    const int H = cfg_.cls_img_h, W = cfg_.cls_img_w;
    const int n = (int)imgs.size();
    std::vector<float> input((size_t)n * 3 * H * W, 0.f);
    for (int i = 0; i < n; ++i) {
        const Image& im = imgs[i];
        if (im.h <= 0 || im.w <= 0) continue;
        double ratio = (double)im.w / im.h;
        int rw = (int)ceil(H * ratio);
        if (rw > W) rw = W;
        if (rw < 1) rw = 1;
        Image rs = img_resize(im, rw, H);
        for (int y = 0; y < H; ++y) {
            const uint8_t* s = rs.row(y);
            for (int x = 0; x < rw; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    double v = ((s[x * 3 + ch] / 255.0) - 0.5) / 0.5;
                    input[((size_t)i * 3 + ch) * H * W + (size_t)y * W + x] = (float)v;
                }
        }
    }
    std::vector<float> out;
    std::vector<int64_t> shape;
    std::vector<int64_t> in_shape = {n, 3, H, W};
    if (!impl_->cls.run(input.data(), in_shape, out, shape, err)) return false;
    if (out.size() < (size_t)n * 2) return true;
    for (int i = 0; i < n; ++i) {
        float p0 = out[(size_t)i * 2], p1 = out[(size_t)i * 2 + 1];
        if (p1 > p0 && p1 > (float)cfg_.cls_thresh) imgs[i] = img_rotate180(imgs[i]);
    }
    return true;
}

bool OcrEngine::rec_batch(const std::vector<Image>& imgs,
                          std::vector<std::pair<std::string, float>>& outres, std::string& err) {
    outres.assign(imgs.size(), {"", 0.f});
    if (!impl_->rec.valid()) return true;
    const int n = (int)imgs.size();
    const int H = cfg_.rec_img_h;
    const int base_w = cfg_.rec_img_w;
    const int batch = std::max(1, cfg_.rec_batch_num);

    for (int beg = 0; beg < n; beg += batch) {
        int end = std::min(n, beg + batch);
        double max_ratio = (double)base_w / H;
        std::vector<double> ratios(imgs.size(), 1.0);
        for (int i = beg; i < end; ++i) {
            double r = imgs[i].w / (double)imgs[i].h;
            ratios[i] = r;
            max_ratio = std::max(max_ratio, r);
        }
        int W = (int)(H * max_ratio);
        if (W < base_w) W = base_w;
        if (W > 4096) W = 4096;

        int cnt = end - beg;
        std::vector<float> input((size_t)cnt * 3 * H * W, 0.f);
        for (int i = beg; i < end; ++i) {
            const Image& im = imgs[i];
            int rw = (int)ceil(H * ratios[i]);
            if (rw > W) rw = W;
            if (rw < 1) rw = 1;
            Image rs = img_resize(im, rw, H);
            int k = i - beg;
            for (int y = 0; y < H; ++y) {
                const uint8_t* s = rs.row(y);
                for (int x = 0; x < rw; ++x)
                    for (int ch = 0; ch < 3; ++ch) {
                        double v = ((s[x * 3 + ch] / 255.0) - 0.5) / 0.5;
                        input[((size_t)k * 3 + ch) * H * W + (size_t)y * W + x] = (float)v;
                    }
            }
        }
        std::vector<float> out;
        std::vector<int64_t> shape;
        std::vector<int64_t> in_shape = {cnt, 3, H, W};
        if (!impl_->rec.run(input.data(), in_shape, out, shape, err)) return false;
        if (shape.size() != 3) { err = "rec 输出维度异常"; return false; }
        int T = (int)shape[1], C = (int)shape[2];
        int csz = (int)impl_->charset.size();
        for (int k = 0; k < cnt; ++k) {
            const float* p = out.data() + (size_t)k * T * C;
            std::string text;
            double sum = 0;
            int cntc = 0;
            int prev = -1;
            for (int t = 0; t < T; ++t) {
                const float* row = p + (size_t)t * C;
                int best = 0;
                float bp = row[0];
                for (int c = 1; c < C; ++c)
                    if (row[c] > bp) { bp = row[c]; best = c; }
                if (best != 0 && best != prev) {
                    if (best < csz) text += impl_->charset[best];
                    sum += bp;
                    ++cntc;
                }
                prev = best;
            }
            outres[beg + k] = {text, cntc ? (float)(sum / cntc) : 0.f};
        }
    }
    return true;
}

bool OcrEngine::run_image(const Image& img_in, OcrResult& res, std::string& err) {
    if (!inited_) { err = "引擎未初始化"; return false; }
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    res.items.clear();
    res.det_ms = res.cls_ms = res.rec_ms = 0;
    last_src = Image();
    last_items.clear();
    last_vis = Image();
    vis_dirty = false;

    // 旧版兼容：外周加白边
    Image img = img_in;
    int pad = cfg_.padding > 0 ? cfg_.padding : 0;
    if (pad > 0) img = img_add_border(img, pad, pad, pad, pad, 255);

    // ---- 全局预处理：缩放到 [min_side_len, max_side_len]
    Image work = img;
    double prh = 1.0, prw = 1.0;
    {
        int h = work.h, w = work.w;
        double r = 1.0;
        if (std::max(h, w) > cfg_.max_side_len) {
            r = (double)cfg_.max_side_len / std::max(h, w);
            int nh = (int)(h * r), nw = (int)(w * r);
            nh = (int)(std::nearbyint((double)nh / 32) * 32);
            nw = (int)(std::nearbyint((double)nw / 32) * 32);
            if (nh < 32) nh = 32;
            if (nw < 32) nw = 32;
            work = img_resize(work, nw, nh);
            prh = (double)h / nh;
            prw = (double)w / nw;
        }
        h = work.h; w = work.w;
        if (std::min(h, w) < cfg_.min_side_len) {
            double r2 = (double)cfg_.min_side_len / std::min(h, w);
            int nh = (int)(h * r2), nw = (int)(w * r2);
            nh = (int)(std::nearbyint((double)nh / 32) * 32);
            nw = (int)(std::nearbyint((double)nw / 32) * 32);
            if (nh < 32) nh = 32;
            if (nw < 32) nw = 32;
            work = img_resize(work, nw, nh);
            prh = (double)h / nh;
            prw = (double)w / nw;
        }
    }

    // ---- 纵向 padding（窄高/极扁图）
    int pad_top = 0;
    if (cfg_.use_vertical_padding) {
        int h = work.h, w = work.w;
        bool limit = (cfg_.width_height_ratio > 0) ? ((double)w / h > cfg_.width_height_ratio) : false;
        if (h <= cfg_.min_height || limit) {
            double new_h = std::max((double)(int)(w / cfg_.width_height_ratio), (double)cfg_.min_height) * 2;
            pad_top = (int)(fabs(new_h - h) / 2);
            if (pad_top > 0) work = img_add_border(work, pad_top, pad_top, 0, 0, 0);
        }
    }

    std::vector<Image> crops;
    std::vector<std::array<float, 8>> boxes;

    if (cfg_.use_det) {
        auto t0 = now();
        if (!det_and_crop(work, crops, boxes, err)) return false;
        res.det_ms = ms(t0, now());
        if (boxes.empty()) return true;
    } else {
        crops.push_back(work);
        boxes.push_back({0, 0, (float)work.w - 1, 0, (float)work.w - 1, (float)work.h - 1, 0,
                         (float)work.h - 1});
    }

    if (cfg_.use_cls) {
        auto t0 = now();
        if (!cls_and_rotate(crops, err)) return false;
        res.cls_ms = ms(t0, now());
    }

    std::vector<std::pair<std::string, float>> recs;
    if (cfg_.use_rec) {
        auto t0 = now();
        if (!rec_batch(crops, recs, err)) return false;
        res.rec_ms = ms(t0, now());
    }

    for (size_t i = 0; i < boxes.size(); ++i) {
        std::string text;
        float score = 0.f;
        if (cfg_.use_rec && i < recs.size()) { text = recs[i].first; score = recs[i].second; }
        // 过滤空白文本
        bool blank = true;
        for (char ch : text) {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') { blank = false; break; }
        }
        if (blank && cfg_.use_rec) continue;
        if (cfg_.use_rec && score < (float)cfg_.text_score) continue;

        OcrItem it;
        it.score = score;
        it.text = text;
        for (int k = 0; k < 4; ++k) {
            double x = boxes[i][k * 2], y = boxes[i][k * 2 + 1];
            // 去掉纵向 padding → 去掉比例缩放 → 去掉白边
            y -= pad_top;
            x *= prw; y *= prh;
            x -= pad; y -= pad;
            x = std::min(std::max(x, 0.0), (double)img_in.w - 1);
            y = std::min(std::max(y, 0.0), (double)img_in.h - 1);
            it.box[k][0] = (float)x;
            it.box[k][1] = (float)y;
        }
        res.items.push_back(it);
    }

    // 只留存原图和框，不在这里画：AHK 取的时候（OCR_VisBitmap）再画，不取就是零开销
    last_src = img_in;
    last_items = res.items;
    vis_dirty = true;
    return true;
}

// alpha_bg 字符串 -> 解码模式（0=auto / 1=drop / 2=white / 3=black）
static int alpha_mode_of(const std::string& s) {
    if (s == "drop") return 1;
    if (s == "white") return 2;
    if (s == "black") return 3;
    if (s == "space") return 4;
    return 0;
}

bool OcrEngine::run_file(const wchar_t* path, OcrResult& res, std::string& err) {
    const int am = alpha_mode_of(cfg_.alpha_bg);
    Image img;
    if (!decode_image_file(path, img, err, am)) return false;
    if (!run_image(img, res, err)) return false;
    // 与官方 HF space 对齐：auto 背景一行都没检出时，按「丢弃 alpha」再试一次
    if (am == 0 && cfg_.alpha_retry && img.has_alpha && res.items.empty()) {
        Image img2;
        std::string e2;
        if (decode_image_file(path, img2, e2, 1)) {
            OcrResult r2;
            std::string e3;
            if (run_image(img2, r2, e3) && !r2.items.empty()) res = r2;
        }
    }
    return true;
}

bool OcrEngine::run_buffer(const void* bgra, int w, int h, int stride, OcrResult& res,
                          std::string& err) {
    const int am = alpha_mode_of(cfg_.alpha_bg);
    Image img;
    if (!image_from_bgra(bgra, w, h, stride, img, am)) {
        err = "无效的像素数据（需要 32bpp BGRA、stride >= w*4、w/h > 0）";
        return false;
    }
    if (!run_image(img, res, err)) return false;
    // 与官方 HF space 对齐：auto 背景一行都没检出时，按「丢弃 alpha」再试一次
    if (am == 0 && cfg_.alpha_retry && img.has_alpha && res.items.empty()) {
        Image img2;
        std::string e2;
        if (image_from_bgra(bgra, w, h, stride, img2, 1)) {
            OcrResult r2;
            std::string e3;
            if (run_image(img2, r2, e3) && !r2.items.empty()) res = r2;
        }
    }
    return true;
}
