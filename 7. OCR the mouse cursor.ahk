; initialize
; 初始化
inst := new RapidOCR

; ocr the mouse cursor
; 识别鼠标指针
MsgBox % inst.ocr(A_Cursor)

#Include RapidOCR\RapidOCR.ahk