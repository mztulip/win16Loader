program PAINT;

{$mode delphi}

uses
  WinProcs, WinTypes, Strings;

const
  { Narzedzia rysowania }
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

  { Style wypelnienia ksztaltow }
  STYLE_OUTLINE       = 1;
  STYLE_FILLED_BORDER = 2;
  STYLE_FILLED_SOLID  = 3;

  { Wymiary obszaru roboczego }
  CANVAS_DEF_W = 580;
  CANVAS_DEF_H = 310;
  CANVAS_X     = 10;
  CANVAS_Y     = 88;

  { Identyfikatory menu }
  IDM_NEW       = 1001;
  IDM_OPEN      = 1002;
  IDM_SAVE      = 1003;
  IDM_SAVEAS    = 1004;
  IDM_EXIT      = 1005;

  IDM_UNDO      = 1010;
  IDM_CLEAR     = 1011;
  IDM_INVERT    = 1012;
  IDM_FLIPH     = 1013;
  IDM_FLIPV     = 1014;

  IDM_ABOUT     = 1020;

  { Flagi DrawText }
  DT_TOP        = $0000;
  DT_LEFT       = $0000;
  DT_CENTER     = $0001;
  DT_RIGHT      = $0002;
  DT_VCENTER    = $0004;
  DT_BOTTOM     = $0008;
  DT_WORDBREAK  = $0010;
  DT_SINGLELINE = $0020;

  { Flagi FloodFill }
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

  { Buforowanie plotna GDI }
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

  { Stan narzedzi i rysowania }
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

  { Wielokat }
  G_PolyPoints: array[0..31] of TPoint;
  G_PolyCount: Integer;

  { Przyciski interfejsu }
  G_Tools: array[1..TOOL_COUNT] of TToolBtn;
  G_Palette: array[0..27] of TColorRef;

  { Generator liczb losowych dla spraya }
  G_RandSeed: Word;

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
  { Rzad 1 - barwy podstawowe i ciemniejsze }
  G_Palette[0]  := RGB(0, 0, 0);         { Czarny }
  G_Palette[1]  := RGB(96, 96, 96);      { Ciemnoszary }
  G_Palette[2]  := RGB(144, 0, 0);       { Bordowy }
  G_Palette[3]  := RGB(220, 20, 20);     { Czerwony }
  G_Palette[4]  := RGB(240, 110, 0);     { Pomaranczowy }
  G_Palette[5]  := RGB(248, 192, 0);     { Zloty }
  G_Palette[6]  := RGB(248, 240, 32);    { Zolty }
  G_Palette[7]  := RGB(0, 112, 32);      { Ciemnozielony }
  G_Palette[8]  := RGB(16, 176, 64);     { Zielony }
  G_Palette[9]  := RGB(112, 224, 32);    { Jasnozielony/Limonka }
  G_Palette[10] := RGB(0, 128, 128);     { Morski/Teal }
  G_Palette[11] := RGB(0, 200, 224);     { Jasnoniebieski/Cyjan }
  G_Palette[12] := RGB(16, 32, 144);     { Granatowy }
  G_Palette[13] := RGB(32, 96, 224);     { Blekit krolewski }

  { Rzad 2 - barwy jasne, pastelowe i akcentowe }
  G_Palette[14] := RGB(255, 255, 255);   { Bialy }
  G_Palette[15] := RGB(192, 192, 192);   { Jasnoszary }
  G_Palette[16] := RGB(128, 64, 16);     { Brazowy }
  G_Palette[17] := RGB(240, 128, 128);   { Lososiowy }
  G_Palette[18] := RGB(255, 192, 160);   { Brzoskwiniowy }
  G_Palette[19] := RGB(255, 255, 176);   { Kremowy }
  G_Palette[20] := RGB(176, 240, 192);   { Mientowy }
  G_Palette[21] := RGB(128, 208, 255);   { Blekitny }
  G_Palette[22] := RGB(200, 232, 255);   { Lodowy }
  G_Palette[23] := RGB(80, 48, 160);     { Ciemnofioletowy }
  G_Palette[24] := RGB(144, 48, 192);    { Fioletowy }
  G_Palette[25] := RGB(224, 40, 192);    { Magenta/Fuksja }
  G_Palette[26] := RGB(255, 128, 192);   { Rozowy }
  G_Palette[27] := RGB(160, 120, 80);    { Bezowy/Khaki }
end;

procedure InitButtons;
var
  I, X, Y, W, H: Integer;
begin
  { Przyciski narzedzi w rzedzie 1 }
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
      TOOL_PENCIL:       StrCopy(G_Tools[I].Name, 'Olow');
      TOOL_BRUSH:        StrCopy(G_Tools[I].Name, 'Pedz');
      TOOL_AIRBRUSH:     StrCopy(G_Tools[I].Name, 'Spry');
      TOOL_ERASER:       StrCopy(G_Tools[I].Name, 'Gumk');
      TOOL_COLOR_ERASER: StrCopy(G_Tools[I].Name, 'G.Kl');
      TOOL_FILL:         StrCopy(G_Tools[I].Name, 'Wiad');
      TOOL_LINE:         StrCopy(G_Tools[I].Name, 'Lin.');
      TOOL_RECT:         StrCopy(G_Tools[I].Name, 'Pros');
      TOOL_ROUNDRECT:    StrCopy(G_Tools[I].Name, 'Zaok');
      TOOL_ELLIPSE:      StrCopy(G_Tools[I].Name, 'Elip');
      TOOL_POLYGON:      StrCopy(G_Tools[I].Name, 'Wiel');
      TOOL_EYEDROPPER:   StrCopy(G_Tools[I].Name, 'Pipe');
      TOOL_TEXT:         StrCopy(G_Tools[I].Name, 'Teks');
      TOOL_SELECT:       StrCopy(G_Tools[I].Name, 'Zazn');
    end;
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

procedure DoUndo;
var
  HdcScr: HDC;
begin
  if not G_CanUndo then
  begin
    MessageBox(G_MainWnd, 'Brak operacji do cofniecia.', 'Cofnij', MB_OK or MB_ICONINFORMATION);
    Exit;
  end;

  { Kopiuj stan z bufora Undo z powrotem na plotno }
  BitBlt(G_hdcCanvas, 0, 0, G_CanvasW, G_CanvasH, G_hdcUndo, 0, 0, SRCCOPY);
  G_CanUndo := False;

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  SetStatus('Cofnieto ostatnia operacje.');
end;

procedure ClearCanvas;
var
  HBr: HBRUSH;
  Rc: TRect;
  HdcScr: HDC;
begin
  SaveUndoState;
  HBr := CreateSolidBrush(G_ColSecondary);
  Rc.left := 0; Rc.top := 0; Rc.right := G_CanvasW; Rc.bottom := G_CanvasH;
  FillRect(G_hdcCanvas, Rc, HBr);
  DeleteObject(HBr);

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  SetStatus('Wyczyszczono plotno do koloru tla.');
end;

procedure InvertCanvas;
var
  HdcScr: HDC;
begin
  SaveUndoState;
  PatBlt(G_hdcCanvas, 0, 0, G_CanvasW, G_CanvasH, DSTINVERT);

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  SetStatus('Odwrocono kolory plotna.');
end;

procedure FlipCanvasH;
var
  X, Y: Integer;
  ColL, ColR: TColorRef;
  HdcScr: HDC;
begin
  SaveUndoState;
  for Y := 0 to G_CanvasH - 1 do
  begin
    for X := 0 to (G_CanvasW div 2) - 1 do
    begin
      ColL := GetPixel(G_hdcCanvas, X, Y);
      ColR := GetPixel(G_hdcCanvas, G_CanvasW - 1 - X, Y);
      SetPixel(G_hdcCanvas, X, Y, ColR);
      SetPixel(G_hdcCanvas, G_CanvasW - 1 - X, Y, ColL);
    end;
  end;

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  SetStatus('Odbito plotno w poziomie.');
end;

procedure FlipCanvasV;
var
  X, Y: Integer;
  ColT, ColB: TColorRef;
  HdcScr: HDC;
begin
  SaveUndoState;
  for Y := 0 to (G_CanvasH div 2) - 1 do
  begin
    for X := 0 to G_CanvasW - 1 do
    begin
      ColT := GetPixel(G_hdcCanvas, X, Y);
      ColB := GetPixel(G_hdcCanvas, X, G_CanvasH - 1 - Y);
      SetPixel(G_hdcCanvas, X, Y, ColB);
      SetPixel(G_hdcCanvas, X, G_CanvasH - 1 - Y, ColT);
    end;
  end;

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  SetStatus('Odbito plotno w pionie.');
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

  { Glowny bufor plotna }
  G_hdcCanvas := CreateCompatibleDC(HdcScr);
  G_hBmpCanvas := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldCanvas := SelectObject(G_hdcCanvas, G_hBmpCanvas);

  { Bufor Undo }
  G_hdcUndo := CreateCompatibleDC(HdcScr);
  G_hBmpUndo := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldUndo := SelectObject(G_hdcUndo, G_hBmpUndo);

  { Bufor podgladu tymczasowego }
  G_hdcTemp := CreateCompatibleDC(HdcScr);
  G_hBmpTemp := CreateCompatibleBitmap(HdcScr, G_CanvasW, G_CanvasH);
  G_hBmpOldTemp := SelectObject(G_hdcTemp, G_hBmpTemp);

  ReleaseDC(Wnd, HdcScr);

  { Wyczysc plotno do bieli }
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
end;

{ Procedury rysowania narzedzi na DC }
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
      { Rysuj linie przerywana / ramke wyboru }
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
    { Proste przyblizenie geometryczne }
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

{ Zapis i odczyt plikow BMP }
function SaveCanvasToBMP(APath: PChar): Boolean;
var
  FH: HFILE;
  FileHdr: TBitmapFileHeader;
  InfoHdr: TBitmapInfoHeader;
  X, Y: Integer;
  RowBytes, PadBytes: Integer;
  Color: TColorRef;
  RowBuf: array[0..2047] of Byte;
  Idx: Integer;
  PadZero: Longint;
begin
  Result := False;
  RowBytes := G_CanvasW * 3;
  PadBytes := (4 - (RowBytes mod 4)) mod 4;

  FillChar(FileHdr, SizeOf(FileHdr), 0);
  FillChar(InfoHdr, SizeOf(InfoHdr), 0);

  FileHdr.bfType := $4D42; { 'BM' }
  FileHdr.bfOffBits := SizeOf(TBitmapFileHeader) + SizeOf(TBitmapInfoHeader);
  FileHdr.bfSize := FileHdr.bfOffBits + Longint(RowBytes + PadBytes) * Longint(G_CanvasH);

  InfoHdr.biSize := SizeOf(TBitmapInfoHeader);
  InfoHdr.biWidth := G_CanvasW;
  InfoHdr.biHeight := G_CanvasH;
  InfoHdr.biPlanes := 1;
  InfoHdr.biBitCount := 24;
  InfoHdr.biCompression := 0; { BI_RGB }
  InfoHdr.biSizeImage := Longint(RowBytes + PadBytes) * Longint(G_CanvasH);

  FH := _lcreat(APath, 0);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Nie mozna utworzyc pliku BMP do zapisu.', 'Blad zapisu', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  _lwrite(FH, @FileHdr, SizeOf(FileHdr));
  _lwrite(FH, @InfoHdr, SizeOf(InfoHdr));

  PadZero := 0;

  { Skanowanie linii od dolu do gory (standard BMP) }
  for Y := G_CanvasH - 1 downto 0 do
  begin
    Idx := 0;
    for X := 0 to G_CanvasW - 1 do
    begin
      Color := GetPixel(G_hdcCanvas, X, Y);
      { BGR }
      RowBuf[Idx]     := (Color shr 16) and $FF; { Blue }
      RowBuf[Idx + 1] := (Color shr 8) and $FF;  { Green }
      RowBuf[Idx + 2] := Color and $FF;         { Red }
      Inc(Idx, 3);
    end;
    _lwrite(FH, @RowBuf[0], RowBytes);
    if PadBytes > 0 then
      _lwrite(FH, @PadZero, PadBytes);
  end;

  _lclose(FH);
  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'PaintCraft - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  SetStatus('Plik zostal pomyslnie zapisany.');
  Result := True;
end;

function LoadCanvasFromBMP(APath: PChar): Boolean;
var
  FH: HFILE;
  FileHdr: TBitmapFileHeader;
  InfoHdr: TBitmapInfoHeader;
  X, Y: Integer;
  RowBytes, PadBytes: Integer;
  RowBuf: array[0..2047] of Byte;
  Idx: Integer;
  Color: TColorRef;
  HdcScr: HDC;
  DrawW, DrawH: Integer;
begin
  Result := False;
  FH := _lopen(APath, 0);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Nie mozna otworzyc wybranego pliku.', 'Blad odczytu', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  _lread(FH, @FileHdr, SizeOf(FileHdr));
  if FileHdr.bfType <> $4D42 then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'Wybrany plik nie jest poprawnym plikiem BMP.', 'Blad', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  _lread(FH, @InfoHdr, SizeOf(InfoHdr));
  if (InfoHdr.biBitCount <> 24) and (InfoHdr.biBitCount <> 32) then
  begin
    _lclose(FH);
    MessageBox(G_MainWnd, 'PaintCraft obsluguje 24-bitowe mapy bitowe BMP.', 'Format pliku', MB_OK or MB_ICONINFORMATION);
    Exit;
  end;

  SaveUndoState;

  _llseek(FH, FileHdr.bfOffBits, 0);

  DrawW := InfoHdr.biWidth;
  if DrawW > G_CanvasW then DrawW := G_CanvasW;
  DrawH := InfoHdr.biHeight;
  if DrawH > G_CanvasH then DrawH := G_CanvasH;

  RowBytes := InfoHdr.biWidth * (InfoHdr.biBitCount div 8);
  PadBytes := (4 - (RowBytes mod 4)) mod 4;

  for Y := InfoHdr.biHeight - 1 downto 0 do
  begin
    _lread(FH, @RowBuf[0], RowBytes + PadBytes);
    if Y < DrawH then
    begin
      Idx := 0;
      for X := 0 to DrawW - 1 do
      begin
        { Odczytaj BGR }
        Color := RGB(RowBuf[Idx + 2], RowBuf[Idx + 1], RowBuf[Idx]);
        SetPixel(G_hdcCanvas, X, Y, Color);
        Inc(Idx, InfoHdr.biBitCount div 8);
      end;
    end;
  end;

  _lclose(FH);

  HdcScr := GetDC(G_MainWnd);
  BitBlt(HdcScr, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
  ReleaseDC(G_MainWnd, HdcScr);

  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'PaintCraft - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  SetStatus('Wczytano obraz z pliku BMP.');
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
  FilterBuf := 'Mapy bitowe (*.bmp)'#0'*.bmp'#0'Wszystkie pliki (*.*)'#0'*.*'#0#0;

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
  FilterBuf := 'Mapy bitowe (*.bmp)'#0'*.bmp'#0'Wszystkie pliki (*.*)'#0'*.*'#0#0;

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

{ Procedury rysowania nowoczesnego interfejsu GDI }
procedure DrawModernUI(DC: HDC);
var
  I, X, Y, W: Integer;
  R: TRect;
  HBr, OldBr: HBRUSH;
  HP, OldPen: HPEN;
  ClientRc: TRect;
  TextBuf: array[0..64] of Char;
begin
  GetClientRect(G_MainWnd, ClientRc);

  { 1. Tlo panelu sterowania (nowoczesny neutralny jasnoszary) }
  R.left := 0; R.top := 0; R.right := ClientRc.right; R.bottom := CANVAS_Y - 4;
  HBr := CreateSolidBrush(RGB(236, 238, 241));
  FillRect(DC, R, HBr);
  DeleteObject(HBr);

  { Subtelna linia podzialu pod paleta }
  HP := CreatePen(PS_SOLID, 1, RGB(180, 185, 195));
  OldPen := SelectObject(DC, HP);
  MoveTo(DC, 0, CANVAS_Y - 5);
  LineTo(DC, ClientRc.right, CANVAS_Y - 5);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  { 2. Rysowanie przyciskow narzedzi (Rzad 1) }
  SetBkMode(DC, TRANSPARENT);
  for I := 1 to TOOL_COUNT do
  begin
    R := G_Tools[I].R;
    if G_ActiveTool = I then
    begin
      { Aktywne narzedzie - wyraziste podswietlenie w nowoczesnym stylu akcentowym }
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
      { Przycisk nieaktywny }
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

  { 3. Rzad 2: Opcje narzedzia (Rozmiar pedzla, Style ksztaltu, Akcje) }
  { Rozmiary pedzla }
  SetTextColor(DC, RGB(60, 64, 72));
  TextOut(DC, 8, 28, 'Grubosc:', 8);
  for I := 1 to 5 do
  begin
    X := 65 + (I - 1) * 26;
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
    if G_BrushSize = W then
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
      1: StrCopy(TextBuf, '1');
      2: StrCopy(TextBuf, '2');
      3: StrCopy(TextBuf, '4');
      4: StrCopy(TextBuf, '8');
      5: StrCopy(TextBuf, '12');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  { Style ksztaltu }
  TextOut(DC, 206, 28, 'Styl:', 5);
  for I := 1 to 3 do
  begin
    X := 245 + (I - 1) * 44;
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
      1: StrCopy(TextBuf, 'Obrys');
      2: StrCopy(TextBuf, 'Pelny+');
      3: StrCopy(TextBuf, 'Pelny');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  { Przyciski szybkich akcji }
  for I := 1 to 5 do
  begin
    X := 386 + (I - 1) * 44;
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
      1: StrCopy(TextBuf, 'Cofnij');
      2: StrCopy(TextBuf, 'Czysc');
      3: StrCopy(TextBuf, 'Negat');
      4: StrCopy(TextBuf, 'Odb.X');
      5: StrCopy(TextBuf, 'Odb.Y');
    end;
    DrawText(DC, TextBuf, -1, R, DT_CENTER or DT_VCENTER or DT_SINGLELINE);
  end;

  { 4. Podwojny wskaznik aktywnych kolorow (Dual-Color Indicator) }
  { Kolor Tla (PPM - Prawy przycisk myszy) na dole po prawej }
  R.left := 22; R.top := 58; R.right := 48; R.bottom := 80;
  HBr := CreateSolidBrush(G_ColSecondary);
  FillRect(DC, R, HBr);
  DeleteObject(HBr);
  HP := CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
  OldPen := SelectObject(DC, HP);
  HBr := GetStockObject(NULL_BRUSH);
  OldBr := SelectObject(DC, HBr);
  Rectangle(DC, R.left, R.top, R.right, R.bottom);

  { Kolor Glowny (LPM - Lewy przycisk myszy) na gorze po lewej (nakladajacy sie) }
  R.left := 10; R.top := 50; R.right := 36; R.bottom := 72;
  HBr := CreateSolidBrush(G_ColPrimary);
  FillRect(DC, R, HBr);
  DeleteObject(HBr);
  Rectangle(DC, R.left, R.top, R.right, R.bottom);
  SelectObject(DC, OldBr);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  { 5. Nowoczesna paleta 28 probek barwnych }
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

  { 6. Ramka wokol plotna (ciemniejsza, nowoczesna krawedz) }
  HP := CreatePen(PS_SOLID, 1, RGB(160, 165, 175));
  OldPen := SelectObject(DC, HP);
  HBr := GetStockObject(NULL_BRUSH);
  OldBr := SelectObject(DC, HBr);
  Rectangle(DC, CANVAS_X - 1, CANVAS_Y - 1, CANVAS_X + G_CanvasW + 1, CANVAS_Y + G_CanvasH + 1);
  SelectObject(DC, OldBr);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  { Cien z prawej i dolu plotna }
  HP := CreatePen(PS_SOLID, 1, RGB(210, 215, 222));
  OldPen := SelectObject(DC, HP);
  MoveTo(DC, CANVAS_X + G_CanvasW + 1, CANVAS_Y);
  LineTo(DC, CANVAS_X + G_CanvasW + 1, CANVAS_Y + G_CanvasH + 2);
  LineTo(DC, CANVAS_X, CANVAS_Y + G_CanvasH + 1);
  SelectObject(DC, OldPen);
  DeleteObject(HP);

  { 7. Dolny pasek stanu (Status Bar) }
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

      StrCopy(G_StatusText, 'PaintCraft - Gotowy. LPM: Kolor glowny, PPM: Kolor tla.');
      Result := 0;
    end;

    WM_PAINT:
    begin
      DC := BeginPaint(Wnd, PS);
      DrawModernUI(DC);
      if G_hdcCanvas <> 0 then
        BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
      EndPaint(Wnd, PS);
      Result := 0;
    end;

    WM_ERASEBKGND:
    begin
      Result := 1;
    end;

    WM_COMMAND:
    begin
      case WParam of
        IDM_NEW:
          ClearCanvas;
        IDM_OPEN:
          DoFileOpen;
        IDM_SAVE:
          DoFileSave;
        IDM_SAVEAS:
          DoFileSaveAs;
        IDM_UNDO:
          DoUndo;
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
            'PaintCraft dla Win16 & DOS'#13#10 +
            'Nowoczesne studio rysunku i edytor grafiki.'#13#10 +
            'Zamiennik i rozwiniecie Windows 3.1 Paintbrush.'#13#10#13#10 +
            'Cechy programu:'#13#10 +
            '- Podwojnie buforowane plotno GDI (brak migotania)'#13#10 +
            '- Podglad ksztaltow w czasie rzeczywistym'#13#10 +
            '- Pelna paleta 28 nowoczesnych barw z podwojnym wskaznikiem'#13#10 +
            '- Narzedzia: olowek, pedzel, spray, gumka, gumka selektywna,'#13#10 +
            '  wiadro z farba, linia, figury geometryczne, tekst, pipeta'#13#10 +
            '- Wielopoziomowe Undo i filtry obrazu (odbicia, negatyw)'#13#10 +
            '- Zapis i odczyt standardowych map bitowych BMP (24-bit)'#13#10#13#10 +
            'Napisano w jezyku Pascal dla srodowiska Win16.',
            'O programie PaintCraft',
            MB_OK or $0040 { MB_ICONINFORMATION }
          );
      end;
      Result := 0;
    end;

    WM_LBUTTONDOWN:
    begin
      X := LOWORD(LParam);
      Y := HIWORD(LParam);

      { 1. Klikniecie w przyciski narzedzi }
      for I := 1 to TOOL_COUNT do
      begin
        if (X >= G_Tools[I].R.left) and (X <= G_Tools[I].R.right) and
           (Y >= G_Tools[I].R.top) and (Y <= G_Tools[I].R.bottom) then
        begin
          G_ActiveTool := I;
          G_PolyCount := 0;
          InvalidateRect(Wnd, nil, False);
          SetStatus('Zmieniono aktywne narzedzie.');
          Result := 0;
          Exit;
        end;
      end;

      { 2. Wybor grubosci pedzla (Row 2, X: 65..195, Y: 26..44) }
      if (Y >= 26) and (Y <= 44) and (X >= 65) and (X <= 195) then
      begin
        I := ((X - 65) div 26) + 1;
        case I of
          1: G_BrushSize := 1;
          2: G_BrushSize := 2;
          3: G_BrushSize := 4;
          4: G_BrushSize := 8;
          5: G_BrushSize := 12;
        end;
        InvalidateRect(Wnd, nil, False);
        SetStatus('Zmieniono grubosc pedzla.');
        Result := 0;
        Exit;
      end;

      { 3. Wybor stylu ksztaltu (Row 2, X: 245..377, Y: 26..44) }
      if (Y >= 26) and (Y <= 44) and (X >= 245) and (X <= 377) then
      begin
        I := ((X - 245) div 44) + 1;
        if (I >= 1) and (I <= 3) then
          G_ShapeStyle := I;
        InvalidateRect(Wnd, nil, False);
        SetStatus('Zmieniono styl rysowania figur.');
        Result := 0;
        Exit;
      end;

      { 4. Szybkie akcje (Row 2, X: 386..606, Y: 26..44) }
      if (Y >= 26) and (Y <= 44) and (X >= 386) and (X <= 606) then
      begin
        I := ((X - 386) div 44) + 1;
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

      { 5. Wybor koloru z palety (LPM -> Kolor glowny) }
      if (Y >= 50) and (Y <= 80) and (X >= 56) and (X <= 56 + 14 * 19) then
      begin
        SwatchIdx := (X - 56) div 19;
        if (SwatchIdx >= 0) and (SwatchIdx < 14) then
        begin
          if Y >= 66 then
            SwatchIdx := SwatchIdx + 14;
          G_ColPrimary := G_Palette[SwatchIdx];
          InvalidateRect(Wnd, nil, False);
          SetStatus('Ustawiono kolor glowny (LPM).');
          Result := 0;
          Exit;
        end;
      end;

      { 6. Klikniecie wewnatrz obszaru plotna }
      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        SetCapture(Wnd);
        X := X - CANVAS_X;
        Y := Y - CANVAS_Y;

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
            SaveUndoState;
            ExtFloodFill(G_hdcCanvas, X, Y, GetPixel(G_hdcCanvas, X, Y), FLOODFILLSURFACE);
            DC := GetDC(Wnd);
            BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
            ReleaseDC(Wnd, DC);
            SetStatus('Wypelniono obszar kolorem.');
          end;

          TOOL_EYEDROPPER:
          begin
            G_ColPrimary := GetPixel(G_hdcCanvas, X, Y);
            InvalidateRect(Wnd, nil, False);
            SetStatus('Pobrano kolor glowny pipeta.');
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
            SetStatus('Wstawiono napis tekstowy.');
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
              SetStatus('Wielokat: Klikaj kolejne punkty. Dwuklik konczy figure.');
            end
            else if G_PolyCount < 30 then
            begin
              { Sprawdz czy klikniecie zamyka wielokat (blisko startu) }
              if (Abs(X - G_PolyPoints[0].x) < 6) and (Abs(Y - G_PolyPoints[0].y) < 6) and (G_PolyCount >= 2) then
              begin
                { Zamknij wielokat }
                Polygon(G_hdcCanvas, G_PolyPoints, G_PolyCount);
                G_PolyCount := 0;
                DC := GetDC(Wnd);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
                ReleaseDC(Wnd, DC);
                SetStatus('Zamknieto i wypelniono wielokat.');
              end
              else
              begin
                G_PolyPoints[G_PolyCount].x := X;
                G_PolyPoints[G_PolyCount].y := Y;
                Inc(G_PolyCount);
                { Narysuj krawedz }
                ApplyDrawShape(G_hdcCanvas, TOOL_LINE, 0, G_LastX, G_LastY, X, Y, G_ColPrimary, 0, G_BrushSize);
                G_LastX := X; G_LastY := Y;
                DC := GetDC(Wnd);
                BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
                ReleaseDC(Wnd, DC);
              end;
            end;
          end;

          TOOL_LINE, TOOL_RECT, TOOL_ROUNDRECT, TOOL_ELLIPSE, TOOL_SELECT:
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

      { Wybor koloru tla (PPM) z palety }
      if (Y >= 50) and (Y <= 80) and (X >= 56) and (X <= 56 + 14 * 19) then
      begin
        SwatchIdx := (X - 56) div 19;
        if (SwatchIdx >= 0) and (SwatchIdx < 14) then
        begin
          if Y >= 66 then
            SwatchIdx := SwatchIdx + 14;
          G_ColSecondary := G_Palette[SwatchIdx];
          InvalidateRect(Wnd, nil, False);
          SetStatus('Ustawiono kolor tla / wtorny (PPM).');
          Result := 0;
          Exit;
        end;
      end;

      { Pipeta pod prawym przyciskiem: probkowanie koloru tla }
      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        if G_ActiveTool = TOOL_EYEDROPPER then
        begin
          G_ColSecondary := GetPixel(G_hdcCanvas, X - CANVAS_X, Y - CANVAS_Y);
          InvalidateRect(Wnd, nil, False);
          SetStatus('Pobrano kolor tla pipeta.');
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

      { Aktualizuj wspolrzedne na pasku stanu }
      if (X >= CANVAS_X) and (X < CANVAS_X + G_CanvasW) and
         (Y >= CANVAS_Y) and (Y < CANVAS_Y + G_CanvasH) then
      begin
        StrCopy(StatusCoord, 'Kursor: X=');
        IntToStrBuf(X - CANVAS_X, CoordBuf);
        StrCat(StatusCoord, CoordBuf);
        StrCat(StatusCoord, ' Y=');
        IntToStrBuf(Y - CANVAS_Y, CoordBuf);
        StrCat(StatusCoord, CoordBuf);
        StrCat(StatusCoord, ' px  |  Plotno: 580 x 310');
        SetStatus(StatusCoord);
      end;

      { Rysowanie ciagle narzedziami odrecznymi }
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

      { Podglad ksztaltu w locie (Rubber-banding bez migotania przez hdcTemp) }
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

      if G_IsDragging then
      begin
        G_IsDragging := False;
        { Zatwierdz ostateczny ksztalt na glownym plotnie }
        ApplyDrawShape(G_hdcCanvas, G_ActiveTool, G_ShapeStyle,
                       G_StartX, G_StartY, G_CurX, G_CurY,
                       G_ColPrimary, G_ColSecondary, G_BrushSize);

        DC := GetDC(Wnd);
        BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
        ReleaseDC(Wnd, DC);
        SetStatus('Narysowano ksztalt.');
      end;
      Result := 0;
    end;

    WM_LBUTTONDBLCLK:
    begin
      { Dwuklik w narzedziu wielokata zamyka i wypelnia figure }
      if (G_ActiveTool = TOOL_POLYGON) and (G_PolyCount >= 2) then
      begin
        Polygon(G_hdcCanvas, G_PolyPoints, G_PolyCount);
        G_PolyCount := 0;
        DC := GetDC(Wnd);
        BitBlt(DC, CANVAS_X, CANVAS_Y, G_CanvasW, G_CanvasH, G_hdcCanvas, 0, 0, SRCCOPY);
        ReleaseDC(Wnd, DC);
        SetStatus('Zamknieto i ukonczono wielokat.');
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

  { Menu Plik }
  HSubFile := CreatePopupMenu;
  AppendMenu(HSubFile, MF_STRING, IDM_NEW,    '&Nowy'#9'Ctrl+N');
  AppendMenu(HSubFile, MF_STRING, IDM_OPEN,   '&Otworz...'#9'Ctrl+O');
  AppendMenu(HSubFile, MF_STRING, IDM_SAVE,   '&Zapisz'#9'Ctrl+S');
  AppendMenu(HSubFile, MF_STRING, IDM_SAVEAS, 'Zapisz &jako...');
  AppendMenu(HSubFile, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubFile, MF_STRING, IDM_EXIT,   '&Zakoncz'#9'Alt+F4');
  AppendMenu(HMenu, MF_POPUP, HSubFile, '&Plik');

  { Menu Edycja }
  HSubEdit := CreatePopupMenu;
  AppendMenu(HSubEdit, MF_STRING, IDM_UNDO,   '&Cofnij'#9'Ctrl+Z');
  AppendMenu(HSubEdit, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubEdit, MF_STRING, IDM_CLEAR,  '&Wyczysc plotno');
  AppendMenu(HSubEdit, MF_STRING, IDM_INVERT, '&Odwroc kolory (Negatyw)');
  AppendMenu(HSubEdit, MF_STRING, IDM_FLIPH,  'Odbij w &poziomie');
  AppendMenu(HSubEdit, MF_STRING, IDM_FLIPV,  'Odbij w &pionie');
  AppendMenu(HMenu, MF_POPUP, HSubEdit, '&Edycja');

  { Menu Pomoc }
  HSubHelp := CreatePopupMenu;
  AppendMenu(HSubHelp, MF_STRING, IDM_ABOUT,  '&O programie PaintCraft...');
  AppendMenu(HMenu, MF_POPUP, HSubHelp, 'P&omoc');

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
    'PaintCraft - [Bez tytulu]',
    WS_OVERLAPPED or WS_CAPTION or WS_SYSMENU or WS_MINIMIZEBOX,
    CW_USEDEFAULT, CW_USEDEFAULT,
    620, 460,
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
