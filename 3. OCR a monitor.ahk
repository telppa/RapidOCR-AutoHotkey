; initialize
; 初始化
inst := new RapidOCR

; ocr whole screen
; 识别所有显示器内容
MsgBox % inst.ocr(0)

; ocr first monitor
; 识别第1台显示器内容
MsgBox % inst.ocr(1)

; ocr second monitor
; 识别第2台显示器内容
; MsgBox % inst.ocr(2)

#Include RapidOCR\RapidOCR.ahk