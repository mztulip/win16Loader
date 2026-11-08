program NOTEPAD;

{$mode delphi}

uses
  WinProcs, WinTypes, Strings;

const
  IDC_EDIT      = 101;

  IDM_NEW       = 1001;
  IDM_OPEN      = 1002;
  IDM_SAVE      = 1003;
  IDM_SAVEAS    = 1004;
  IDM_EXIT      = 1005;

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

function GetOpenFileName(var OpenFile: TOpenFileName): Bool; far; external 'COMMDLG' index 1;
function GetSaveFileName(var OpenFile: TOpenFileName): Bool; far; external 'COMMDLG' index 2;

var
  G_Instance: THandle;
  G_MainWnd: HWND;
  G_EditWnd: HWND;
  G_FileBuf: array[0..32000] of Char;
  G_TitleBuf: array[0..160] of Char;
  G_CurFile: array[0..128] of Char;

{ Deklaracje wyprzedzajace }
procedure DoFileSave; forward;
procedure DoFileSaveAs; forward;

function TryOpenFile(APath: PChar): HFILE;
var
  FH: HFILE;
  Ofs: OFSTRUCT;
begin
  FH := _lopen(APath, 0);
  if FH = HFILE_ERROR then
    FH := OpenFile(APath, @Ofs, OF_READ);
  Result := FH;
end;

function LoadFileToEdit(APath: PChar): Boolean;
var
  FH: HFILE;
  BytesRead: Word;
begin
  Result := False;
  FH := TryOpenFile(APath);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Nie mozna otworzyc podanego pliku.', 'Blad', MB_OK or MB_ICONEXCLAMATION);
    Exit;
  end;

  BytesRead := _lread(FH, @G_FileBuf[0], SizeOf(G_FileBuf) - 1);
  _lclose(FH);

  if BytesRead > 0 then
    G_FileBuf[BytesRead] := #0
  else
    G_FileBuf[0] := #0;

  SetWindowText(G_EditWnd, @G_FileBuf[0]);
  SendMessage(G_EditWnd, WM_USER + 9, 0, 0); { EM_SETMODIFY = 0 }
  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'Notepad - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  Result := True;
end;

function SaveEditToFile(APath: PChar): Boolean;
var
  FH: HFILE;
  Len: Word;
  BytesWritten: Word;
begin
  Result := False;
  Len := GetWindowTextLength(G_EditWnd);
  if Len > SizeOf(G_FileBuf) - 1 then
    Len := SizeOf(G_FileBuf) - 1;

  if Len > 0 then
    GetWindowText(G_EditWnd, @G_FileBuf[0], SizeOf(G_FileBuf))
  else
    G_FileBuf[0] := #0;

  FH := _lcreat(APath, 0);
  if FH = HFILE_ERROR then
  begin
    MessageBox(G_MainWnd, 'Nie mozna utworzyc lub zapisac pliku.', 'Blad zapisu', MB_OK or MB_ICONSTOP);
    Exit;
  end;

  if Len > 0 then
  begin
    BytesWritten := _lwrite(FH, @G_FileBuf[0], Len);
    if BytesWritten <> Len then
    begin
      _lclose(FH);
      MessageBox(G_MainWnd, 'Blad podczas zapisywania danych do pliku.', 'Blad zapisu', MB_OK or MB_ICONSTOP);
      Exit;
    end;
  end;

  _lclose(FH);

  SendMessage(G_EditWnd, WM_USER + 9, 0, 0); { EM_SETMODIFY = 0 }
  StrCopy(G_CurFile, APath);
  StrCopy(G_TitleBuf, 'Notepad - ');
  StrCat(G_TitleBuf, G_CurFile);
  SetWindowText(G_MainWnd, G_TitleBuf);
  Result := True;
end;

procedure NewDocument;
begin
  SetWindowText(G_EditWnd, '');
  SendMessage(G_EditWnd, WM_USER + 9, 0, 0); { EM_SETMODIFY = 0 }
  G_CurFile[0] := #0;
  SetWindowText(G_MainWnd, 'Notepad - (Bez tytulu)');
end;

procedure DoFileOpen;
var
  OFN: TOpenFileName;
  FileBuf: array[0..128] of Char;
  FilterBuf: array[0..128] of Char;
begin
  FillChar(OFN, SizeOf(OFN), 0);
  FileBuf[0] := #0;

  FilterBuf := 'Pliki tekstowe (*.txt)'#0'*.txt'#0'Wszystkie pliki (*.*)'#0'*.*'#0#0;

  OFN.lStructSize := SizeOf(TOpenFileName);
  OFN.hwndOwner := G_MainWnd;
  OFN.lpstrFilter := @FilterBuf[0];
  OFN.nFilterIndex := 1;
  OFN.lpstrFile := @FileBuf[0];
  OFN.nMaxFile := SizeOf(FileBuf);
  OFN.lpstrDefExt := 'txt';
  OFN.Flags := $00001000 or $00000800 or $00000004; { OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY }

  if GetOpenFileName(OFN) then
  begin
    if StrLen(FileBuf) > 0 then
      LoadFileToEdit(FileBuf);
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

  FilterBuf := 'Pliki tekstowe (*.txt)'#0'*.txt'#0'Wszystkie pliki (*.*)'#0'*.*'#0#0;

  OFN.lStructSize := SizeOf(TOpenFileName);
  OFN.hwndOwner := G_MainWnd;
  OFN.lpstrFilter := @FilterBuf[0];
  OFN.nFilterIndex := 1;
  OFN.lpstrFile := @FileBuf[0];
  OFN.nMaxFile := SizeOf(FileBuf);
  OFN.lpstrDefExt := 'txt';
  OFN.Flags := $00000002 or $00000800 or $00000004; { OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY }

  if GetSaveFileName(OFN) then
  begin
    if StrLen(FileBuf) > 0 then
      SaveEditToFile(FileBuf);
  end;
end;

procedure DoFileSave;
begin
  if StrLen(G_CurFile) > 0 then
    SaveEditToFile(G_CurFile)
  else
    DoFileSaveAs;
end;

function MaybeSavePrompt: Boolean;
var
  Ans: Integer;
begin
  Result := True;
  if SendMessage(G_EditWnd, WM_USER + 8, 0, 0) <> 0 then { EM_GETMODIFY }
  begin
    Ans := MessageBox(G_MainWnd, 'Tekst zostal zmieniony.'#13#10'Czy chcesz zapisac zmiany?', 'Notepad', MB_YESNOCANCEL or MB_ICONQUESTION);
    case Ans of
      6: { IDYES }
      begin
        DoFileSave;
        Result := (SendMessage(G_EditWnd, WM_USER + 8, 0, 0) = 0);
      end;
      7: { IDNO }
        Result := True;
      2: { IDCANCEL }
        Result := False;
    end;
  end;
end;

function WndProc(Wnd: HWND; Msg: UINT; WParam: WPARAM; LParam: LPARAM): LRESULT; export;
var
  Rc: TRect;
begin
  case Msg of
    WM_CREATE:
    begin
      GetClientRect(Wnd, Rc);
      G_EditWnd := CreateWindow(
        'EDIT',
        '',
        WS_CHILD or WS_VISIBLE or WS_VSCROLL or WS_HSCROLL or
        ES_MULTILINE or ES_AUTOVSCROLL or ES_AUTOHSCROLL,
        0, 0,
        Rc.right, Rc.bottom,
        Wnd, IDC_EDIT, G_Instance, nil
      );

      if G_EditWnd = 0 then
      begin
        Result := -1;
        Exit;
      end;

      SendMessage(G_EditWnd, WM_SETFONT, GetStockObject(SYSTEM_FONT), 0);
      SetFocus(G_EditWnd);
      Result := 0;
    end;

    WM_SIZE:
    begin
      if G_EditWnd <> 0 then
        MoveWindow(G_EditWnd, 0, 0, LOWORD(LParam), HIWORD(LParam), True);
      Result := 0;
    end;

    WM_SETFOCUS:
    begin
      if G_EditWnd <> 0 then
        SetFocus(G_EditWnd);
      Result := 0;
    end;

    WM_COMMAND:
    begin
      case WParam of
        IDM_NEW:
          if MaybeSavePrompt then
            NewDocument;
        IDM_OPEN:
          if MaybeSavePrompt then
            DoFileOpen;
        IDM_SAVE:
          DoFileSave;
        IDM_SAVEAS:
          DoFileSaveAs;
        IDM_EXIT:
          SendMessage(Wnd, WM_CLOSE, 0, 0);
      end;
      Result := 0;
    end;

    WM_CLOSE:
    begin
      if MaybeSavePrompt then
        DestroyWindow(Wnd);
      Result := 0;
    end;

    WM_DESTROY:
    begin
      PostQuitMessage(0);
      Result := 0;
    end;

    else
      Result := DefWindowProc(Wnd, Msg, WParam, LParam);
  end;
end;

procedure CreateMainMenu(Wnd: HWND);
var
  HMenu, HSubMenu: WinTypes.HMENU;
begin
  HMenu := CreateMenu;
  HSubMenu := CreatePopupMenu;

  AppendMenu(HSubMenu, MF_STRING, IDM_NEW,    '&Nowy');
  AppendMenu(HSubMenu, MF_STRING, IDM_OPEN,   '&Otworz...');
  AppendMenu(HSubMenu, MF_STRING, IDM_SAVE,   '&Zapisz');
  AppendMenu(HSubMenu, MF_STRING, IDM_SAVEAS, 'Zapisz &jako...');
  AppendMenu(HSubMenu, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubMenu, MF_STRING, IDM_EXIT,   '&Zakoncz');

  AppendMenu(HMenu, MF_POPUP, HSubMenu, '&Plik');
  SetMenu(Wnd, HMenu);
end;

var
  WndClass: TWndClass;
  Msg: TMsg;
  ParamS: ShortString;
  CmdParam: array[0..128] of Char;
begin
  G_Instance := HInstance;
  G_CurFile[0] := #0;

  FillChar(WndClass, SizeOf(WndClass), 0);
  WndClass.style := CS_HREDRAW or CS_VREDRAW;
  WndClass.lpfnWndProc := @WndProc;
  WndClass.hInstance := G_Instance;
  WndClass.hIcon := LoadIcon(G_Instance, MakeIntResource(1));
  if WndClass.hIcon = 0 then
    WndClass.hIcon := LoadIcon(0, IDI_APPLICATION);
  WndClass.hCursor := LoadCursor(0, IDC_ARROW);
  WndClass.hbrBackground := COLOR_WINDOW + 1;
  WndClass.lpszClassName := 'W16NotepadClass';

  if not RegisterClass(WndClass) then
    Halt(1);

  G_MainWnd := CreateWindow(
    'W16NotepadClass',
    'Notepad - (Bez tytulu)',
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT,
    520, 360,
    0, 0, G_Instance, nil
  );

  if G_MainWnd = 0 then
    Halt(1);

  CreateMainMenu(G_MainWnd);
  ShowWindow(G_MainWnd, SW_SHOW);
  UpdateWindow(G_MainWnd);

  { Startujemy z pustym dokumentem (Bez tytulu) }

  while GetMessage(Msg, 0, 0, 0) do
  begin
    TranslateMessage(Msg);
    DispatchMessage(Msg);
  end;

  Halt(Msg.wParam);
end.
