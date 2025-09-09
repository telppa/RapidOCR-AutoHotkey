; initialize
; 初始化
inst := new RapidOCR

; All WinTitle format is supported. Affected by SetTitleMatchMode.
; 识别一个程序界面， WinTitle 格式都支持，受 SetTitleMatchMode 影响。

; Run Paint
; 启动画图
Run mspaint.exe
Sleep 1000

; by title
; 根据窗口标题识别一个程序界面（这里用的是画图窗口）
if (A_Language = 0804)
  MsgBox % inst.ocr("无标题 - 画图")
else
  MsgBox % inst.ocr("Untitled - Paint")

; by ahk_class
; 根据窗口类名识别
MsgBox % inst.ocr("ahk_class MSPaintApp")

; by ahk_id
; 根据窗口句柄识别
; MsgBox % inst.ocr("ahk_id 0x123abc")

; by ahk_exe
; 根据进程名识别
MsgBox % inst.ocr("ahk_exe mspaint.exe")

; by ahk_pid
; 根据进程 PID 识别
; MsgBox % inst.ocr("ahk_pid 1234")

#Include RapidOCR\RapidOCR.ahk