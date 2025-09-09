# RapidOCR-AutoHotkey  
RapidOCR AutoHotkey Version.  
RapidOCR AHK 版。  
  <br>
# How to use  
```AutoHotkey
; initialize
; 初始化
inst := new RapidOCR

; Img file ( bmp, dib, rle, jpg, jpeg, jpe, jfif, gif, tif, tiff, png )
; 识别本地图片（支持 bmp, dib, rle, jpg, jpeg, jpe, jfif, gif, tif, tiff, png ）
MsgBox % inst.ocr("test\zh_hans.jpg")

; PDF file page 2
; 识别本地 PDF 文件第2页
MsgBox % inst.ocr({file:"test\en.pdf", index:2})

; ocr a screenshot
; 根据坐标截屏并识别
MsgBox % inst.ocr([0, 0, 100, 200])

; ocr the clipboard
; 识别剪贴板
MsgBox % inst.ocr(ClipboardAll)
```
For more usages, please refer to the sample file.  
更多用法请看示例文件。  
  <br>
# Visualization  
  ![Visualization](https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.3/doc/imgs_results/ch_ppocr_mobile_v2.0/00006737.jpg)  
  ![Visualization](https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.3/doc/imgs_results/french_0.jpg)  
  ![Visualization](https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.3/doc/imgs_results/korean.jpg)  
  ![Visualization](https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/release/2.3/doc/imgs_results/ch_ppocr_mobile_v2.0/img_12.jpg)  
  <br>
# Thanks  
#### Umi-OCR
* https://github.com/hiroi-sora/Umi-OCR  
  
#### ImagePut
* Made by iseahound.  
* https://github.com/iseahound/ImagePut  
  
#### cJson
* Made by G33kDude.  
* https://github.com/G33kDude/cJson.ahk/tree/main  