; initialize
; 初始化
inst := new RapidOCR

; ocr the clipboard
; 识别剪贴板
MsgBox % inst.ocr(ClipboardAll)

#Include RapidOCR\RapidOCR.ahk