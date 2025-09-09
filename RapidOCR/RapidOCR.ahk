/*
author:    telppa（空）
version:   2025.09.08
*/
class RapidOCR
{
  __New(Configs := "")
  {
    ; 简中、繁中、英、日、韩共5种文字的默认对应模型文件
    models := {}
    
    models.zh_hans := { "cls":  "ch_ppocr_mobile_v2.0_cls_infer.onnx"
                      , "det":  "ch_PP-OCRv4_det_infer.onnx"
                      , "rec":  "ch_PP-OCRv4_rec_infer.onnx"
                      , "keys": "dict_chinese.txt"}
    
    models.zh_hant := { "cls":  "ch_ppocr_mobile_v2.0_cls_infer.onnx"
                      , "det":  "ch_PP-OCRv4_det_infer.onnx"
                      , "rec":  "chinese_cht_PP-OCRv3_rec_infer.onnx"
                      , "keys": "dict_chinese_cht.txt"}
    
    models.en      := { "cls":  "ch_ppocr_mobile_v2.0_cls_infer.onnx"
                      , "det":  "en_PP-OCRv3_det_infer.onnx"
                      , "rec":  "en_PP-OCRv4_rec_infer.onnx"
                      , "keys": "dict_en.txt"}
    
    models.ja      := { "cls":  "ch_ppocr_mobile_v2.0_cls_infer.onnx"
                      , "det":  "Multilingual_PP-OCRv3_det_infer.onnx"
                      , "rec":  "japan_PP-OCRv4_rec_infer.onnx"
                      , "keys": "dict_japan.txt"}
    
    models.ko      := { "cls":  "ch_ppocr_mobile_v2.0_cls_infer.onnx"
                      , "det":  "Multilingual_PP-OCRv3_det_infer.onnx"
                      , "rec":  "korean_PP-OCRv4_rec_infer.onnx"
                      , "keys": "dict_korean.txt"}
    
    model            := NonNull_Ret(Configs.model           , "zh_hans")
    get_all_info     := NonNull_Ret(Configs.get_all_info    , 0)
    visualize        := NonNull_Ret(Configs.visualize       , 0)
    
    ; 可通过下面4个参数覆盖5种文字的默认对应模型文件
    cls              := NonNull_Ret(Configs.cls             , models[model, "cls"])
    det              := NonNull_Ret(Configs.det             , models[model, "det"])
    rec              := NonNull_Ret(Configs.rec             , models[model, "rec"])
    keys             := NonNull_Ret(Configs.keys            , models[model, "keys"])
    
    num_threads      := NonNull_Ret(Configs.num_threads     , 4, 1, 16)
    padding          := NonNull_Ret(Configs.padding         , 50)
    max_side_len     := NonNull_Ret(Configs.max_side_len    , 1024)
    box_thresh       := NonNull_Ret(Configs.box_thresh      , 0.3, 0, 1)
    box_score_thresh := NonNull_Ret(Configs.box_score_thresh, 0.5, 0, 1)
    unclip_ratio     := NonNull_Ret(Configs.unclip_ratio    , 1.6)
    do_angle         := NonNull_Ret(Configs.do_angle        , 1)
    most_angle       := NonNull_Ret(Configs.most_angle      , 1)
    
    ; 命令行模板
    template = 
    (LTrim Join`s
    RapidOCR-json.exe --models="models" --ensureAscii=1
    --cls="%cls%"
    --det="%det%"
    --rec="%rec%"
    --keys="%keys%"
    --numThread="%num_threads%"
    --padding="%padding%"
    --maxSideLen="%max_side_len%"
    --boxThresh="%box_thresh%"
    --boxScoreThresh="%box_score_thresh%"
    --unClipRatio="%unclip_ratio%"
    --doAngle="%do_angle%"
    --mostAngle="%most_angle%"
    )
    
    ; 设置工作目录，否则无法启动 RapidOCR-json.exe
    ; 方法1 设置 shell.CurrentDirectory 可对 shell.exec() 生效，但会导致 A_WorkingDir 发生变化
    ; 方法2 手动备份并设置 A_WorkingDir ，之后再还原此值
    exe_dir := A_LineFile "\..\exe"
    prev_workingdir := A_WorkingDir
    SetWorkingDir % exe_dir
    
    ; 检查4个模型文件是否存在，不存在则无法启动
    if (!FileExist("models\" cls))
    {
      MsgBox, 0x40010, , cls model "%cls%" not found.
      ExitApp
    }
    if (!FileExist("models\" det))
    {
      MsgBox, 0x40010, , det model "%det%" not found.
      ExitApp
    }
    if (!FileExist("models\" rec))
    {
      MsgBox, 0x40010, , rec model "%rec%" not found.
      ExitApp
    }
    if (!FileExist("models\" keys))
    {
      MsgBox, 0x40010, , keys file "%keys%" not found.
      ExitApp
    }
    
    ; debug 用
    ; Run cmd.exe /k %template%
    
    ; 隐藏控制台
    ; https://www.reddit.com/r/AutoHotkey/comments/phhkcq/yet_another_how_do_i_sendreceive_from_cmd_window/
    Run % A_ComSpec,, Hide, cmdPid
    DetectHiddenWindows On
    WinWait % "ahk_pid" cmdPid
    DllCall("AttachConsole", "UInt", cmdPid)
    Process Close, % cmdPid
    
    ; 启动 RapidOCR-json.exe
    ; https://learn.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/windows-scripting/ateytk4a(v=vs.84)
    shell := ComObjCreate("WScript.Shell")
    exec := shell.Exec(template)
    DllCall("FreeConsole")
    SetWorkingDir % prev_workingdir
    
    ; 等待程序运行
    while (exec.Status)
    {
      Sleep 100
      
      ; 启动超过10秒则报错
      if (A_Index >= 100)
      {
        MsgBox, 0x40010, , Failed to run "RapidOCR-json.exe".
        ExitApp
      }
    }
    
    ; 跳过第一行 读取第二行
    exec.StdOut.ReadLine()
    line := exec.StdOut.ReadLine()
    if (line = "OCR init completed.")
    {
      this.get_all_info := get_all_info
      this.visualize    := visualize
      this.exec         := exec
      return this
    }
    else
    {
      MsgBox, 0x40010, , Failed to init RapidOCR.`r`n`r`nError:%line%
      ExitApp
    }
  }
  
  __Delete()
  {
    try this.exec.Terminate()
  }
  
  ocr(image)
  {
    try
    {
      ; 不能在此预先缩放图像，否则返回坐标时会与用户传入的原图对不上
      obj := {"image_base64": ImagePutBase64(image, "png")}
      this.exec.StdIn.WriteLine(json.dump(obj))
      
      ; 这里会耗时 200 ms 左右
      ret := json.load(this.exec.StdOut.ReadLine())
      if (ret.code = 100 or ret.code = 101)
      {
        ; 里面有3组数据，分别为 box score text
        if (this.get_all_info)
          return ret.data
        else
        {
          for k, v in ret.data
            text .= v.text "`n"
          
          return text
        }
      }
      else
        throw Exception(ret.data)
    }
    catch e
      throw Exception(e.Message, -1)
  }
}

#Include %A_LineFile%\..\Lib\ImagePut.ahk
#Include %A_LineFile%\..\Lib\NonNull.ahk
#Include %A_LineFile%\..\Lib\cJson.ahk