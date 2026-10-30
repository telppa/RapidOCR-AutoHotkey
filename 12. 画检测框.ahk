; draw detection boxes: recognize first, draw only when you want to look
; 画检测框：识别完想看图再画，不看就不画
inst := new RapidOCR

; text is all you normally need; take the result object when you want boxes and scores
; 平时只要文本；需要框和置信度就取结果对象，再读它的 data
ret := inst.ocr_all("test\zh_hans.jpg")
arr := ret.data
MsgBox, 0x40, 识别结果, % "共 " arr.Length() " 行，首行：" arr.1.text

; or show it in a window (closing the window doesn't touch the image)
; 或者直接弹窗看（关窗即释放窗口，图还在 inst 里）
inst.vis_show("检测框预览")

; save as PNG, returns the file path
; 存 PNG（返回路径）
path := inst.vis_save(A_ScriptDir . "\zh_hans_vis.png")
MsgBox, 0x40, 已保存, %path%

; get the annotated image (boxes + numbers); the handle belongs to inst,
; so don't DeleteObject it yourself
; 取标注图（框 + 编号）。句柄归 inst 所有，别自己 DeleteObject
; anything else goes through ImagePut, just feed it the handle
; 要拿去干别的，直接喂给 ImagePut：
; hb := inst.vis_HBITMAP()
; put it on the clipboard
; 塞剪贴板
; ImagePutClipboard(hb)
; to base64, png by default, handy for HTTP requests
; 转 base64（默认 png，发网络请求之类）
; b64 := inst.vis_base64()
; or roll your own window
; 自己弹窗
; ImagePutWindow(hb, "预览")

; arr[i] matches the drawn number, 1-based
; boxes dropped by text_score are neither drawn nor numbered
; 编号 i 对应 arr[i]，从 1 开始，和图上标的数字一致
; （被 text_score 过滤掉的框不会画，也不占编号）

ExitApp

#Include RapidOCR\RapidOCR.ahk
