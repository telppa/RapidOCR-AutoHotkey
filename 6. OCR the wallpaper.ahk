; initialize
; 初始化
inst := new RapidOCR

; ocr the wallpaper
; 识别壁纸
MsgBox % inst.ocr("wallpaper")

#Include RapidOCR\RapidOCR.ahk