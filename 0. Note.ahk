MsgBox,
(
Usage:
  inst := new RapidOCR(Configs)
  inst.ocr(Image)

  Image
    Same as the original wrapper, anything ImagePut accepts:
      image file            inst.ocr("test\zh_hans.jpg")
      screen region         inst.ocr([0, 0, 100, 200])
      monitor               inst.ocr(0)  ; 0=all 1=first 2=second
      window                inst.ocr("ahk_exe notepad.exe")
      clipboard             inst.ocr(ClipboardAll)
      wallpaper             inst.ocr("wallpaper")
      mouse cursor          inst.ocr(A_Cursor)
      base64 string         inst.ocr("iVBORw0KGgo...")
      PDF page 2            inst.ocr({file:"test\en.pdf", index:2})
      URL                   inst.ocr("https://.../logo.png")

  Configs (all optional)
    {
      "model":            kept for compatibility with old code. PP-OCRv6 uses one
                          multilingual model for zh-Hans / zh-Hant / en / ja, so
                          this no longer affects model selection.
      "model_size":       default "small".
                          "tiny" is the fastest (about a third of small),
                          "medium" is the most accurate and the slowest.
      "cls" "det" "rec" "keys"
                          default comes from model_size, override to use your own
                          files. Relative paths resolve against RapidOCR\models,
                          absolute paths also work.

      "num_threads":      default 4. CPU threads. 4 measured fastest, 8/16 were
                          slower.
      "padding":          default 0. Pixels of white border around the image (the
                          old version defaulted to 50).
      "text_score":       default 0.5. Lines below this score are dropped.
      "max_side_len":     default 2000. Downscale when the long side exceeds it.
      "min_side_len":     default 30. Upscale when the short side is below it.
      "box_thresh":       default 0.3. Pixel threshold, lower means more pixels
                          are treated as text.
      "box_score_thresh": default 0.5. Mean box score, lower it when boxes are
                          missed.
      "unclip_ratio":     default 1.6. Box expansion ratio.
      "do_angle":         default 1. 1 = detect 180-degree rotation, 0 = off.
      "most_angle":       no longer used (the new engine has no angle voting),
                          kept for compatibility only.

      "det_limit_side_len": default 736. Detector input side length.
      "det_limit_type":     default "min". min = upscale when the short side is
                            short; "max" = downscale when the long side is long.
      "det_max_upscale":    default 3. Cap on short-side upscaling. Small images
                            (logos, small screenshots) get worse when blown up
                            too far, so it stops at 3x; set 0 for no cap, which
                            is exactly rapidocr's behaviour (short side straight
                            to 736).
      "alpha_bg":           default "auto". Background for transparent images:
                            auto  = rapidocr's rule (dark text on white /
                                    light text on black)
                            drop  = drop alpha
                            white / black = fixed colour
                            space = reproduce the official HF space input
                                    (drop alpha + swap red and blue)
      "alpha_retry":        default 1. In auto mode, if nothing was detected and
                            the image really has transparent pixels, run once
                            more with drop (it can only help).
      "rec_batch_num":      default 6. Recognition batch size.
      "use_det" "use_rec" "use_cls"  default 1. Per-stage switches.
    }

Switch models:
  inst := new RapidOCR({"model_size":"tiny"})    ; fastest
  inst := new RapidOCR({"model_size":"medium"})  ; most accurate
  inst := new RapidOCR({"det":"my_det.onnx", "rec":"my_rec.onnx"})

Other methods:
  inst.ocr(image)      plain text, one line per detected line
  inst.ocr_all(image)  everything as an object: code / data / elapse
  inst.version         engine version string
  inst.config({...})   change parameters on the fly; only the keys you pass
                       are replaced. thresholds / preprocessing apply at once
                       (microseconds); models / threads / switches rebuild
                       the sessions (about 0.2 s)

Annotated image (boxes + numbers; not a config, drawn only when you ask):
  hb := inst.vis_HBITMAP()   annotated image of the last recognition, HBITMAP
  inst.vis_save("x.png")     save to file, format taken from the extension
  inst.vis_show("preview")   show in a window
  b64 := inst.vis_base64()   to base64 (png by default)
  hb belongs to inst, do not DeleteObject it yourself, it is auto released on
  the next call or when the object is destroyed.
  Number i on the image is index i in the data array of ocr_all (1-based).

Environment:
  AutoHotkey v1.1 x64 (AutoHotkeyU64.exe). A 32-bit AHK cannot load this DLL.
  onnxruntime.dll and the models folder must sit next to the DLL; it cannot be
  copied out on its own.

Engine:
  RapidOCR (rapidai/rapidocr) + PP-OCRv6 ONNX models, running inside
  RapidOCR.dll via ONNX Runtime. No Python, no child process, no console window.

Thanks:
  RapidOCR     https://github.com/rapidai/rapidocr
  PaddleOCR    https://github.com/PaddlePaddle/PaddleOCR
  ONNX Runtime https://github.com/microsoft/onnxruntime
  ImagePut     Made by iseahound.
  cJson        Made by G33kDude.
)

MsgBox,
(
用法：
  inst := new RapidOCR(Configs)
  inst.ocr(Image)

  Image
    与旧版一致，凡 ImagePut 支持的输入都支持：
      本地图片路径        inst.ocr("test\zh_hans.jpg")
      屏幕区域            inst.ocr([0, 0, 100, 200])
      显示器              inst.ocr(0)  ; 0=全部 1=第1台 2=第2台
      窗口                inst.ocr("ahk_exe notepad.exe")
      剪贴板              inst.ocr(ClipboardAll)
      壁纸                inst.ocr("wallpaper")
      鼠标指针            inst.ocr(A_Cursor)
      base64 字符串       inst.ocr("iVBORw0KGgo...")
      PDF 第2页           inst.ocr({file:"test\en.pdf", index:2})
      网址                inst.ocr("https://.../logo.png")

  Configs（全部可省略）
    {
      "model":            兼容旧代码保留。PP-OCRv6 用同一个多语种模型覆盖
                          简中/繁中/英/日，此项已不再影响模型选择。
      "model_size":       默认 "small"。
                          "tiny" 最快（约为 small 的 1/3 时间），"medium" 最准最慢。
      "cls" "det" "rec" "keys"
                          默认由 model_size 决定，可自行指定覆盖。
                          相对路径按 RapidOCR\models 解析，也可写绝对路径。

      "num_threads":      默认 4。CPU 线程数。实测 4 最快，8/16 反而更慢。
      "padding":          默认 0。图像外周加白边的像素数（旧版默认 50）。
      "text_score":       默认 0.5。低于此置信度的行会被丢弃。
      "max_side_len":     默认 2000。长边超过则等比缩小。
      "min_side_len":     默认 30。短边小于则等比放大。
      "box_thresh":       默认 0.3。像素阈值，越小越多像素被当作文字。
      "box_score_thresh": 默认 0.5。文本框平均分阈值，漏框时调小。
      "unclip_ratio":     默认 1.6。文本框外扩倍率。
      "do_angle":         默认 1。1=启用 180 度方向检测，0=关闭。
      "most_angle":       已失效（新版内核没有角度投票机制），保留仅为兼容。

      "det_limit_side_len": 默认 736。检测网络输入边长。
      "det_limit_type":     默认 "min"。min=短边不足则放大；"max"=长边超限则缩小。
      "det_max_upscale":    默认 3。短边放大倍率上限。小图（logo、小截图）放大过头
                            反而检测不出，所以默认最多放大 3 倍；设 0 = 不限制，
                            完全照 rapidocr 原始行为（短边直接拉到 736）。
      "alpha_bg":           默认 "auto"。透明图的合成背景：
                            auto  = rapidocr 原规则（暗字白底 / 亮字黑底）
                            drop  = 直接丢掉 alpha
                            white / black = 固定底色
                            space = 复刻官方 HF space 的输入（丢 alpha + 红蓝互换）
      "alpha_retry":        默认 1。auto 模式下一行都没检出、图又确实带透明像素时，
                            按 drop 再跑一次（只会更好，不会更差）。
      "rec_batch_num":      默认 6。识别批大小。
      "use_det" "use_rec" "use_cls"  默认 1。分模块开关。
    }

换模型：
  inst := new RapidOCR({"model_size":"tiny"})    ; 最快
  inst := new RapidOCR({"model_size":"medium"})  ; 最准
  inst := new RapidOCR({"det":"my_det.onnx", "rec":"my_rec.onnx"})

其它方法：
  inst.ocr(image)      纯文本，每行一条
  inst.ocr_all(image)  整条结果对象：code / data / elapse
  inst.version         内核版本字符串
  inst.config({...})   运行中改参数，只覆盖传进来的键。阈值、预处理之类立即
                       生效（微秒级）；改到模型、线程、开关才重建 session，
                       0.2 秒上下

标注图（画了检测框 + 编号，不是配置项，你取才画，不取不花时间）：
  hb := inst.vis_HBITMAP()   最近一次识别的标注图，返回 HBITMAP
  inst.vis_save("x.png")     存盘，格式看扩展名
  inst.vis_show("预览")      弹窗查看
  b64 := inst.vis_base64()   转 base64（默认 png）
  hb 归 inst 所有，别自己 DeleteObject，下次调用/对象销毁时自动释放。
  检测框编号 = ocr_all 返回的 data 下标（从 1 起）。

环境要求：
  AutoHotkey v1.1 x64（AutoHotkeyU64.exe）。32 位 AHK 无法加载本 DLL。
  同目录必须有 onnxruntime.dll 与 models 文件夹，不可单独拷走 DLL。

内核：
  RapidOCR (rapidai/rapidocr) + PP-OCRv6 ONNX 模型，进程内 ONNX Runtime 推理。
  无 Python、无子进程、无隐藏控制台窗口。

感谢：
  RapidOCR     https://github.com/rapidai/rapidocr
  PaddleOCR    https://github.com/PaddlePaddle/PaddleOCR
  ONNX Runtime https://github.com/microsoft/onnxruntime
  ImagePut     Made by iseahound.
  cJson        Made by G33kDude.
)
