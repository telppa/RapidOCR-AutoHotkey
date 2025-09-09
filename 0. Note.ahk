MsgBox,
(
Usage:
  inst := new RapidOCR(Configs)
  inst.ocr(Image)
  
  Image
    Can be an image of almost any format.
    For details see: https://github.com/iseahound/ImagePut
  
  Configs
    Below are the supported settings and their default values.
    Some of them will affect the recognition result, all of them can be omitted.
    See the examples or souce code for the meaning.
    {
      "model":            "zh_hans"
      "get_all_info":     0
      "visualize":        0
      
      "cls":              ""
      "det":              ""
      "rec":              ""
      "keys":             ""
      
      "num_threads":      4
      "padding":          50
      "max_side_len":     1024
      "box_thresh":       0.3
      "box_score_thresh": 0.5
      "unclip_ratio":     1.6
      "do_angle":         1
      "most_angle":       1
    }
  
How to switch model:
  inst := new RapidOCR({model:"en"}) ; english model
  inst.ocr(Image)
  
  inst := new RapidOCR({model:"ja"}) ; japanese model
  inst.ocr(Image)
  
  inst := new RapidOCR({model:"ko"}) ; korean model
  inst.ocr(Image)
  
Download other language models:
  Download models.
    https://www.modelscope.cn/models/RapidAI/RapidOCR/files
  Move models to.
    "RapidOCR\Exe\models"
  
  inst := new RapidOCR({det:"your_det_file", rec:"your_rec_file", keys:"your_keys_file"})
  inst.ocr(Image)
  
Thanks:
  Umi-OCR
    https://github.com/hiroi-sora/Umi-OCR
  
  ImagePut
    Made by iseahound.
    https://github.com/iseahound/ImagePut
  
  cJson
    Made by G33kDude.
    https://github.com/G33kDude/cJson.ahk/tree/main
)

MsgBox,
(
用法：
  inst := new RapidOCR(Configs)
  inst.ocr(Image)
  
  Image
    可以是几乎任意形式的图片。
    具体可见：https://github.com/iseahound/ImagePut
  
  Configs
    以下是支持的参数及其默认值。
    部分会影响识别结果，全部可以省略，具体含义看示例或翻代码。
    {
      "model":            默认 "zh_hans" 。可选 "zh_hant" "en" "ja" "ko" 。分别表示简中、繁中、英文、日文、韩文5种模型。
      "get_all_info":     默认0。0表示直接返回文本。1表示返回对象，其中包含 box（坐标），score（置信度），text（文本）。
      "visualize":        默认0。1表示额外生成含有文本框的图像（尚未实现）。
      
      "cls":              默认由 model 参数决定，亦可自行指定覆盖。文本方向检测模型路径。
      "det":              默认由 model 参数决定，亦可自行指定覆盖。文本检测模型路径。
      "rec":              默认由 model 参数决定，亦可自行指定覆盖。文本识别模型路径。
      "keys":             默认由 model 参数决定，亦可自行指定覆盖。文本词典路径。
      
      "num_threads":      默认4。线程数，理论上越接近 CPU 物理核心数越快，实测1和12基本没区别。
      "padding":          默认50。当文字贴近图像边缘时可能导致文本框无法框住，此参数通过在图像外周添加白边，解决此问题。
      "max_side_len":     默认1024。当图像长边大于此值时，等比例缩放图像，使得图像长边为此值。0表示不缩放。如果图像长边小于32，则缩放到32。
      "box_thresh":       默认0.3。像素点得分大于此阈值时，才会被认为是文字像素点。此参数越小则越多像素会被视作文字，越大则相反，适中最好。
      "box_score_thresh": 默认0.5。文本检测结果边框内，所有像素点的平均得分大于该阈值时，才会被认为是文本区域。漏框文字时可调小，反之亦然。
      "unclip_ratio":     默认1.6。文本框大小倍率，越小时文本框贴文本越紧，越大时越松。
      "do_angle":         默认1。1表示启用文字方向检测（可使旋转90~270度的图片正常识别）。0表示禁用。
      "most_angle":       默认1。1表示启用角度投票（整张图片以最大可能文字方向来识别），当禁用文字方向检测时，此参数也不起作用。0表示禁用。
    }
  
感谢：
  Umi-OCR
    https://github.com/hiroi-sora/Umi-OCR
  
  ImagePut
    Made by iseahound.
    https://github.com/iseahound/ImagePut
  
  cJson
    Made by G33kDude.
    https://github.com/G33kDude/cJson.ahk/tree/main
)