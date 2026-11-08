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

  ID_DLG_EDIT   = 201;
  ID_DLG_OK     = 1;
  ID_DLG_CANCEL = 2;

var
  G_Instance: THandle;
  G_MainWnd: HWND;
  G_EditWnd: HWND;
  G_FileBuf: array[0..32000] of Char;
  G_TitleBuf: array[0..160] of Char;
  G_CurFile: array[0..128] of Char;

  G_DlgWnd: HWND;
  G_DlgEdit: HWND;
  G_DlgPath: array[0..128] of Char;
  G_DlgResult: Boolean;
  G_DlgClassRegistered: Boolean;

{ Deklaracje wyprzedzajace }
procedure DoFileSave; forward;
procedure DoFileSaveAs; forward;

function FileDlgProc(Wnd: HWND; Msg: UINT; WParam: WPARAM; LParam: LPARAM): LRESULT; export;
var
  HBtnOk, HBtnCancel, HLbl: HWND;
begin
  case Msg of
    WM_CREATE:
    begin
      HLbl := CreateWindow(
        'STATIC',
        'Podaj sciezke lub nazwe pliku:',
        WS_CHILD or WS_VISIBLE,
        14, 12, 320, 16,
        Wnd, 0, G_Instance, nil
      );

      G_DlgEdit := CreateWindow(
        'EDIT',
        G_DlgPath,
        WS_CHILD or WS_VISIBLE or WS_BORDER or ES_AUTOHSCROLL or WS_TABSTOP,
        14, 32, 320, 22,
        Wnd, ID_DLG_EDIT, G_Instance, nil
      );

      HBtnOk := CreateWindow(
        'BUTTON',
        'OK',
        WS_CHILD or WS_VISIBLE or BS_DEFPUSHBUTTON or WS_TABSTOP,
        140, 64, 85, 24,
        Wnd, ID_DLG_OK, G_Instance, nil
      );

      HBtnCancel := CreateWindow(
        'BUTTON',
        'Anuluj',
        WS_CHILD or WS_VISIBLE or BS_PUSHBUTTON or WS_TABSTOP,
        235, 64, 85, 24,
        Wnd, ID_DLG_CANCEL, G_Instance, nil
      );

      SetFocus(G_DlgEdit);
      SendMessage(G_DlgEdit, EM_SETSEL, 0, MAKELONG(0, $FFFF));
      Result := 0;
    end;

    WM_COMMAND:
    begin
      case WParam of
        ID_DLG_OK:
        begin
          GetWindowText(G_DlgEdit, G_DlgPath, SizeOf(G_DlgPath));
          G_DlgResult := True;
          DestroyWindow(Wnd);
        end;

        ID_DLG_CANCEL:
        begin
          G_DlgResult := False;
          DestroyWindow(Wnd);
        end;
      end;
      Result := 0;
    end;

    WM_CLOSE:
    begin
      G_DlgResult := False;
      DestroyWindow(Wnd);
      Result := 0;
    end;

    else
      Result := DefWindowProc(Wnd, Msg, WParam, LParam);
  end;
end;

procedure EnsureDlgClass;
var
  DlgClass: TWndClass;
begin
  if G_DlgClassRegistered then Exit;
  FillChar(DlgClass, SizeOf(DlgClass), 0);
  DlgClass.style := CS_HREDRAW or CS_VREDRAW;
  DlgClass.lpfnWndProc := @FileDlgProc;
  DlgClass.hInstance := G_Instance;
  DlgClass.hCursor := LoadCursor(0, IDC_ARROW);
  DlgClass.hbrBackground := COLOR_BTNFACE + 1;
  DlgClass.lpszClassName := 'W16PromptDlg';
  if RegisterClass(DlgClass) then
    G_DlgClassRegistered := True;
end;

function PromptFileName(const ATitle, ADefault: PChar): Boolean;
var
  Rc: TRect;
  X, Y: Integer;
  M: TMsg;
begin
  EnsureDlgClass;
  G_DlgResult := False;
  StrCopy(G_DlgPath, ADefault);

  GetWindowRect(G_MainWnd, Rc);
  X := Rc.left + (Rc.right - Rc.left - 360) div 2;
  Y := Rc.top + (Rc.bottom - Rc.top - 130) div 2;
  if X < 10 then X := 10;
  if Y < 10 then Y := 10;

  G_DlgWnd := CreateWindow(
    'W16PromptDlg',
    ATitle,
    WS_POPUP or WS_CAPTION or WS_SYSMENU or WS_VISIBLE or WS_BORDER,
    X, Y, 360, 130,
    G_MainWnd, 0, G_Instance, nil
  );

  if G_DlgWnd = 0 then
  begin
    Result := False;
    Exit;
  end;

  EnableWindow(G_MainWnd, False);

  while IsWindow(G_DlgWnd) and GetMessage(M, 0, 0, 0) do
  begin
    if not IsDialogMessage(G_DlgWnd, M) then
    begin
      TranslateMessage(M);
      DispatchMessage(M);
    end;
  end;

  EnableWindow(G_MainWnd, True);
  SetActiveWindow(G_MainWnd);
  SetFocus(G_EditWnd);

  Result := G_DlgResult;
end;

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
begin
  if PromptFileName('Otworz plik', G_CurFile) then
  begin
    if StrLen(G_DlgPath) > 0 then
      LoadFileToEdit(G_DlgPath);
  end;
end;

procedure DoFileSaveAs;
begin
  if PromptFileName('Zapisz plik jako', G_CurFile) then
  begin
    if StrLen(G_DlgPath) > 0 then
      SaveEditToFile(G_DlgPath);
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
  G_CurFile[0] := #0;
  G_DlgClassRegistered := False;

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

  if ParamCount >= 1 then
    ParamS := ParamStr(1)
  else
    ParamS := '';

  if IsValidFileParam(ParamS) and (ParamS <> 'NOTEPAD.EXE') and (ParamS <> 'C:\CALMIRA\NOTEPAD.EXE') then
  begin
    StrPCopy(CmdParam, ParamS);
    LoadFileToEdit(CmdParam);
  end;

  while GetMessage(Msg, 0, 0, 0) do
  begin
    TranslateMessage(Msg);
    DispatchMessage(Msg);
  end;

  Halt(Msg.wParam);
end.
