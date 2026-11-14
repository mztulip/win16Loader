program PAINT;

{$mode delphi}

uses
  WinProcs, WinTypes, Strings;

const
  { Drawing Tools }
  TOOL_PENCIL       = 1;
  TOOL_BRUSH        = 2;
  TOOL_AIRBRUSH     = 3;
  TOOL_ERASER       = 4;
  TOOL_COLOR_ERASER = 5;
  TOOL_FILL         = 6;
  TOOL_LINE         = 7;
  TOOL_RECT         = 8;
  TOOL_ROUNDRECT    = 9;
  TOOL_ELLIPSE      = 10;
  TOOL_POLYGON      = 11;
  TOOL_EYEDROPPER   = 12;
  TOOL_TEXT         = 13;
  TOOL_SELECT       = 14;
  TOOL_COUNT        = 14;

  { Shape Styles }
  STYLE_OUTLINE       = 1;
  STYLE_FILLED_BORDER = 2;
  STYLE_FILLED_SOLID  = 3;

  { Canvas Default Dimensions }
  CANVAS_DEF_W = 580;
  CANVAS_DEF_H = 310;
  CANVAS_X     = 10;
  CANVAS_Y     = 88;

  { Menu Identifiers }
  IDM_NEW       = 1001;
  IDM_OPEN      = 1002;
  IDM_SAVE      = 1003;
  IDM_SAVEAS    = 1004;
  IDM_EXIT      = 1005;

  IDM_CUT       = 1006;
  IDM_COPY      = 1007;
  IDM_PASTE     = 1008;
  IDM_DELETE    = 1009;

  IDM_UNDO      = 1010;
  IDM_CLEAR     = 1011;
  IDM_INVERT    = 1012;
  IDM_FLIPH     = 1013;
  IDM_FLIPV     = 1014;
  IDM_SELECTALL = 1015;

  IDM_ABOUT     = 1020;

  { DrawText Flags }
  DT_TOP        = $0000;
  DT_LEFT       = $0000;
  DT_CENTER     = $0001;
  DT_RIGHT      = $0002;
  DT_VCENTER    = $0004;
  DT_BOTTOM     = $0008;
  DT_WORDBREAK  = $0010;
  DT_SINGLELINE = $0020;

  { FloodFill Flags }
  FLOODFILLBORDER  = 0;
  FLOODFILLSURFACE = 1;

  { ROP }
  DSTINVERT = $00550009;

type
  TOpenFileName = record
    lStructSize: Longint;
    hwndOwner: HWnd;
    hInstance: THandle;
    lpstrFilter: PChar;
    lpstrCustomFilter: PChar;
    nMaxCustFilter: Longint;
    nFilterIndex: Longint;
    lpstrFile: PChar;
    nMaxFile: Longint;
    lpstrFileTitle: PChar;
    nMaxFileTitle: Longint;
    lpstrInitialDir: PChar;
    lpstrTitle: PChar;
    Flags: Longint;
    nFileOffset: Word;
    nFileExtension: Word;
    lpstrDefExt: PChar;
    lCustData: Longint;
    lpfnHook: Pointer;
    lpTemplateName: PChar;
  end;

  TBitmapFileHeader = packed record
    bfType: Word;
    bfSize: Longint;
    bfReserved1: Word;
    bfReserved2: Word;
    bfOffBits: Longint;
  end;

  TBitmapInfoHeader = packed record
    biSize: Longint;
    biWidth: Longint;
    biHeight: Longint;
    biPlanes: Word;
    biBitCount: Word;
    biCompression: Longint;
    biSizeImage: Longint;
    biXPelsPerMeter: Longint;
    biYPelsPerMeter: Longint;
    biClrUsed: Longint;
    biClrImportant: Longint;
  end;

  TRgbQuad = packed record
    rgbBlue: Byte;
    rgbGreen: Byte;
    rgbRed: Byte;
    rgbReserved: Byte;
  end;

  TToolBtn = record
    R: TRect;
    Id: Integer;
    Name: array[0..15] of Char;
  end;

function GetOpenFileName(var OpenFile: TOpenFileName): Bool; far; external 'COMMDLG' index 1;
function GetSaveFileName(var OpenFile: TOpenFileName): Bool; far; external 'COMMDLG' index 2;

var
  G_Instance: THandle;
  G_MainWnd: HWND;
  G_CurFile: array[0..128] of Char;
  G_TitleBuf: array[0..160] of Char;
  G_StatusText: array[0..120] of Char;

  { GDI Canvas Buffering }
  G_CanvasW: Integer;
  G_CanvasH: Integer;
  G_hdcCanvas: HDC;
  G_hBmpCanvas: HBITMAP;
  G_hBmpOldCanvas: HBITMAP;

  G_hdcUndo: HDC;
  G_hBmpUndo: HBITMAP;
  G_hBmpOldUndo: HBITMAP;
  G_CanUndo: Boolean;

  G_hdcTemp: HDC;
  G_hBmpTemp: HBITMAP;
  G_hBmpOldTemp: HBITMAP;

  { Selection State }
  G_HasSel: Boolean;
  G_SelRc: TRect;
  G_SelW, G_SelH: Integer;
  G_hdcSel: HDC;
  G_hBmpSel: HBITMAP;
  G_hBmpOldSel: HBITMAP;
  G_SelFloating: Boolean;
  G_SelDragging: Boolean;
  G_SelAnchorX, G_SelAnchorY: Integer;

  { Clipboard Buffer }
  G_hdcClip: HDC;
  G_hBmpClip: HBITMAP;
  G_hBmpOldClip: HBITMAP;
  G_ClipW, G_ClipH: Integer;
  G_HasClip: Boolean;

  { Tool States }
  G_ActiveTool: Integer;
  G_BrushSize: Integer;
  G_ShapeStyle: Integer;
  G_ColPrimary: TColorRef;
  G_ColSecondary: TColorRef;

  G_IsDrawing: Boolean;
  G_IsDragging: Boolean;
  G_StartX, G_StartY: Integer;
  G_LastX, G_LastY: Integer;
  G_CurX, G_CurY: Integer;

  { Polygon }
  G_PolyPoints: array[0..31] of TPoint;
  G_PolyCount: Integer;

  { UI Elements }
  G_Tools: array[1..TOOL_COUNT] of TToolBtn;
  G_Palette: array[0..27] of TColorRef;

  { Pseudo-random Seed for Spray }
  G_RandSeed: Word;

  { BMP I/O Buffers (global to protect stack) }
  G_BmpRow: array[0..4095] of Byte;
  G_BmpPalette: array[0..255] of TRgbQuad;

procedure Dbg(S: PChar);
begin
  OutputDebugString(S);
end;

procedure IntToStrBuf(Val: Integer; P: PChar);
var
  S: ShortString;
begin
  Str(Val, S);
  StrPCopy(P, S);
end;

function SimpleRand: Word;
begin
  G_RandSeed := Word(G_RandSeed * 25173 + 13849);
  Result := G_RandSeed;
end;

procedure SetStatus(S: PChar);
var
  Rc: TRect;
begin
  StrCopy(G_StatusText, S);
  GetClientRect(G_MainWnd, Rc);
  Rc.top := Rc.bottom - 22;
  InvalidateRect(G_MainWnd, @Rc, True);
end;

procedure InitPalette;
begin
  { Row 1 - Deep & vibrant colors }
  G_Palette[0]  := RGB(0, 0, 0);
  G_Palette[1]  := RGB(96, 96, 96);
  G_Palette[2]  := RGB(144, 0, 0);
  G_Palette[3]  := RGB(220, 20, 20);
  G_Palette[4]  := RGB(240, 110, 0);
  G_Palette[5]  := RGB(248, 192, 0);
  G_Palette[6]  := RGB(248, 240, 32);
  G_Palette[7]  := RGB(0, 112, 32);
  G_Palette[8]  := RGB(16, 176, 64);
  G_Palette[9]  := RGB(112, 224, 32);
  G_Palette[10] := RGB(0, 128, 128);
  G_Palette[11] := RGB(0, 200, 224);
  G_Palette[12] := RGB(16, 32, 144);
  G_Palette[13] := RGB(32, 96, 224);

  { Row 2 - Light, pastel and accents }
  G_Palette[14] := RGB(255, 255, 255);
  G_Palette[15] := RGB(192, 192, 192);
  G_Palette[16] := RGB(128, 64, 16);
  G_Palette[17] := RGB(240, 128, 128);
  G_Palette[18] := RGB(255, 192, 160);
  G_Palette[19] := RGB(255, 255, 176);
  G_Palette[20] := RGB(176, 240, 192);
  G_Palette[21] := RGB(128, 208, 255);
  G_Palette[22] := RGB(200, 232, 255);
  G_Palette[23] := RGB(80, 48, 160);
  G_Palette[24] := RGB(144, 48, 192);
  G_Palette[25] := RGB(224, 40, 192);
  G_Palette[26] := RGB(255, 128, 192);
  G_Palette[27] := RGB(160, 120, 80);
end;

procedure InitButtons;
var
  I, X, Y, W, H: Integer;
begin
  W := 38;
  H := 20;
  Y := 4;
  for I := 1 to TOOL_COUNT do
  begin
    X := 8 + (I - 1) * (W + 3);
    G_Tools[I].R.left   := X;
    G_Tools[I].R.top    := Y;
    G_Tools[I].R.right  := X + W;
    G_Tools[I].R.bottom := Y + H;
    G_Tools[I].Id       := I;
    case I of
      TOOL_PENCIL:       StrCopy(G_Tools[I].Name, 'Penc');
      TOOL_BRUSH:        StrCopy(G_Tools[I].Name, 'Brsh');
      TOOL_AIRBRUSH:     StrCopy(G_Tools[I].Name, 'Spry');
      TOOL_ERASER:       StrCopy(G_Tools[I].Name, 'Eras');
      TOOL_COLOR_ERASER: StrCopy(G_Tools[I].Name, 'C.Er');
      TOOL_FILL:         StrCopy(G_Tools[I].Name, 'Fill');
      TOOL_LINE:         StrCopy(G_Tools[I].Name, 'Line');
      TOOL_RECT:         StrCopy(G_Tools[I].Name, 'Rect');
      TOOL_ROUNDRECT:    StrCopy(G_Tools[I].Name, 'RndR');
      TOOL_ELLIPSE:      StrCopy(G_Tools[I].Name, 'Oval');
      TOOL_POLYGON:      StrCopy(G_Tools[I].Name, 'Poly');
      TOOL_EYEDROPPER:   StrCopy(G_Tools[I].Name, 'Pick');
      TOOL_TEXT:         StrCopy(G_Tools[I].Name, 'Text');
      TOOL_SELECT:       StrCopy(G_Tools[I].Name, 'Sel.');
    end;
  end;
end;

function ToolSupportsSize(Tool: Integer): Boolean;
begin
  case Tool of
    TOOL_BRUSH, TOOL_AIRBRUSH, TOOL_ERASER, TOOL_COLOR_ERASER,
    TOOL_LINE, TOOL_RECT, TOOL_ROUNDRECT, TOOL_ELLIPSE, TOOL_POLYGON:
      ToolSupportsSize := True;
    else
      ToolSupportsSize := False;
  end;
end;

procedure SaveUndoState;
begin
  if (G_hdcCanvas <> 0) and (G_hdcUndo <> 0) then
  begin
    BitBlt(G_hdcUndo, 0, 0, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
    G_CanUndo := True;
  end;
end;

procedure RenderCanvasToDC(TargetDC: HDC);
var
  HP, OldPen: HPEN;
  OldBr: HBRUSH;
begin
  if (G_hdcCanvas = 0) or (G_hdcTemp = 0) then Exit;

  BitBlt(G_hdcTemp, 0, 0, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);

  if G_HasSel and G_SelFloating and (G_hdcSel <> 0) and (G_hBmpSel <> 0) then
    BitBlt(G_hdcTemp, G_SelRc.left, G_SelRc.top, G_SelW, G_SelH, G_hdcSel, 0, 0, SRCCOPY);

  if G_HasSel then
  begin
    HP := CreatePen(PS_DOT, 1, RGB(0, 0, 0));
    OldPen := SelectObject(G_hdcTemp, HP);
    OldBr := SelectObject(G_hdcTemp, GetStockObject(NULL_BRUSH));
    Rectangle(G_hdcTemp, G_SelRc.left, G_SelRc.top, G_SelRc.right, G_SelRc.bottom);
    SelectObject(G_hdcTemp, OldBr);
    SelectObject(G_hdcTemp, OldPen);
    DeleteObject(HP);
  end;

  BitBlt(TargetDC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcTemp, 0, 0, SRCCOPY);
end;

procedure RefreshCanvas;
var
  DC: HDC;
begin
  if G_MainWnd <> 0 then
  begin
    DC := GetDC(G_MainWnd);
    if DC <> 0 then
    begin
      RenderCanvasToDC(DC);
      ReleaseDC(G_MainWnd, DC);
    end;
  end;
end;

procedure CommitSelection;
begin
  if not G_HasSel then Exit;

  if G_SelFloating and (G_hdcSel <> 0) and (G_hBmpSel <> 0) and (G_hdcCanvas <> 0) then
  begin
    BitBlt(G_hdcCanvas, G_SelRc.left, G_SelRc.top, G_SelW, G_SelH, G_hdcSel, 0, 0, SRCCOPY);
    SelectObject(G_hdcSel, G_hBmpOldSel);
    DeleteObject(G_hBmpSel);
    G_hBmpSel := 0;
    G_SelFloating := False;
  end;
  G_HasSel := False;
  G_SelDragging := False;
  RefreshCanvas;
end;

procedure DoCopy;
var
  HdcScr: HDC;
  HBmp: HBITMAP;
begin
  if G_MainWnd = 0 then Exit;
  HdcScr := GetDC(G_MainWnd);

  if G_HasSel then
  begin
    HBmp := CreateCompatibleBitmap(HdcScr, G_SelW, G_SelH);
    if G_hBmpClip <> 0 then DeleteObject(G_hBmpClip);
    G_hBmpClip := HBmp;
    SelectObject(G_hdcClip, G_hBmpClip);
    G_ClipW := G_SelW;
    G_ClipH := G_SelH;
    G_HasClip := True;

    if G_SelFloating and (G_hdcSel <> 0) then
      BitBlt(G_hdcClip, 0, 0, G_SelW, G_SelH, G_hdcSel, 0, 0, SRCCOPY)
    else
      BitBlt(G_hdcClip, 0, 0, G_SelW, G_SelH, G_hdcCanvas, G_SelRc.left, G_SelRc.top, SRCCOPY);
    SetStatus('Selection copied to clipboard.');
  end
  else
  begin
    HBmp := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
    if G_hBmpClip <> 0 then DeleteObject(G_hBmpClip);
    G_hBmpClip := HBmp;
    SelectObject(G_hdcClip, G_hBmpClip);
    G_ClipW := G_CanvasW;
    G_ClipH := G_CanvasH;
    G_HasClip := True;
    BitBlt(G_hdcClip, 0, 0, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
    SetStatus('Entire canvas copied to clipboard.');
  end;
  ReleaseDC(G_MainWnd, HdcScr);
end;

procedure DoCut;
var
  HBr: HBRUSH;
begin
  if not G_HasSel then
  begin
    SetStatus('Nothing selected to cut.');
    Exit;
  end;
  DoCopy;
  SaveUndoState;
  if not G_SelFloating then
  begin
    HBr := CreateSolidBrush(G_ColSecondary);
    FillRect(G_hdcCanvas, G_SelRc, HBr);
    DeleteObject(HBr);
  end
  else
  begin
    if G_hBmpSel <> 0 then
    begin
      SelectObject(G_hdcSel, G_hBmpOldSel);
      DeleteObject(G_hBmpSel);
      G_hBmpSel := 0;
    end;
    G_SelFloating := False;
  end;
  G_HasSel := False;
  G_SelDragging := False;
  RefreshCanvas;
  SetStatus('Selection cut to clipboard.');
end;

procedure DoPaste;
var
  HdcScr: HDC;
begin
  if (not G_HasClip) or (G_MainWnd = 0) then
  begin
    SetStatus('Clipboard is empty.');
    Exit;
  end;
  CommitSelection;
  SaveUndoState;

  HdcScr := GetDC(G_MainWnd);
  if G_hBmpSel <> 0 then
  begin
    SelectObject(G_hdcSel, G_hBmpOldSel);
    DeleteObject(G_hBmpSel);
  end;
  G_hBmpSel := CreateCompatibleBitmap(HdcScr, G_ClipW, G_ClipH);
  ReleaseDC(G_MainWnd, HdcScr);

  SelectObject(G_hdcSel, G_hBmpSel);
  BitBlt(G_hdcSel, 0, 0, G_ClipW, G_ClipH, G_hdcClip, 0, 0, SRCCOPY);

  G_HasSel := True;
  G_SelFloating := True;
  G_SelW := G_ClipW;
  G_SelH := G_ClipH;
  G_SelRc.left := 10;
  G_SelRc.top := 10;
  G_SelRc.right := 10 + G_SelW;
  G_SelRc.bottom := 10 + G_SelH;
  G_ActiveTool := TOOL_SELECT;

  InvalidateRect(G_MainWnd, nil, False);
  RefreshCanvas;
  SetStatus('Pasted from clipboard. Drag inside to move, click outside to stamp.');
end;

procedure DoDelete;
var
  HBr: HBRUSH;
begin
  if not G_HasSel then Exit;
  SaveUndoState;
  if not G_SelFloating then
  begin
    HBr := CreateSolidBrush(G_ColSecondary);
    FillRect(G_hdcCanvas, G_SelRc, HBr);
    DeleteObject(HBr);
  end
  else
  begin
    if G_hBmpSel <> 0 then
    begin
      SelectObject(G_hdcSel, G_hBmpOldSel);
      DeleteObject(G_hBmpSel);
      G_hBmpSel := 0;
    end;
    G_SelFloating := False;
  end;
  G_HasSel := False;
  G_SelDragging := False;
  RefreshCanvas;
  SetStatus('Selection cleared.');
end;

procedure DoSelectAll;
begin
  CommitSelection;
  G_HasSel := True;
  G_SelFloating := False;
  G_SelRc.left := 0;
  G_SelRc.top := 0;
  G_SelRc.right := G_CanvasW;
  G_SelRc.bottom := G_CanvasH;
  G_SelW := G_CanvasW;
  G_SelH := G_CanvasH;
  G_ActiveTool := TOOL_SELECT;
  InvalidateRect(G_MainWnd, nil, False);
  RefreshCanvas;
  SetStatus('Entire canvas selected.');
end;

procedure DoUndo;
begin
  if not G_CanUndo then
  begin
    MessageBox(G_MainWnd, 'No action available to undo.', 'Undo', MB_OK or MB_ICONINFORMATION);
    Exit;
  end;

  if G_HasSel then
  begin
    if G_SelFloating and (G_hBmpSel <> 0) then
    begin
      SelectObject(G_hdcSel, G_hBmpOldSel);
      DeleteObject(G_hBmpSel);
      G_hBmpSel := 0;
    end;
    G_HasSel := False;
    G_SelFloating := False;
    G_SelDragging := False;
  end;

  BitBlt(G_hdcCanvas, 0, 0, G_CanvasW, G_CanvasH, G_hdcUndo, 0, 0, SRCCOPY);
  G_CanUndo := False;
  RefreshCanvas;
  SetStatus('Undid last action.');
end;

procedure ClearCanvas;
var
  HBr: HBRUSH;
  Rc: TRect;
begin
  if G_HasSel then
  begin
    DoDelete;
    Exit;
  end;
  SaveUndoState;
  HBr := CreateSolidBrush(G_ColSecondary);
  Rc.left := 0; Rc.top := 0; Rc.right := G_CanvasW; Rc.bottom := G_CanvasH;
  FillRect(G_hdcCanvas, Rc, HBr);
  DeleteObject(HBr);
  RefreshCanvas;
  SetStatus('Canvas cleared with background color.');
end;

procedure InvertCanvas;
begin
  SaveUndoState;
  if G_HasSel then
  begin
    if G_SelFloating and (G_hdcSel <> 0) then
      PatBlt(G_hdcSel, 0, 0, G_SelW, G_SelH, DSTINVERT)
    else
      PatBlt(G_hdcCanvas, G_SelRc.left, G_SelRc.top, G_SelW, G_SelH, DSTINVERT);
    RefreshCanvas;
    SetStatus('Selection colors inverted.');
    Exit;
  end;
  PatBlt(G_hdcCanvas, 0, 0, G_CanvasW, G_CanvasH, DSTINVERT);
  RefreshCanvas;
  SetStatus('Colors inverted.');
end;

procedure FlipCanvasH;
var
  X, Y: Integer;
  ColL, ColR: TColorRef;
begin
  SaveUndoState;
  if G_HasSel and G_SelFloating and (G_hdcSel <> 0) then
  begin
    for Y := 0 to G_SelH - 1 do
      for X := 0 to (G_SelW div 2) - 1 do
      begin
        ColL := GetPixel(G_hdcSel, X, Y);
        ColR := GetPixel(G_hdcSel, G_SelW - 1 - X, Y);
        SetPixel(G_hdcSel, X, Y, ColR);
        SetPixel(G_hdcSel, G_SelW - 1 - X, Y, ColL);
      end;
    RefreshCanvas;
    SetStatus('Selection flipped horizontally.');
    Exit;
  end;
  for Y := 0 to G_CanvasH - 1 do
    for X := 0 to (G_CanvasW div 2) - 1 do
    begin
      ColL := GetPixel(G_hdcCanvas, X, Y);
      ColR := GetPixel(G_hdcCanvas, G_CanvasW - 1 - X, Y);
      SetPixel(G_hdcCanvas, X, Y, ColR);
      SetPixel(G_hdcCanvas, G_CanvasW - 1 - X, Y, ColL);
    end;
  RefreshCanvas;
  SetStatus('Canvas flipped horizontally.');
end;

procedure FlipCanvasV;
var
  X, Y: Integer;
  ColT, ColB: TColorRef;
begin
  SaveUndoState;
  if G_HasSel and G_SelFloating and (G_hdcSel <> 0) then
  begin
    for Y := 0 to (G_SelH div 2) - 1 do
      for X := 0 to G_SelW - 1 do
      begin
        ColT := GetPixel(G_hdcSel, X, Y);
        ColB := GetPixel(G_hdcSel, X, G_SelH - 1 - Y);
        SetPixel(G_hdcSel, X, Y, ColB);
        SetPixel(G_hdcSel, X, G_SelH - 1 - Y, ColT);
      end;
    RefreshCanvas;
    SetStatus('Selection flipped vertically.');
    Exit;
  end;
  for Y := 0 to (G_CanvasH div 2) - 1 do
    for X := 0 to G_CanvasW - 1 do
    begin
      ColT := GetPixel(G_hdcCanvas, X, Y);
      ColB := GetPixel(G_hdcCanvas, X, G_CanvasH - 1 - Y);
      SetPixel(G_hdcCanvas, X, Y, ColB);
      SetPixel(G_hdcCanvas, X, G_CanvasH - 1 - Y, ColT);
    end;
  RefreshCanvas;
  SetStatus('Canvas flipped vertically.');
end;

procedure InitCanvasBuffers(Wnd: HWND);
var
  HdcScr: HDC;
  Rc: TRect;
  HBr: HBRUSH;
begin
  G_CanvasW := CANVAS_DEF_W;
  G_CanvasH := CANVAS_DEF_H;

  HdcScr := GetDC(Wnd);

  G_hdcCanvas := CreateCompatibleDC(HdcScr);
  G_hBmpCanvas := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldCanvas := SelectObject(G_hdcCanvas, G_hBmpCanvas);

  G_hdcUndo := CreateCompatibleDC(HdcScr);
  G_hBmpUndo := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldUndo := SelectObject(G_hdcUndo, G_hBmpUndo);

  G_hdcTemp := CreateCompatibleDC(HdcScr);
  G_hBmpTemp := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldTemp := SelectObject(G_hdcTemp, G_hBmpTemp);

  G_hdcSel := CreateCompatibleDC(HdcScr);
  G_hBmpOldSel := SelectObject(G_hdcSel, CreateCompatibleBitmap(HdcScr, 1, 1));

  G_hdcClip := CreateCompatibleDC(HdcScr);
  G_hBmpOldClip := SelectObject(G_hdcClip, CreateCompatibleBitmap(HdcScr, 1, 1));

  ReleaseDC(Wnd, HdcScr);

  HBr := CreateSolidBrush(RGB(255, 255, 255));
  Rc.left := 0; Rc.top := 0; Rc.right := G_CanvasW; Rc.bottom := G_CanvasH;
  FillRect(G_hdcCanvas, Rc, HBr);
  FillRect(G_hdcUndo, Rc, HBr);
  DeleteObject(HBr);

  G_CanUndo := False;
end;

procedure DestroyCanvasBuffers;
begin
  if G_hdcCanvas <> 0 then
  begin
    SelectObject(G_hdcCanvas, G_hBmpOldCanvas);
    DeleteObject(G_hBmpCanvas);
    DeleteDC(G_hdcCanvas);
  end;
  if G_hdcUndo <> 0 then
  begin
    SelectObject(G_hdcUndo, G_hBmpOldUndo);
    DeleteObject(G_hBmpUndo);
    DeleteDC(G_hdcUndo);
  end;
  if G_hdcTemp <> 0 then
  begin
    SelectObject(G_hdcTemp, G_hBmpOldTemp);
    DeleteObject(G_hBmpTemp);
    DeleteDC(G_hdcTemp);
  end;
  if G_hdcSel <> 0 then
  begin
    SelectObject(G_hdcSel, G_hBmpOldSel);
    if G_hBmpSel <> 0 then DeleteObject(G_hBmpSel);
    DeleteDC(G_hdcSel);
    G_hdcSel := 0;
  end;
  if G_hdcClip <> 0 then
  begin
    SelectObject(G_hdcClip, G_hBmpOldClip);
    if G_hBmpClip <> 0 then DeleteObject(G_hBmpClip);
    DeleteDC(G_hdcClip);
    G_hdcClip := 0;
  end;
end;

procedure ApplyDrawShape(TargetDC: HDC; Tool, Style, X1, Y1, X2, Y2: Integer; ColPen, ColFill: TColorRef; BSize: Integer);
var
  HP: HPEN;
  HBr: HBRUSH;
  OldPen: HPEN;
  OldBr: HBRUSH;
  Left, Top, Right, Bottom: Integer;
begin
  if X1 < X2 then begin Left := X1; Right := X2; end else begin Left := X2; Right := X1; end;
  if Y1 < Y2 then begin Top := Y1; Bottom := Y2; end else begin Top := Y2; Bottom := Y1; end;

  case Tool of
    TOOL_LINE:
    begin
      HP := CreatePen(PS_SOLID, BSize, ColPen);
      OldPen := SelectObject(TargetDC, HP);
      MoveTo(TargetDC, X1, Y1);
      LineTo(TargetDC, X2, Y2);
      SelectObject(TargetDC, OldPen);
      DeleteObject(HP);
    end;

    TOOL_RECT:
    begin
      case Style of
        STYLE_OUTLINE:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := GetStockObject(NULL_BRUSH);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Rectangle(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HP);
        end;
        STYLE_FILLED_BORDER:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := CreateSolidBrush(ColFill);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Rectangle(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
          DeleteObject(HP);
        end;
        STYLE_FILLED_SOLID:
        begin
          HP := GetStockObject(NULL_PEN);
          HBr := CreateSolidBrush(ColPen);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Rectangle(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
        end;
      end;
    end;

    TOOL_ROUNDRECT:
    begin
      case Style of
        STYLE_OUTLINE:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := GetStockObject(NULL_BRUSH);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          RoundRect(TargetDC, Left, Top, Right, Bottom, 16, 16);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HP);
        end;
        STYLE_FILLED_BORDER:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := CreateSolidBrush(ColFill);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          RoundRect(TargetDC, Left, Top, Right, Bottom, 16, 16);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
          DeleteObject(HP);
        end;
        STYLE_FILLED_SOLID:
        begin
          HP := GetStockObject(NULL_PEN);
          HBr := CreateSolidBrush(ColPen);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          RoundRect(TargetDC, Left, Top, Right, Bottom, 16, 16);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
        end;
      end;
    end;

    TOOL_ELLIPSE:
    begin
      case Style of
        STYLE_OUTLINE:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := GetStockObject(NULL_BRUSH);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Ellipse(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HP);
        end;
        STYLE_FILLED_BORDER:
        begin
          HP := CreatePen(PS_SOLID, BSize, ColPen);
          HBr := CreateSolidBrush(ColFill);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Ellipse(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
          DeleteObject(HP);
        end;
        STYLE_FILLED_SOLID:
        begin
          HP := GetStockObject(NULL_PEN);
          HBr := CreateSolidBrush(ColPen);
          OldPen := SelectObject(TargetDC, HP);
          OldBr  := SelectObject(TargetDC, HBr);
          Ellipse(TargetDC, Left, Top, Right, Bottom);
          SelectObject(TargetDC, OldBr);
          SelectObject(TargetDC, OldPen);
          DeleteObject(HBr);
        end;
      end;
    end;

    TOOL_SELECT:
    begin
      HP := CreatePen(PS_DOT, 1, RGB(0, 0, 0));
      HBr := GetStockObject(NULL_BRUSH);
      OldPen := SelectObject(TargetDC, HP);
      OldBr  := SelectObject(TargetDC, HBr);
      Rectangle(TargetDC, Left, Top, Right, Bottom);
      SelectObject(TargetDC, OldBr);
      SelectObject(TargetDC, OldPen);
      DeleteObject(HP);
    end;
  end;
end;

procedure PaintBrushDisc(TargetDC: HDC; X, Y, Radius: Integer; Color: TColorRef);
var
  HBr, OldBr: HBRUSH;
  HP, OldPen: HPEN;
begin
  if Radius <= 1 then
  begin
    SetPixel(TargetDC, X, Y, Color);
    Exit;
  end;
  HBr := CreateSolidBrush(Color);
  HP := CreatePen(PS_SOLID, 1, Color);
  OldBr := SelectObject(TargetDC, HBr);
  OldPen := SelectObject(TargetDC, HP);
  Ellipse(TargetDC, X - Radius, Y - Radius, X + Radius + 1, Y + Radius + 1);
  SelectObject(TargetDC, OldPen);
  SelectObject(TargetDC, OldBr);
  DeleteObject(HP);
  DeleteObject(HBr);
end;

procedure DrawContinuousBrush(TargetDC: HDC; X1, Y1, X2, Y2, Size: Integer; Color: TColorRef);
var
  Dist, I, Steps: Integer;
  DX, DY: Integer;
  CurX, CurY: Integer;
begin
  DX := X2 - X1;
  DY := Y2 - Y1;
  Dist := Abs(DX);
  if Abs(DY) > Dist then Dist := Abs(DY);
  Steps := Dist;
  if Steps < 1 then Steps := 1;

  for I := 0 to Steps do
  begin
    CurX := X1 + (DX * I) div Steps;
    CurY := Y1 + (DY * I) div Steps;
    PaintBrushDisc(TargetDC, CurX, CurY, Size div 2, Color);
  end;
end;

procedure DrawSpray(TargetDC: HDC; CenterX, CenterY, Radius: Integer; Color: TColorRef);
var
  I, R, Angle, Px, Py: Integer;
begin
  if Radius < 3 then Radius := 3;
  for I := 1 to 14 do
  begin
    R := (SimpleRand mod Radius);
    Angle := (SimpleRand mod 360);
    Px := CenterX + (R * (180 - Abs(Angle - 180))) div 120 - (R div 2);
    Py := CenterY + (R * (180 - Abs(((Angle + 90) mod 360) - 180))) div 120 - (R div 2);
    if (Px >= 0) and (Px < G_CanvasW) and (Py >= 0) and (Py < G_CanvasH) then
      SetPixel(TargetDC, Px, Py, Color);
  end;
end;

procedure DrawColorEraser(TargetDC: HDC; CenterX, CenterY, HalfSize: Integer; FindCol, ReplaceCol: TColorRef);
var
  X, Y: Integer;
begin
  for Y := CenterY - HalfSize to CenterY + HalfSize do
  begin
    if (Y >= 0) and (Y < G_CanvasH) then
    begin
      for X := CenterX - HalfSize to CenterX + HalfSize do
      begin
        if (X >= 0) and (X < G_CanvasW) then
        begin
          if GetPixel(TargetDC, X, Y) = FindCol then
            SetPixel(TargetDC, X, Y, ReplaceCol);
        end;
      end;
    end;
  end;
end;

procedure DrawNormalEraser(TargetDC: HDC; CenterX, CenterY, HalfSize: Integer; BgColor: TColorRef);
var
  Rc: TRect;
  HBr: HBRUSH;
begin
  Rc.left := CenterX - HalfSize;
  Rc.top := CenterY - HalfSize;
  Rc.right := CenterX + HalfSize + 1;
  Rc.bottom := CenterY + HalfSize + 1;
  HBr := CreateSolidBrush(BgColor);
  FillRect(TargetDC, Rc, HBr);
  DeleteObject(HBr);
end;

function SaveCanvasToBMP(APath: PChar): Boolean;
var
  FH: HFILE;
  FileHdr: TBitmapFileHeader;
  InfoHdr: TBitmapInfoHeader;
  X, Y: Integer;
  RowBytes, PadBytes: Integer;
  Color: TColorRef;
  Idx: Integer;
  PadZero: Longint;
begin
  Result := False;
  RowBytes := G_CanvasW * 3;
  PadBytes := (4 - (RowBytes mod 4)) mod 4;

  FillChar(FileHdr, SizeOf(FileHdr), 0);
  FillChar(InfoHdr, SizeOf(InfoHdr), 0);

  FileHdr.bfType := $4D42;
  FileHdr.bfOffBits := SizeOf(TBitmapFileHeader) + SizeOf(TBitmapInfoHeader);
  FileHdr.bfSize := FileHdr.bfOffBits + Longint(RowBytes + PadBytes) * Longint(G_CanvasH);

  InfoHdr.biSize := SizeOf(TBitmapInfoHeader);
  InfoHdr.biWidth := G_CanvasW;
  InfoHdr.biHeight := G_CanvasH;
  InfoHdr.biPlanes := 1;
  InfoHdr.biBitCount := 24;
  InfoHdr.biCompression := 0;
  InfoHdr.biSizeImage := Longint(RowBytes + PadBytes) * Longint(G_CanvasH);

  FH := _lcreat(APath, 0);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Unable to create BMP file for saving.', 'Save Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  _lwrite(FH, @FileHdr, SizeOf(FileHdr));
  _lwrite(FH, @InfoHdr, SizeOf(InfoHdr));

  PadZero := 0;

  for Y := G_CanvasH - 1 downto 0 do
  begin
    Idx := 0;
    for X := 0 to G_CanvasW - 1 do
    begin
      Color := GetPixel(G_hdcCanvas, X, Y);
      G_BmpRow[Idx]     := (Color shr 16) and $FF;
      G_BmpRow[Idx + 1] := (Color shr 8) and $FF;
      G_BmpRow[Idx + 2] := Color and $FF;
      Inc(Idx, 3);
    end;
    _lwrite(FH, @G_BmpRow[0], RowBytes);
    if PadBytes > 0 then
      _lwrite(FH, @PadZero, PadBytes);
  end;

  _lclose(FH);
  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'PaintCraft - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  SetStatus('File saved successfully.');
  Result := True;
end;

function LoadCanvasFromBMP(APath: PChar): Boolean;
var
  FH: HFILE;
  FileHdr: TBitmapFileHeader;
  InfoHdr: TBitmapInfoHeader;
  X, Y: Integer;
  DrawW, DrawH: Integer;
  ImgW, ImgH, Bpp: Integer;
  TopDown: Boolean;
  RowStride, BytesToRead, ExtraSkip: Longint;
  NumColors, PalBytes: Longint;
  ScreenY: Integer;
  ByteIdx, BitIdx, PalIdx: Integer;
  Color: TColorRef;
  HBr: HBRUSH;
  Rc: TRect;
begin
  Result := False;
  FH := _lopen(APath, 0);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Unable to open selected file.', 'Open Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if _lread(FH, @FileHdr, SizeOf(FileHdr)) <> SizeOf(FileHdr) then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Failed reading BMP file header.', 'Format Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if FileHdr.bfType <> $4D42 then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Selected file is not a valid BMP bitmap.', 'Format Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if _lread(FH, @InfoHdr, SizeOf(InfoHdr)) <> SizeOf(InfoHdr) then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Failed reading BMP info header.', 'Format Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if InfoHdr.biCompression <> 0 then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Compressed BMP files (RLE) are not supported.', 'File Format', MB_OK or MB_ICONINFORMATION);
    Exit;
  end;

  Bpp := InfoHdr.biBitCount;
  if (Bpp <> 1) and (Bpp <> 4) and (Bpp <> 8) and (Bpp <> 24) and (Bpp <> 32) then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Unsupported bit depth. Supported: 1, 4, 8, 24, 32 bpp.', 'File Format', MB_OK or MB_ICONINFORMATION);
    Exit;
  end;

  ImgW := InfoHdr.biWidth;
  if ImgW <= 0 then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Invalid bitmap width.', 'Format Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if InfoHdr.biHeight < 0 then
  begin
    TopDown := True;
    ImgH := -InfoHdr.biHeight;
  end
  else
  begin
    TopDown := False;
    ImgH := InfoHdr.biHeight;
  end;

  if ImgH <= 0 then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Invalid bitmap height.', 'Format Error', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  { Read palette for indexed formats (<= 8 bpp) }
  if Bpp <= 8 then
  begin
    NumColors := InfoHdr.biClrUsed;
    if NumColors = 0 then
      NumColors := Longint(1) shl Bpp;
    if NumColors > 256 then
      NumColors := 256;

    _llseek(FH, SizeOf(TBitmapFileHeader) + InfoHdr.biSize, 0);
    PalBytes := NumColors * SizeOf(TRgbQuad);
    FillChar(G_BmpPalette, SizeOf(G_BmpPalette), 0);
    _lread(FH, @G_BmpPalette[0], Word(PalBytes));
  end;

  CommitSelection;
  SaveUndoState;

  { Clear canvas to white }
  HBr := CreateSolidBrush(RGB(255, 255, 255));
  SetRect(Rc, 0, 0, G_CanvasW, G_CanvasH);
  FillRect(G_hdcCanvas, Rc, HBr);
  DeleteObject(HBr);

  { Row stride aligned to 4 bytes }
  RowStride := ((Longint(ImgW) * Longint(Bpp) + 31) div 32) * 4;
  BytesToRead := RowStride;
  if BytesToRead > SizeOf(G_BmpRow) then
  begin
    BytesToRead := SizeOf(G_BmpRow);
    ExtraSkip := RowStride - BytesToRead;
  end
  else
    ExtraSkip := 0;

  DrawW := ImgW;
  if DrawW > G_CanvasW then DrawW := G_CanvasW;
  DrawH := ImgH;
  if DrawH > G_CanvasH then DrawH := G_CanvasH;

  _llseek(FH, FileHdr.bfOffBits, 0);

  for Y := 0 to ImgH - 1 do
  begin
    if TopDown then
      ScreenY := Y
    else
      ScreenY := ImgH - 1 - Y;

    _lread(FH, @G_BmpRow[0], Word(BytesToRead));
    if ExtraSkip > 0 then
      _llseek(FH, ExtraSkip, 1);

    if ScreenY < DrawH then
    begin
      if Bpp = 1 then
      begin
        for X := 0 to DrawW - 1 do
        begin
          ByteIdx := X shr 3;
          if ByteIdx < BytesToRead then
          begin
            BitIdx := 7 - (X and 7);
            PalIdx := (G_BmpRow[ByteIdx] shr BitIdx) and 1;
            Color := RGB(G_BmpPalette[PalIdx].rgbRed,
                         G_BmpPalette[PalIdx].rgbGreen,
                         G_BmpPalette[PalIdx].rgbBlue);
            SetPixel(G_hdcCanvas, X, ScreenY, Color);
          end;
        end;
      end
      else if Bpp = 4 then
      begin
        for X := 0 to DrawW - 1 do
        begin
          ByteIdx := X shr 1;
          if ByteIdx < BytesToRead then
          begin
            if (X and 1) = 0 then
              PalIdx := (G_BmpRow[ByteIdx] shr 4) and $0F
            else
              PalIdx := G_BmpRow[ByteIdx] and $0F;
            Color := RGB(G_BmpPalette[PalIdx].rgbRed,
                         G_BmpPalette[PalIdx].rgbGreen,
                         G_BmpPalette[PalIdx].rgbBlue);
            SetPixel(G_hdcCanvas, X, ScreenY, Color);
          end;
        end;
      end
      else if Bpp = 8 then
      begin
        for X := 0 to DrawW - 1 do
        begin
          if X < BytesToRead then
          begin
            PalIdx := G_BmpRow[X];
            Color := RGB(G_BmpPalette[PalIdx].rgbRed,
                         G_BmpPalette[PalIdx].rgbGreen,
                         G_BmpPalette[PalIdx].rgbBlue);
            SetPixel(G_hdcCanvas, X, ScreenY, Color);
          end;
        end;
      end
      else if Bpp = 24 then
      begin
        ByteIdx := 0;
        for X := 0 to DrawW - 1 do
        begin
          if ByteIdx + 2 < BytesToRead then
          begin
            Color := RGB(G_BmpRow[ByteIdx + 2], G_BmpRow[ByteIdx + 1], G_BmpRow[ByteIdx]);
            SetPixel(G_hdcCanvas, X, ScreenY, Color);
          end;
          Inc(ByteIdx, 3);
        end;
      end
      else if Bpp = 32 then
      begin
        ByteIdx := 0;
        for X := 0 to DrawW - 1 do
        begin
          if ByteIdx + 2 < BytesToRead then
          begin
            Color := RGB(G_BmpRow[ByteIdx + 2], G_BmpRow[ByteIdx + 1], G_BmpRow[ByteIdx]);
            SetPixel(G_hdcCanvas, X, ScreenY, Color);
          end;
          Inc(ByteIdx, 4);
        end;
      end;
    end;
  end;

  _lclose(FH);
  RefreshCanvas;

  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'PaintCraft - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  SetStatus('Loaded BMP image.');
  Result := True;
end;

procedure DoFileOpen;
var
  OFN: TOpenFileName;
  FileBuf: array[0..128] of Char;
  FilterBuf: array[0..128] of Char;
begin
  FillChar(OFN, SizeOf(OFN), 0);
  FileBuf[0] := #0;
  FilterBuf := 'Bitmap Files (*.bmp)'#0'*.bmp'#0'All Files (*.*)'#0'*.*'#0#0;

  OFN.lStructSize := SizeOf(TOpenFileName);
  OFN.hwndOwner := G_MainWnd;
  OFN.lpstrFilter := @FilterBuf[0];
  OFN.nFilterIndex := 1;
  OFN.lpstrFile := @FileBuf[0];
  OFN.nMaxFile := SizeOf(FileBuf);
  OFN.lpstrDefExt := 'bmp';
  OFN.Flags := $00001000 or $00000800 or $00000004;

  if GetOpenFileName(OFN) then
  begin
    if StrLen(FileBuf) > 0 then
      LoadCanvasFromBMP(FileBuf);
  end;
end;

procedure DoFileSaveAs;
var
  OFN: TOpenFileName;
  FileBuf: array[0..128] of Char;
  FilterBuf: array[0..128] of Char;
begin
  FillChar(OFN, SizeOf(OFN), 0);
  StrCopy(FileBuf, G_CurFile);
  FilterBuf := 'Bitmap Files (*.bmp)'#0'*.bmp'#0'All Files (*.*)'#0'*.*'#0#0;

  OFN.lStructSize := SizeOf(TOpenFileName);
  OFN.hwndOwner := G_MainWnd;
  OFN.lpstrFilter := @FilterBuf[0];
  OFN.nFilterIndex := 1;
  OFN.lpstrFile := @FileBuf[0];
  OFN.nMaxFile := SizeOf(FileBuf);
  OFN.lpstrDefExt := 'bmp';
  OFN.Flags := $00000002 or $00000800 or $00000004;

  if GetSaveFileName(OFN) then
  begin
    if StrLen(FileBuf) > 0 then
      SaveCanvasToBMP(FileBuf);
  end;
end;

procedure DoFileSave;
begin
  if StrLen(G_CurFile) > 0 then
    SaveCanvasToBMP(G_CurFile)
  else
    DoFileSaveAs;
end;

procedure DrawModernUI(DC: HDC);
var
  I, X, Y, W: Integer;
  R: TRect;
  HBr, OldBr: HBRUSH;
  HP, OldPen: HPEN;
  ClientRc: TRect;
  TextBuf: array[0..64] of Char;
  HasSize: Boolean;
begin
  GetClientRect(G_MainWnd, ClientRc);

  { Neutral background for window }
  R.left := 0; R.top := 0; R.right := ClientRc.right; R.bottom := ClientRc.bottom;
  HBr := COLOR_BTNFACE + 1;
  FillRect(DC, R, HBr);

  { Control panel background }
  R.left := 0; R.top := 0; R.right := ClientRc.right; R.bottom := CANVAS_Y - 4;
  HBr := CreateSolidBrush(RGB(236, 238, 241));
  FillRect(DC, R, HBr);
  DeleteObject(HBr);

  HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
  OldPen := SelectObject(DC, HP);
  MoveTo(DC, 0, CANVAS_Y - 5);
  LineTo(DC, ClientRc.right, CANVAS_Y - 5);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  SetBkMode(DC, TRANSPARENT);
  for I := 1 to TOOL_COUNT do
  begin
    R := G_Tools[I].R;
    if G_ActiveTool = I then
    begin
      HBr := CreateSolidBrush(RGB(196, 220, 250));
      FillRect(DC, R, HBr);
      DeleteObject(HBr);
      HP := CreatePen(PS_SOLID, 1, RGB(32, 96, 200));
      OldPen := SelectObject(DC, HP);
      HBr := GetStockObject(NULL_BRUSH);
      OldBr := SelectObject(DC, HBr);
      Rectangle(DC, R.left, R.top, R.right, R.bottom);
      SelectObject(DC, OldBr);
      SelectObject(DC, OldPen);
      DeleteObject(HP);
      SetTextColor(DC, RGB(16, 48, 128));
    end
    else
    begin
      HBr := CreateSolidBrush(RGB(248, 249, 250));
      FillRect(DC, R, HBr);
      DeleteObject(HBr);
      HP := CreatePen(PS_SOLID, 1, RGB(190, 195, 205));
      OldPen := SelectObject(DC, HP);
      HBr := GetStockObject(NULL_BRUSH);
      OldBr := SelectObject(DC, HBr);
      Rectangle(DC, R.left, R.top, R.right, R.bottom);
      SelectObject(DC, OldBr);
      SelectObject(DC, OldPen);
      DeleteObject(HP);
      SetTextColor(DC, RGB(40, 44, 52));
    end;
    DrawText(DC, G_Tools[I].Name, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  HasSize := ToolSupportsSize(G_ActiveTool);
  if HasSize then
    SetTextColor(DC, RGB(60, 64, 72))
  else
    SetTextColor(DC, RGB(150, 155, 165));
  TextOut(DC, 8, 28, 'Size:', 5);
  for I := 1 to 5 do
  begin
    X := 52 + (I - 1) * 26;
    Y := 26;
    R.left := X; R.top := Y; R.right := X + 22; R.bottom := Y + 18;
    case I of
      1: W := 1;
      2: W := 2;
      3: W := 4;
      4: W := 8;
      5: W := 12;
      else W := 1;
    end;
    if HasSize then
    begin
      if G_BrushSize = W then
      begin
        HBr := CreateSolidBrush(RGB(196, 220, 250));
        SetTextColor(DC, RGB(16, 48, 128));
      end
      else
      begin
        HBr := CreateSolidBrush(RGB(255, 255, 255));
        SetTextColor(DC, RGB(40, 44, 52));
      end;
      HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
    end
    else
    begin
      if (G_ActiveTool = TOOL_PENCIL) and (W = 1) then
      begin
        HBr := CreateSolidBrush(RGB(226, 230, 238));
        HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
        SetTextColor(DC, RGB(100, 110, 130));
      end
      else
      begin
        HBr := CreateSolidBrush(RGB(240, 241, 243));
        HP := CreatePen(PS_SOLID, 1, RGB(215, 218, 224));
        SetTextColor(DC, RGB(170, 175, 185));
      end;
    end;
    FillRect(DC, R, HBr);
    DeleteObject(HBr);

    OldPen := SelectObject(DC, HP);
    HBr := GetStockObject(NULL_BRUSH);
    OldBr := SelectObject(DC, HBr);
    Rectangle(DC, R.left, R.top, R.right, R.bottom);
    SelectObject(DC, OldBr);
    SelectObject(DC, OldPen);
    DeleteObject(HP);

    case I of
      1: StrCopy(TextBuf, '1');
      2: StrCopy(TextBuf, '2');
      3: StrCopy(TextBuf, '4');
      4: StrCopy(TextBuf, '8');
      5: StrCopy(TextBuf, '12');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  TextOut(DC, 194, 28, 'Style:', 6);
  for I := 1 to 3 do
  begin
    X := 240 + (I - 1) * 44;
    Y := 26;
    R.left := X; R.top := Y; R.right := X + 40; R.bottom := Y + 18;
    if G_ShapeStyle = I then
      HBr := CreateSolidBrush(RGB(196, 220, 250))
    else
      HBr := CreateSolidBrush(RGB(255, 255, 255));
    FillRect(DC, R, HBr);
    DeleteObject(HBr);

    HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
    OldPen := SelectObject(DC, HP);
    HBr := GetStockObject(NULL_BRUSH);
    OldBr := SelectObject(DC, HBr);
    Rectangle(DC, R.left, R.top, R.right, R.bottom);
    SelectObject(DC, OldBr);
    SelectObject(DC, OldPen);
    DeleteObject(HP);

    case I of
      1: StrCopy(TextBuf, 'Frame');
      2: StrCopy(TextBuf, 'Fill+');
      3: StrCopy(TextBuf, 'Solid');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  for I := 1 to 5 do
  begin
    X := 384 + (I - 1) * 44;
    Y := 26;
    R.left := X; R.top := Y; R.right := X + 41; R.bottom := Y + 18;
    HBr := CreateSolidBrush(RGB(244, 245, 247));
    FillRect(DC, R, HBr);
    DeleteObject(HBr);

    HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
    OldPen := SelectObject(DC, HP);
    HBr := GetStockObject(NULL_BRUSH);
    OldBr := SelectObject(DC, HBr);
    Rectangle(DC, R.left, R.top, R.right, R.bottom);
    SelectObject(DC, OldBr);
    SelectObject(DC, OldPen);
    DeleteObject(HP);

    case I of
      1: StrCopy(TextBuf, 'Undo');
      2: StrCopy(TextBuf, 'Clear');
      3: StrCopy(TextBuf, 'Invert');
      4: StrCopy(TextBuf, 'FlipX');
      5: StrCopy(TextBuf, 'FlipY');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  R.left := 22; R.top := 58; R.right := 48; R.bottom := 80;
  HBr := CreateSolidBrush(G_ColSecondary);
  FillRect(DC, R, HBr);
  DeleteObject(HBr);
  HP := CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
  OldPen := SelectObject(DC, HP);
  HBr := GetStockObject(NULL_BRUSH);
  OldBr := SelectObject(DC, HBr);
  Rectangle(DC, R.left, R.top, R.right, R.bottom);

  R.left := 10; R.top := 50; R.right := 36; R.bottom := 72;
  HBr := CreateSolidBrush(G_ColPrimary);
  FillRect(DC, R, HBr);
  DeleteObject(HBr);
  Rectangle(DC, R.left, R.top, R.right, R.bottom);
  SelectObject(DC, OldBr);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  for I := 0 to 27 do
  begin
    if I < 14 then
    begin
      X := 56 + I * 19;
      Y := 50;
    end
    else
    begin
      X := 56 + (I - 14) * 19;
      Y := 66;
    end;
    R.left := X; R.top := Y; R.right := X + 16; R.bottom := Y + 13;
    HBr := CreateSolidBrush(G_Palette[I]);
    FillRect(DC, R, HBr);
    DeleteObject(HBr);

    HP := CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
    OldPen := SelectObject(DC, HP);
    HBr := GetStockObject(NULL_BRUSH);
    OldBr := SelectObject(DC, HBr);
    Rectangle(DC, R.left, R.top, R.right, R.bottom);
    SelectObject(DC, OldBr);
    SelectObject(DC, OldPen);
    DeleteObject(HP);
  end;

  HP := CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
  OldPen := SelectObject(DC, HP);
  HBr := GetStockObject(NULL_BRUSH);
  OldBr := SelectObject(DC, HBr);
  Rectangle(DC, CANVAS_X - 1, CANVAS_Y - 1, CANVAS_X + G_CanvasW + 1, CANVAS_Y + G_CanvasH + 1);
  SelectObject(DC, OldBr);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  HP := CreatePen(PS_SOLID, 1, RGB(200, 205, 212));
  OldPen := SelectObject(DC, HP);
  MoveTo(DC, CANVAS_X + G_CanvasW + 1, CANVAS_Y);
  LineTo(DC, CANVAS_X + G_CanvasW + 1, CANVAS_Y + G_CanvasH + 2);
  LineTo(DC, CANVAS_X, CANVAS_Y + G_CanvasH + 1);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  R.left := 0; R.top := ClientRc.bottom - 22; R.right := ClientRc.right; R.bottom := ClientRc.bottom;
  HBr := CreateSolidBrush(RGB(240, 242, 245));
  FillRect(DC, R, HBr);
  DeleteObject(HBr);

  HP := CreatePen(PS_SOLID, 1, RGB(190, 195, 205));
  OldPen := SelectObject(DC, HP);
  MoveTo(DC, 0, R.top);
  LineTo(DC, ClientRc.right, R.top);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  SetBkMode(DC, TRANSPARENT);
  SetTextColor(DC, RGB(40, 44, 52));
  TextOut(DC, 8, R.top + 3, G_StatusText, StrLen(G_StatusText));
end;

function WndProc(Wnd: HWND; Msg: UINT; WParam: WPARAM; LParam: LPARAM): LRESULT; export;
var
  PS: PAINTSTRUCT;
  DC: HDC;
  X, Y: Integer;
  I, SwatchIdx: Integer;
  StatusCoord: array[0..80] of Char;
  CoordBuf: array[0..16] of Char;
  TargetCol: TColorRef;
  FillBr, OldFillBr: HBRUSH;
  Pt: TPoint;
  Left, Top, Right, Bottom: Integer;
  HdcScr: HDC;
begin
  case Msg of
    WM_CREATE:
    begin
      InitCanvasBuffers(Wnd);
      G_ActiveTool   := TOOL_PENCIL;
      G_BrushSize    := 2;
      G_ShapeStyle   := STYLE_OUTLINE;
      G_ColPrimary   := RGB(0, 0, 0);
      G_ColSecondary := RGB(255, 255, 255);
      G_IsDrawing    := False;
      G_IsDragging   := False;
      G_PolyCount    := 0;
      G_RandSeed     := 12345;
      G_CurFile[0]   := #0;

      StrCopy(G_StatusText, 'PaintCraft - Ready. LMB: Primary Color, RMB: Background Color.');
      Result := 0;
    end;

    WM_PAINT:
    begin
      DC := BeginPaint(Wnd, PS);
      DrawModernUI(DC);
      RenderCanvasToDC(DC);
      EndPaint(Wnd, PS);
      Result := 0;
    end;

    WM_SIZE:
    begin
      InvalidateRect(Wnd, nil, True);
      Result := 0;
    end;

    WM_ERASEBKGND:
    begin
      Result := 1;
    end;

    WM_KEYDOWN:
    begin
      if WParam = $2E { VK_DELETE } then
      begin
        DoDelete;
        Result := 0;
        Exit;
      end;
      if GetKeyState($11 { VK_CONTROL }) < 0 then
      begin
        case WParam of
          Ord('Z'), Ord('z'): begin DoUndo; Result := 0; Exit; end;
          Ord('X'), Ord('x'): begin DoCut; Result := 0; Exit; end;
          Ord('C'), Ord('c'): begin DoCopy; Result := 0; Exit; end;
          Ord('V'), Ord('v'): begin DoPaste; Result := 0; Exit; end;
          Ord('A'), Ord('a'): begin DoSelectAll; Result := 0; Exit; end;
          Ord('N'), Ord('n'): begin CommitSelection; ClearCanvas; Result := 0; Exit; end;
          Ord('O'), Ord('o'): begin CommitSelection; DoFileOpen; Result := 0; Exit; end;
          Ord('S'), Ord('s'): begin CommitSelection; DoFileSave; Result := 0; Exit; end;
        end;
      end;
    end;

    WM_COMMAND:
    begin
      case WParam of
        IDM_NEW:
        begin
          CommitSelection;
          ClearCanvas;
        end;
        IDM_OPEN:
        begin
          CommitSelection;
          DoFileOpen;
        end;
        IDM_SAVE:
        begin
          CommitSelection;
          DoFileSave;
        end;
        IDM_SAVEAS:
        begin
          CommitSelection;
          DoFileSaveAs;
        end;
        IDM_UNDO:
          DoUndo;
        IDM_CUT:
          DoCut;
        IDM_COPY:
          DoCopy;
        IDM_PASTE:
          DoPaste;
        IDM_DELETE:
          DoDelete;
        IDM_SELECTALL:
          DoSelectAll;
        IDM_CLEAR:
          ClearCanvas;
        IDM_INVERT:
          InvertCanvas;
        IDM_FLIPH:
          FlipCanvasH;
        IDM_FLIPV:
          FlipCanvasV;
        IDM_EXIT:
          SendMessage(Wnd, WM_CLOSE, 0, 0);
        IDM_ABOUT:
          MessageBox(
            Wnd,
            'PaintCraft Studio for Windows & DOS'#13#10 +
            'A modern pixel art & graphics editor.'#13#10#13#10 +
            'Features:'#13#10 +
            '- Double-buffered GDI canvas (flicker-free)'#13#10 +
            '- Real-time interactive shape preview'#13#10 +
            '- 14 drawing tools & 28-color palette'#13#10 +
            '- Direct 24-bit BMP loading and saving'#13#10 +
            '- Multi-level Undo & canvas transformations'#13#10#13#10 +
            'Win16Loader Project',
            'About PaintCraft',
            MB_OK or $0040
          );
      end;
      Result := 0;
    end;

    WM_LBUTTONDOWN:
    begin
      X := LOWORD(LParam);
      Y := HIWORD(LParam);

      for I := 1 to TOOL_COUNT do
      begin
        if (X >= G_Tools[I].R.left) and (X <= G_Tools[I].R.right) and
           (Y >= G_Tools[I].R.top) and (Y <= G_Tools[I].R.bottom) then
        begin
          if I <> TOOL_SELECT then
            CommitSelection;
          G_ActiveTool := I;
          G_PolyCount := 0;
          InvalidateRect(Wnd, nil, False);
          SetStatus('Active tool changed.');
          Result := 0;
          Exit;
        end;
      end;

      if (Y >= 26) and (Y <= 44) and (X >= 52) and (X <= 182) then
      begin
        if not ToolSupportsSize(G_ActiveTool) then
        begin
          if G_ActiveTool = TOOL_PENCIL then
            SetStatus('Pencil is fixed at 1 px (hairline).')
          else
            SetStatus('Size option is not available for this tool.');
          Result := 0;
          Exit;
        end;
        I := ((X - 52) div 26) + 1;
        case I of
          1: G_BrushSize := 1;
          2: G_BrushSize := 2;
          3: G_BrushSize := 4;
          4: G_BrushSize := 8;
          5: G_BrushSize := 12;
        end;
        InvalidateRect(Wnd, nil, False);
        SetStatus('Brush size changed.');
        Result := 0;
        Exit;
      end;

      if (Y >= 26) and (Y <= 44) and (X >= 240) and (X <= 372) then
      begin
        I := ((X - 240) div 44) + 1;
        if (I >= 1) and (I <= 3) then
          G_ShapeStyle := I;
        InvalidateRect(Wnd, nil, False);
        SetStatus('Shape style changed.');
        Result := 0;
        Exit;
      end;

      if (Y >= 26) and (Y <= 44) and (X >= 384) and (X <= 604) then
      begin
        I := ((X - 384) div 44) + 1;
        case I of
          1: DoUndo;
          2: ClearCanvas;
          3: InvertCanvas;
          4: FlipCanvasH;
          5: FlipCanvasV;
        end;
        Result := 0;
        Exit;
      end;

      if (Y >= 50) and (Y <= 80) and (X >= 56) and (X <= 56 + 14 * 19) then
      begin
        SwatchIdx := (X - 56) div 19;
        if (SwatchIdx >= 0) and (SwatchIdx < 14) then
        begin
          if Y >= 66 then
            SwatchIdx := SwatchIdx + 14;
          G_ColPrimary := G_Palette[SwatchIdx];
          InvalidateRect(Wnd, nil, False);
          SetStatus('Primary color selected (LMB).');
          Result := 0;
          Exit;
        end;
      end;

      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        X := X - CANVAS_X;
        Y := Y - CANVAS_Y;

        if G_ActiveTool = TOOL_SELECT then
        begin
          Pt.x := X; Pt.y := Y;
          if G_HasSel and PtInRect(G_SelRc, Pt) then
          begin
            SetCapture(Wnd);
            if not G_SelFloating then
            begin
              SaveUndoState;
              HdcScr := GetDC(Wnd);
              if G_hBmpSel <> 0 then
              begin
                SelectObject(G_hdcSel, G_hBmpOldSel);
                DeleteObject(G_hBmpSel);
              end;
              G_hBmpSel := CreateCompatibleBitmap(HdcScr, G_SelW, G_SelH);
              ReleaseDC(Wnd, HdcScr);
              SelectObject(G_hdcSel, G_hBmpSel);
              BitBlt(G_hdcSel, 0, 0, G_SelW, G_SelH, G_hdcCanvas, G_SelRc.left, G_SelRc.top, SRCCOPY);

              if not (GetKeyState($11 { VK_CONTROL }) < 0) then
              begin
                FillBr := CreateSolidBrush(G_ColSecondary);
                FillRect(G_hdcCanvas, G_SelRc, FillBr);
                DeleteObject(FillBr);
              end;
              G_SelFloating := True;
            end;
            G_SelDragging := True;
            G_SelAnchorX := X - G_SelRc.left;
            G_SelAnchorY := Y - G_SelRc.top;
            SetStatus('Moving selection. Release to place.');
            Result := 0;
            Exit;
          end
          else
          begin
            CommitSelection;
            SetCapture(Wnd);
            G_IsDragging := True;
            G_StartX := X; G_CurX := X;
            G_StartY := Y; G_CurY := Y;
            SetStatus('Select: Drag cursor to define region.');
            Result := 0;
            Exit;
          end;
        end
        else
          CommitSelection;

        SetCapture(Wnd);

        case G_ActiveTool of
          TOOL_PENCIL, TOOL_BRUSH, TOOL_AIRBRUSH, TOOL_ERASER, TOOL_COLOR_ERASER:
          begin
            SaveUndoState;
            G_IsDrawing := True;
            G_StartX := X; G_StartY := Y;
            G_LastX  := X; G_LastY  := Y;

            DC := GetDC(Wnd);
            case G_ActiveTool of
              TOOL_PENCIL:
              begin
                SetPixel(G_hdcCanvas, X, Y, G_ColPrimary);
                BitBlt(DC, CANVAS_X + X, CANVAS_Y + Y, 1, 1, G_hdcCanvas, X, Y, SRCCOPY);
              end;
              TOOL_BRUSH:
              begin
                PaintBrushDisc(G_hdcCanvas, X, Y, G_BrushSize div 2, G_ColPrimary);
                BitBlt(DC, CANVAS_X + X - G_BrushSize, CANVAS_Y + Y - G_BrushSize,
                       G_BrushSize * 2 + 1, G_BrushSize * 2 + 1,
                       G_hdcCanvas, X - G_BrushSize, Y - G_BrushSize, SRCCOPY);
              end;
              TOOL_AIRBRUSH:
              begin
                DrawSpray(G_hdcCanvas, X, Y, G_BrushSize * 3 + 4, G_ColPrimary);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
              end;
              TOOL_ERASER:
              begin
                DrawNormalEraser(G_hdcCanvas, X, Y, G_BrushSize * 2, G_ColSecondary);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
              end;
              TOOL_COLOR_ERASER:
              begin
                DrawColorEraser(G_hdcCanvas, X, Y, G_BrushSize * 2, G_ColPrimary, G_ColSecondary);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
              end;
            end;
            ReleaseDC(Wnd, DC);
          end;

          TOOL_FILL:
          begin
            TargetCol := GetPixel(G_hdcCanvas, X, Y);
            if TargetCol <> G_ColPrimary then
            begin
              SaveUndoState;
              FillBr := CreateSolidBrush(G_ColPrimary);
              OldFillBr := SelectObject(G_hdcCanvas, FillBr);
              ExtFloodFill(G_hdcCanvas, X, Y, TargetCol, FLOODFILLSURFACE);
              SelectObject(G_hdcCanvas, OldFillBr);
              DeleteObject(FillBr);
              DC := GetDC(Wnd);
              BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
              ReleaseDC(Wnd, DC);
              SetStatus('Surface filled with primary color.');
            end;
          end;

          TOOL_EYEDROPPER:
          begin
            G_ColPrimary := GetPixel(G_hdcCanvas, X, Y);
            InvalidateRect(Wnd, nil, False);
            SetStatus('Primary color picked from canvas.');
          end;

          TOOL_TEXT:
          begin
            SaveUndoState;
            SetBkMode(G_hdcCanvas, TRANSPARENT);
            SetTextColor(G_hdcCanvas, G_ColPrimary);
            TextOut(G_hdcCanvas, X, Y, 'PaintCraft', 10);
            DC := GetDC(Wnd);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
            ReleaseDC(Wnd, DC);
            SetStatus('Text stamped onto canvas.');
          end;

          TOOL_POLYGON:
          begin
            if G_PolyCount = 0 then
            begin
              SaveUndoState;
              G_PolyPoints[0].x := X;
              G_PolyPoints[0].y := Y;
              G_PolyCount := 1;
              G_LastX := X; G_LastY := Y;
              SetStatus('Polygon: Click points. Double-click to close.');
            end
            else if G_PolyCount < 30 then
            begin
              if (Abs(X - G_PolyPoints[0].x) < 6) and (Abs(Y - G_PolyPoints[0].y) < 6) and (G_PolyCount >= 2) then
              begin
                Polygon(G_hdcCanvas, G_PolyPoints, G_PolyCount);
                G_PolyCount := 0;
                DC := GetDC(Wnd);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
                ReleaseDC(Wnd, DC);
                SetStatus('Polygon closed and filled.');
              end
              else
              begin
                G_PolyPoints[G_PolyCount].x := X;
                G_PolyPoints[G_PolyCount].y := Y;
                Inc(G_PolyCount);
                ApplyDrawShape(G_hdcCanvas, TOOL_LINE, 0, G_LastX, G_LastY, X, Y, G_ColPrimary, 0, G_BrushSize);
                G_LastX := X; G_LastY := Y;
                DC := GetDC(Wnd);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
                ReleaseDC(Wnd, DC);
              end;
            end;
          end;

          TOOL_LINE, TOOL_RECT, TOOL_ROUNDRECT, TOOL_ELLIPSE:
          begin
            SaveUndoState;
            G_IsDragging := True;
            G_StartX := X; G_StartY := Y;
            G_CurX   := X; G_CurY   := Y;
          end;
        end;
      end;
      Result := 0;
    end;

    WM_RBUTTONDOWN:
    begin
      X := LOWORD(LParam);
      Y := HIWORD(LParam);

      if (Y >= 50) and (Y <= 80) and (X >= 56) and (X <= 56 + 14 * 19) then
      begin
        SwatchIdx := (X - 56) div 19;
        if (SwatchIdx >= 0) and (SwatchIdx < 14) then
        begin
          if Y >= 66 then
            SwatchIdx := SwatchIdx + 14;
          G_ColSecondary := G_Palette[SwatchIdx];
          InvalidateRect(Wnd, nil, False);
          SetStatus('Background color selected (RMB).');
          Result := 0;
          Exit;
        end;
      end;

      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        X := X - CANVAS_X;
        Y := Y - CANVAS_Y;
        if G_ActiveTool = TOOL_EYEDROPPER then
        begin
          G_ColSecondary := GetPixel(G_hdcCanvas, X, Y);
          InvalidateRect(Wnd, nil, False);
          SetStatus('Background color picked from canvas.');
          Result := 0;
          Exit;
        end
        else if G_ActiveTool = TOOL_FILL then
        begin
          TargetCol := GetPixel(G_hdcCanvas, X, Y);
          if TargetCol <> G_ColSecondary then
          begin
            SaveUndoState;
            FillBr := CreateSolidBrush(G_ColSecondary);
            OldFillBr := SelectObject(G_hdcCanvas, FillBr);
            ExtFloodFill(G_hdcCanvas, X, Y, TargetCol, FLOODFILLSURFACE);
            SelectObject(G_hdcCanvas, OldFillBr);
            DeleteObject(FillBr);
            DC := GetDC(Wnd);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
            ReleaseDC(Wnd, DC);
            SetStatus('Surface filled with background color (RMB).');
          end;
          Result := 0;
          Exit;
        end;
      end;
      Result := 0;
    end;

    WM_MOUSEMOVE:
    begin
      X := LOWORD(LParam);
      Y := HIWORD(LParam);

      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        StrCopy(StatusCoord, 'Cursor: X=');
        IntToStrBuf(X - CANVAS_X, CoordBuf);
        StrCat(StatusCoord, CoordBuf);
        StrCat(StatusCoord, ' Y=');
        IntToStrBuf(Y - CANVAS_Y, CoordBuf);
        StrCat(StatusCoord, CoordBuf);
        StrCat(StatusCoord, ' px  |  Canvas: 580 x 310');
        SetStatus(StatusCoord);
      end;

      if G_SelDragging then
      begin
        G_SelRc.left := (X - CANVAS_X) - G_SelAnchorX;
        G_SelRc.top  := (Y - CANVAS_Y) - G_SelAnchorY;
        G_SelRc.right := G_SelRc.left + G_SelW;
        G_SelRc.bottom := G_SelRc.top + G_SelH;
        RefreshCanvas;
        Result := 0;
        Exit;
      end;

      if G_IsDrawing and (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        X := X - CANVAS_X;
        Y := Y - CANVAS_Y;
        DC := GetDC(Wnd);

        case G_ActiveTool of
          TOOL_PENCIL:
          begin
            ApplyDrawShape(G_hdcCanvas, TOOL_LINE, 0, G_LastX, G_LastY, X, Y, G_ColPrimary, 0, 1);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
          end;
          TOOL_BRUSH:
          begin
            DrawContinuousBrush(G_hdcCanvas, G_LastX, G_LastY, X, Y, G_BrushSize, G_ColPrimary);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
          end;
          TOOL_AIRBRUSH:
          begin
            DrawSpray(G_hdcCanvas, X, Y, G_BrushSize * 3 + 4, G_ColPrimary);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
          end;
          TOOL_ERASER:
          begin
            DrawNormalEraser(G_hdcCanvas, X, Y, G_BrushSize * 2, G_ColSecondary);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
          end;
          TOOL_COLOR_ERASER:
          begin
            DrawColorEraser(G_hdcCanvas, X, Y, G_BrushSize * 2, G_ColPrimary, G_ColSecondary);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
          end;
        end;

        G_LastX := X;
        G_LastY := Y;
        ReleaseDC(Wnd, DC);
      end;

      if G_IsDragging then
      begin
        X := X - CANVAS_X;
        Y := Y - CANVAS_Y;
        if X < 0 then X := 0;
        if X >= G_CanvasW then X := G_CanvasW - 1;
        if Y < 0 then Y := 0;
        if Y >= G_CanvasH then Y := G_CanvasH - 1;
        G_CurX := X;
        G_CurY := Y;

        BitBlt(G_hdcTemp, 0, 0, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
        ApplyDrawShape(G_hdcTemp, G_ActiveTool, G_ShapeStyle,
                       G_StartX, G_StartY, G_CurX, G_CurY,
                       G_ColPrimary, G_ColSecondary, G_BrushSize);

        DC := GetDC(Wnd);
        BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcTemp, 0, 0, SRCCOPY);
        ReleaseDC(Wnd, DC);
      end;

      Result := 0;
    end;

    WM_LBUTTONUP:
    begin
      ReleaseCapture;
      if G_IsDrawing then
      begin
        G_IsDrawing := False;
      end;

      if G_SelDragging then
      begin
        G_SelDragging := False;
        RefreshCanvas;
        SetStatus('Selection moved. Drag to move again, or click outside to stamp.');
        Result := 0;
        Exit;
      end;

      if G_IsDragging then
      begin
        G_IsDragging := False;
        if G_ActiveTool = TOOL_SELECT then
        begin
          if G_StartX < G_CurX then begin Left := G_StartX; Right := G_CurX; end else begin Left := G_CurX; Right := G_StartX; end;
          if G_StartY < G_CurY then begin Top := G_StartY; Bottom := G_CurY; end else begin Top := G_CurY; Bottom := G_StartY; end;
          if (Right - Left > 2) and (Bottom - Top > 2) then
          begin
            G_HasSel := True;
            G_SelFloating := False;
            G_SelRc.left := Left;
            G_SelRc.top := Top;
            G_SelRc.right := Right;
            G_SelRc.bottom := Bottom;
            G_SelW := Right - Left;
            G_SelH := Bottom - Top;
            SetStatus('Area selected. Drag inside to move, Ctrl+C to copy, Del to clear.');
          end
          else
          begin
            G_HasSel := False;
            SetStatus('Ready.');
          end;
          RefreshCanvas;
        end
        else
        begin
          ApplyDrawShape(G_hdcCanvas, G_ActiveTool, G_ShapeStyle,
                         G_StartX, G_StartY, G_CurX, G_CurY,
                         G_ColPrimary, G_ColSecondary, G_BrushSize);
          RefreshCanvas;
          SetStatus('Shape drawn.');
        end;
      end;
      Result := 0;
    end;

    WM_LBUTTONDBLCLK:
    begin
      if (G_ActiveTool = TOOL_POLYGON) and (G_PolyCount >= 2) then
      begin
        Polygon(G_hdcCanvas, G_PolyPoints, G_PolyCount);
        G_PolyCount := 0;
        DC := GetDC(Wnd);
        BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
        ReleaseDC(Wnd, DC);
        SetStatus('Polygon completed.');
      end;
      Result := 0;
    end;

    WM_CLOSE:
    begin
      DestroyWindow(Wnd);
      Result := 0;
    end;

    WM_DESTROY:
    begin
      DestroyCanvasBuffers;
      PostQuitMessage(0);
      Result := 0;
    end;

    else
      Result := DefWindowProc(Wnd, Msg, WParam, LParam);
  end;
end;

procedure CreateMainMenu(Wnd: HWND);
var
  HMenu, HSubFile, HSubEdit, HSubHelp: WinTypes.HMENU;
begin
  HMenu := CreateMenu;

  HSubFile := CreatePopupMenu;
  AppendMenu(HSubFile, MF_STRING, IDM_NEW,    '&New'#9'Ctrl+N');
  AppendMenu(HSubFile, MF_STRING, IDM_OPEN,   '&Open...'#9'Ctrl+O');
  AppendMenu(HSubFile, MF_STRING, IDM_SAVE,   '&Save'#9'Ctrl+S');
  AppendMenu(HSubFile, MF_STRING, IDM_SAVEAS, 'Save &As...');
  AppendMenu(HSubFile, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubFile, MF_STRING, IDM_EXIT,   'E&xit'#9'Alt+F4');
  AppendMenu(HMenu, MF_POPUP, HSubFile, '&File');

  HSubEdit := CreatePopupMenu;
  AppendMenu(HSubEdit, MF_STRING, IDM_UNDO,      '&Undo'#9'Ctrl+Z');
  AppendMenu(HSubEdit, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubEdit, MF_STRING, IDM_CUT,       'Cu&t'#9'Ctrl+X');
  AppendMenu(HSubEdit, MF_STRING, IDM_COPY,      '&Copy'#9'Ctrl+C');
  AppendMenu(HSubEdit, MF_STRING, IDM_PASTE,     '&Paste'#9'Ctrl+V');
  AppendMenu(HSubEdit, MF_STRING, IDM_DELETE,    '&Delete'#9'Del');
  AppendMenu(HSubEdit, MF_STRING, IDM_SELECTALL, 'Select &All'#9'Ctrl+A');
  AppendMenu(HSubEdit, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubEdit, MF_STRING, IDM_CLEAR,     '&Clear Canvas');
  AppendMenu(HSubEdit, MF_STRING, IDM_INVERT,    '&Invert Colors');
  AppendMenu(HSubEdit, MF_STRING, IDM_FLIPH,     'Flip &Horizontal');
  AppendMenu(HSubEdit, MF_STRING, IDM_FLIPV,     'Flip &Vertical');
  AppendMenu(HMenu, MF_POPUP, HSubEdit, '&Edit');

  HSubHelp := CreatePopupMenu;
  AppendMenu(HSubHelp, MF_STRING, IDM_ABOUT,  '&About PaintCraft...');
  AppendMenu(HMenu, MF_POPUP, HSubHelp, '&Help');

  SetMenu(Wnd, HMenu);
end;

var
  WndClass, DummyClass: TWndClass;
  Msg: TMsg;
  CurTask: Word;
begin
  G_Instance := HInstance;
  if G_Instance = 0 then
  begin
    CurTask := GetCurrentTask;
    if CurTask <> 0 then
      G_Instance := PWord(Ptr(CurTask, $1C))^;
  end;

  InitPalette;
  InitButtons;

  FillChar(WndClass, SizeOf(WndClass), 0);
  WndClass.style := CS_HREDRAW or CS_VREDRAW or CS_DBLCLKS;
  WndClass.lpfnWndProc := @WndProc;
  WndClass.hInstance := G_Instance;
  WndClass.hIcon := LoadIcon(G_Instance, MakeIntResource(1));
  if WndClass.hIcon = 0 then
    WndClass.hIcon := LoadIcon(0, IDI_APPLICATION);
  WndClass.hCursor := LoadCursor(0, IDC_ARROW);
  WndClass.hbrBackground := COLOR_BTNFACE + 1;
  WndClass.lpszClassName := 'W16PaintCraftClass';

  if not RegisterClass(WndClass) then
  begin
    FillChar(DummyClass, SizeOf(DummyClass), 0);
    if not GetClassInfo(G_Instance, 'W16PaintCraftClass', DummyClass) and
       not GetClassInfo(0, 'W16PaintCraftClass', DummyClass) then
    begin
      Dbg('PAINT: RegisterClass FAILED!');
      Halt(1);
    end;
  end;

  G_MainWnd := CreateWindow(
    'W16PaintCraftClass',
    'PaintCraft - [Untitled]',
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT,
    624, 468,
    0, 0, G_Instance, nil
  );

  if G_MainWnd = 0 then
  begin
    Dbg('PAINT: CreateWindow FAILED!');
    Halt(1);
  end;

  CreateMainMenu(G_MainWnd);
  ShowWindow(G_MainWnd, SW_SHOW);
  UpdateWindow(G_MainWnd);

  while GetMessage(Msg, 0, 0, 0) do
  begin
    TranslateMessage(Msg);
    DispatchMessage(Msg);
  end;

  Halt(Msg.wParam);
end.
