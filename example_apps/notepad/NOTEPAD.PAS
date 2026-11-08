program NOTEPAD;

{$mode delphi}

uses
  WinProcs, WinTypes, Strings;

const
  IDC_EDIT = 101;
  IDM_README  = 1001;
  IDM_LICENSE = 1002;
  IDM_CALMINI = 1003;
  IDM_WININI  = 1004;
  IDM_EXIT    = 1005;

var
  G_Instance: THandle;
  G_MainWnd: HWND;
  G_EditWnd: HWND;
  G_FileBuf: array[0..16383] of Char;
  G_TitleBuf: array[0..128] of Char;

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

procedure LoadFileToEdit(APath: PChar);
var
  FH: HFILE;
  BytesRead: Word;
  FullBuf: array[0..128] of Char;
begin
  FH := TryOpenFile(APath);
  if FH = HFILE_ERROR then
  begin
    StrCopy(FullBuf, 'C:\CALMIRA\');
    StrCat(FullBuf, APath);
    FH := TryOpenFile(FullBuf);
  end;

  if FH = HFILE_ERROR then
  begin
    SetWindowText(G_EditWnd, 'Nie mozna otworzyc pliku.');
    Exit;
  end;

  BytesRead := _lread(FH, @G_FileBuf[0], SizeOf(G_FileBuf) - 1);
  _lclose(FH);

  if BytesRead > 0 then
    G_FileBuf[BytesRead] := #0
  else
    G_FileBuf[0] := #0;

  SetWindowText(G_EditWnd, @G_FileBuf[0]);
  StrCopy(G_TitleBuf, 'Notepad - ');
  StrCat(G_TitleBuf, APath);
  SetWindowText(G_MainWnd, G_TitleBuf);
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
        0, 0, Rc.right, Rc.bottom,
        Wnd,
        IDC_EDIT,
        G_Instance,
        nil
      );
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
        IDM_README:  LoadFileToEdit('C:\CALMIRA\README.TXT');
        IDM_LICENSE: LoadFileToEdit('C:\CALMIRA\LICENSE.TXT');
        IDM_CALMINI: LoadFileToEdit('C:\CALMIRA\CALMIRA.INI');
        IDM_WININI:  LoadFileToEdit('C:\WINDOWS\WIN.INI');
        IDM_EXIT:    DestroyWindow(Wnd);
      end;
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

  AppendMenu(HSubMenu, MF_STRING, IDM_README,  '&Otworz README.TXT');
  AppendMenu(HSubMenu, MF_STRING, IDM_LICENSE, '&Otworz LICENSE.TXT');
  AppendMenu(HSubMenu, MF_STRING, IDM_CALMINI, '&Otworz CALMIRA.INI');
  AppendMenu(HSubMenu, MF_STRING, IDM_WININI,   '&Otworz WIN.INI');
  AppendMenu(HSubMenu, MF_SEPARATOR, 0, nil);
  AppendMenu(HSubMenu, MF_STRING, IDM_EXIT,    '&Zakoncz');

  AppendMenu(HMenu, MF_POPUP, HSubMenu, '&Plik');
  SetMenu(Wnd, HMenu);
end;

function IsValidFileParam(const S: ShortString): Boolean;
var
  i: Integer;
begin
  if Length(S) = 0 then
  begin
    Result := False;
    Exit;
  end;
  for i := 1 to Length(S) do
  begin
    if (S[i] < ' ') or (Ord(S[i]) >= 127) then
    begin
      Result := False;
      Exit;
    end;
  end;
  Result := True;
end;

var
  WndClass: TWndClass;
  Msg: TMsg;
  ParamS: ShortString;
  CmdParam: array[0..128] of Char;
begin
  G_Instance := HInstance;

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
    'Notepad Viewer',
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

  if ParamCount >= 1 then
    ParamS := ParamStr(1)
  else
    ParamS := '';

  if IsValidFileParam(ParamS) and (ParamS <> 'NOTEPAD.EXE') and (ParamS <> 'C:\CALMIRA\NOTEPAD.EXE') then
  begin
    StrPCopy(CmdParam, ParamS);
    LoadFileToEdit(CmdParam);
  end
  else
    LoadFileToEdit('C:\CALMIRA\README.TXT');

  while GetMessage(Msg, 0, 0, 0) do
  begin
    TranslateMessage(Msg);
    DispatchMessage(Msg);
  end;

  Halt(Msg.wParam);
end.
