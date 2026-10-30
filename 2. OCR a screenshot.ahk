; initialize
; 初始化
inst := new RapidOCR

; ocr a screenshot
; 根据坐标截屏并识别
MsgBox % inst.ocr([0, 0, 100, 200])

; 屏幕左上角一小块，内容少时可换小模型提速
; inst := new RapidOCR({"model_size":"tiny"})
; MsgBox % inst.ocr([0, 0, 100, 200])

#Include RapidOCR\RapidOCR.ahk
