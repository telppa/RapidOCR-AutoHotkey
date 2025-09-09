; initialize
; 初始化
inst := new RapidOCR

; URL can point to Img file, PDF file, base64 string, and so on
; 识别网址（网址可对应图片、 PDF 、 base64 字符串等等上述提到过的全部内容，这里用的是百度的 logo ）
MsgBox % inst.ocr("https://www.baidu.com/img/flexible/logo/pc/result.png")

#Include RapidOCR\RapidOCR.ahk