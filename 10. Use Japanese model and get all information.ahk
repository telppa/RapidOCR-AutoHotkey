; initialize
; 初始化
inst := new RapidOCR

; Use Japanese model and get all information including confidence and position.
; Because "get_all_info" is set, so the return value is an object.
; 使用日文模型识别本地图片并返回包括置信度与坐标在内的全部信息。
; 因为设置了 get_all_info ，所以返回值是一个对象。
inst := new RapidOCR({"model":"ja", "get_all_info":1})
ret := inst.ocr("test\ja.jpg")
for k, v in ret
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