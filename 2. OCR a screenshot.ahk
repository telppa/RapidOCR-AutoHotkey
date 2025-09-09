; initialize
; 初始化
inst := new RapidOCR

; ocr a screenshot
; 根据坐标截屏并识别
MsgBox % inst.ocr([0, 0, 100, 200])

#Include RapidOCR\RapidOCR.ahk