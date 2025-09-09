; initialize
; 初始化
inst := new RapidOCR

; Img file ( bmp, dib, rle, jpg, jpeg, jpe, jfif, gif, tif, tiff, png )
; 识别本地图片（支持 bmp, dib, rle, jpg, jpeg, jpe, jfif, gif, tif, tiff, png ）
MsgBox % inst.ocr("test\zh_hans.jpg")

; PDF file
; 识别本地 PDF 文件
MsgBox % inst.ocr("test\en.pdf")

; PDF file page 2
; 识别本地 PDF 文件第2页
MsgBox % inst.ocr({file:"test\en.pdf", index:2})

; PDF file last page
; 识别本地 PDF 文件最后1页
MsgBox % inst.ocr({file:"test\en.pdf", index:-1})

#Include RapidOCR\RapidOCR.ahk