; initialize
; 初始化
inst := new RapidOCR

; Get everything: confidence and position, from the result object.
; ocr() only gives you text; ocr_all() gives you code / data / elapse.
; PP-OCRv6 是统一多语种模型，日文不需要单独换模型，直接用默认即可。
; ocr() 只给文本；想要置信度和坐标就用 ocr_all()，它返回 code / data / elapse。
ret := inst.ocr_all("test\ja.jpg")

for k, v in ret.data
{
  text := v.text
  score := v.score
  x1 := v.box.1.1, y1 := v.box.1.2
  x2 := v.box.2.1, y2 := v.box.2.2
  x3 := v.box.3.1, y3 := v.box.3.2
  x4 := v.box.4.1, y4 := v.box.4.2
  MsgBox,
  (Ltrim
  text:`t%text%
  score:`t%score%
  x1,y1:`t%x1%,%y1%
  x2,y2:`t%x2%,%y2%
  x3,y3:`t%x3%,%y3%
  x4,y4:`t%x4%,%y4%
  )
}

#Include RapidOCR\RapidOCR.ahk
