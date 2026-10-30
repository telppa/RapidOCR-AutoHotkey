/*
RapidOCR v6 for AutoHotkey v1.1
--------------------------------
内核：RapidOCR (https://github.com/rapidai/rapidocr) + PP-OCRv6 ONNX 模型
封装：RapidOCR.dll（onnxruntime 推理）

用法与原版完全一致：
  inst := new RapidOCR(Configs)
  text := inst.ocr(Image)         ; 纯文本
  obj  := inst.ocr_all(Image)     ; 完整结果（对象）

  Image 可以是：本地图片路径、截图坐标 [x,y,w,h]、窗口标题/WinTitle、剪贴板、
               壁纸、鼠标位置、base64 字符串、{file:"x.pdf", index:2} 等，
               一切 ImagePut 支持的输入都支持。

version: 2026.10.29
*/
class RapidOCR
{
	static _loaded := 0

	; 初始化---------------------------------------------------------------------
	__New(Configs := "")
	{
		this.h := 0
		this._loadDll()

		if !IsObject(Configs)
			Configs := {}

		; vis_HBITMAP() 取到的句柄，归本对象所有
		this._vis_hbm     := 0

		; 原样交给 DLL，参数名/默认值都由 DLL 负责
		h := DllCall("RapidOCR.dll\OCR_Create", "Ptr", this._utf8(buf, JSON.dump(Configs)), "Ptr")
		if !h
			throw Exception("RapidOCR 初始化失败：`n" this._lastError())

		this.h       := h
		this.version := StrGet(DllCall("RapidOCR.dll\OCR_Version", "CDecl Ptr"), "UTF-8")
	}

	__Delete()
	{
		if this._vis_hbm
			DllCall("DeleteObject", "Ptr", this._vis_hbm)
		if this.h
		{
			DllCall("RapidOCR.dll\OCR_Destroy", "Ptr", this.h)
			this.h := 0
		}
	}

	; 识别-----------------------------------------------------------------------
	; 返回纯文本，每行一条（行尾带换行）
	ocr(image)
	{
		ret := this.ocr_all(image)
		if (ret.code != 100 && ret.code != 101)
			throw Exception("识别失败：`n" ret.data)

		text := ""
		for k, v in ret.data
			text .= v.text "`n"
		return text
	}

	; 整条结果对象：{code, data:[{"box":..,"score":..,"text":..}...], elapse:{..}}
	; code 101 表示没识别到文字，这里不判断，交给调用方
	ocr_all(image)
	{
		jtxt := this._run(image)
		if (jtxt = "")
			throw Exception("识别失败：`n" this._lastError())
		return JSON.load(jtxt)
	}

	; 可视化---------------------------------------------------------------------
	; 取最近一次识别的标注图（框 + 编号），返回 HBITMAP。
	; 句柄归本对象所有 —— 别自己 DeleteObject，下次调用和销毁时自动释放。
	vis_HBITMAP()
	{
		hb := DllCall("RapidOCR.dll\OCR_VisBitmap", "Ptr", this.h, "CDecl Ptr")
		if !hb
			throw Exception("取标注图失败：`n" this._lastError())
		if this._vis_hbm
			DllCall("DeleteObject", "Ptr", this._vis_hbm)
		this._vis_hbm := hb
		return hb
	}

	; 存盘，格式看扩展名（png/jpg/bmp...）
	vis_save(path)
	{
		return ImagePutFile(this.vis_HBITMAP(), path)
	}

	; 弹窗查看
	vis_show(title := "RapidOCR")
	{
		return ImagePutWindow(this.vis_HBITMAP(), title)
	}

	; base64 字符串（默认 png）；要 data URI 前缀就自己拼 "data:image/png;base64,"
	vis_base64(ext := "png")
	{
		return ImagePutBase64(this.vis_HBITMAP(), ext)
	}

	; 运行中改参数：只覆盖传进来的键，其余保持不动。
	; 阈值、预处理之类立即生效（微秒级）；改到模型 / 线程 / 开关才重建 session（0.2 秒上下）。
	config(cfg)
	{
		if !IsObject(cfg)
			throw Exception("config 要传对象，例如 inst.config({""text_score"": 0.7})")

		if DllCall("RapidOCR.dll\OCR_SetConfig", "Ptr", this.h
		         , "Ptr", this._utf8(buf, JSON.dump(cfg)), "CDecl Int")
			throw Exception("改配置失败：`n" this._lastError())
		return 1
	}

	; 内部-----------------------------------------------------------------------
	_loadDll()
	{
		if RapidOCR._loaded
			return
		dll := A_LineFile . "\..\RapidOCR.dll"
		if !FileExist(dll)
			throw Exception("找不到 RapidOCR.dll：`n" dll)
		if !DllCall("LoadLibrary", "Str", dll, "Ptr")
			throw Exception("无法加载 RapidOCR.dll（位数不匹配或缺少 onnxruntime.dll）`n" dll)
		RapidOCR._loaded := 1
	}

	; 字符串 → UTF-8 缓冲区，返回指针；缓冲区由调用方用 ByRef 变量持有
	_utf8(ByRef buf, s)
	{
		VarSetCapacity(buf, StrPut(s, "UTF-8"), 0)
		StrPut(s, &buf, "UTF-8")
		return &buf
	}

	_lastError()
	{
		return StrGet(DllCall("RapidOCR.dll\OCR_LastError", "CDecl Ptr"), "UTF-8")
	}

	; 判断输入能否直接当本地图片文件交给 WIC 解码
	_isImagePath(img)
	{
		if !IsObject(img) && (img != "") && !InStr(img, "`n") && StrLen(img) < 260
		{
			SplitPath, img, , , ext
			if ext in bmp,dib,rle,jpg,jpeg,jpe,jfif,gif,tif,tiff,png
				if FileExist(img)
					return 1
		}
		return 0
	}

	_run(image)
	{
		; 本地图片文件：交给 DLL 用 WIC 原生解码（最快，且能纠正 EXIF 方向）
		if this._isImagePath(image)
			return this._runFile(image)

		; 其余一切输入（截图区域/窗口/剪贴板/壁纸/URL/base64/PDF 页/HBitmap...）
		; 都先由 ImagePut 归一化成 32bpp BGRA 像素，DLL 直接读，不做任何编解码
		buf := ImagePutBuffer(image)
		return this._runBuffer(buf.ptr, buf.width, buf.height, buf.stride)
	}

	_runFile(path)
	{
		p := DllCall("RapidOCR.dll\OCR_RunFile", "Ptr", this.h, "WStr", path, "CDecl Ptr")
		return this._take(p)
	}

	; ImagePut Buffer：32bpp BGRA、top-down、stride = width*4（ImagePut 的固定输出）
	_runBuffer(ptr, w, h, stride)
	{
		p := DllCall("RapidOCR.dll\OCR_RunBuffer", "Ptr", this.h, "Ptr", ptr
		           , "Int", w, "Int", h, "Int", stride, "CDecl Ptr")
		return this._take(p)
	}

	_take(p)
	{
		if !p
			return ""
		s := StrGet(p, "UTF-8")
		DllCall("RapidOCR.dll\OCR_FreeString", "Ptr", p, "CDecl")
		return s
	}
}

#Include %A_LineFile%\..\Lib\ImagePut.ahk
#Include %A_LineFile%\..\Lib\cJson.ahk