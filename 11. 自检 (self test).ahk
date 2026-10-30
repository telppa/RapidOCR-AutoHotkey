; 自检：确认 DLL、onnxruntime、模型都在位，并跑一遍常见输入。
; 换电脑、搬目录、换模型之后建议先跑一次。
SetBatchLines -1
SetWorkingDir %A_ScriptDir%

report := ""

t := A_TickCount
inst := new RapidOCR
report .= "① 初始化`t`t" (A_TickCount - t) " ms`n"
report .= "   内核： " inst.version "`n"

t := A_TickCount
txt := inst.ocr("test\zh_hans.jpg")
report .= "② 文档图片`t" (A_TickCount - t) " ms，  " StrLen(txt) " 字符`n"

t := A_TickCount
logo := inst.ocr("test\logo.png")
report .= "③ 小图 logo`t" (A_TickCount - t) " ms，  [" Trim(logo) "]`n"

inst2 := new RapidOCR({"model_size": "tiny"})
t := A_TickCount
txt3 := inst2.ocr([0, 0, 300, 150])
report .= "④ 截图区域`t" (A_TickCount - t) " ms，  " StrLen(txt3) " 字符（tiny 模型）`n"

t := A_TickCount
txt4 := inst2.ocr({"file": "test\en.pdf", "index": 2})
report .= "⑤ PDF 第2页`t" (A_TickCount - t) " ms，  " StrLen(txt4) " 字符`n"

; 标注图：按需绘制（识别时不画），存成 PNG
inst3 := new RapidOCR
txt5 := inst3.ocr("test\zh_hans.jpg")
t := A_TickCount
vp := inst3.vis_save(A_Temp . "\RapidOCR_self_vis.png")
FileGetSize, vsz, %vp%
report .= "⑥ 标注图`t`t" (A_TickCount - t) " ms，  " vsz " 字节`n"

; ③ 是关键回归项：202×66 透明 PNG，曾经因为把短边放大 11 倍而检测不出
if (StrLen(txt) < 10 || StrLen(txt4) < 10 || StrLen(logo) < 2 || vsz < 1000)
{
	MsgBox, 0x10, RapidOCR 自检, 识别结果异常，请检查 models 与 onnxruntime.dll 是否完整。`n`n%report%
	ExitApp
}

MsgBox, 0x40, RapidOCR 自检,
(LTrim
全部通过。

%report%
（⑥ 的标注图已存到 %A_Temp%\RapidOCR_self_vis.png，可打开查看）

第一行识别结果：
%txt%
)

ExitApp

#Include RapidOCR\RapidOCR.ahk
