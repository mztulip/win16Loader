# Obsługa wyjątków CPU, ochrona pamięci i mechanizmy debugowania

Dokument opisuje architekturę obsługi błędów, sprzętową ochronę pamięci procesora x86, ekran paniki (**Panic Screen**) oraz mechanizmy diagnostyczne i debugowania w projekcie **win16Loader**.

---

## 1. Sprzętowa ochrona pamięci w trybie chronionym

W przeciwieństwie do trybu rzeczywistego DOS, w którym programy mają bezpośredni dostęp do fizycznej pamięci i mogą bezkarnie nadpisywać cudzy kod lub jądro, **win16Loader** wykonuje aplikacje w 16-bitowym trybie chronionym (Protected Mode 80286/i486).

### Limity segmentów w GDT
Dla każdego przydzielonego bloku pamięci (segmentu kodu, segmentu danych, bloku dynamicznego z `GlobalAlloc` czy stosu) w tablicy GDT tworzony jest deskryptor określający:
* **Adres bazowy (Base)**: 24- lub 32-bitowy fizyczny adres początku bloku w pamięci RAM / XMS.
* **Limit segmentu (Limit)**: dokładny maksymalny dopuszczalny offset (np. `0x1FFF` dla bufora 8 KB).
* **Prawa dostępu (Access Rights)**:
  * Segment kodu (`0x9A`): wykonywalny, tylko do odczytu (brak możliwości zapisu).
  * Segment danych (`0x92`): odczyt i zapis w granicach `0 .. Limit`.

### Wyjątki procesora przechwytywane przez loader
Gdy aplikacja złamie zasady ochrony, procesor natychmiast zatrzymuje wykonanie instrukcji i generuje wyjątek sprzętowy:
* **`#GP` – General Protection Fault (`0x0D`)**:
  * Próba odczytu lub zapisu pod offset większy niż limit deskryptora (`offset > limit`).
  * Próba zapisu do segmentu kodu (`CS`).
  * Użycie nieprawidłowego, pustego (NULL) lub nieistniejącego selektora.
* **`#SS` – Stack Fault (`0x0C`)**:
  * Wyjście wskaźnika stosu `SP` poza limit segmentu stosu (przepełnienie stosu / stack overflow).
* **`#NP` – Segment Not Present (`0x0B`)**:
  * Odwołanie do selektora, którego flaga obecności w GDT jest wyzerowana.
* **`#UD` – Invalid Opcode (`0x06`)**:
  * Napotkanie nieprawidłowej instrukcji procesora (np. próba wykonania danych zamiast kodu).
* **`#DE` – Divide Error (`0x00`)**:
  * Dzielenie przez zero lub przepełnienie wyniku dzielenia.
* **`#DF` – Double Fault (`0x08`)**:
  * Błąd podwójny (wystąpienie wyjątku podczas próby obsługi innego wyjątku).

Wektory tych wyjątków są rejestrowane w tablicy IDT (`g_idt_phys`) w funkcji [`init_int3f()`](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/loader.c#L280-L310).

---

## 2. Architektura reakcji na błąd

Reakcja systemu na naruszenie pamięci jest dwuetapowa i zależy od kontekstu wykonania ([pm_call.asm](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/pm_call.asm#L1596-L1615)):

```
                       Wyjątek CPU (#GP, #SS, #UD itp.)
                                      │
                                      ▼
                           IDT -> stub wyjątku
                                      │
                                      ▼
                               [exc_common]
                                      │
                 Liczba aktywnych zadań (KCB_NTASKS)?
                                      │
                  ┌───────────────────┴───────────────────┐
                  ▼ (>= 2 zadania)                        ▼ (1 zadanie / błąd krytyczny)
        [Izolacja procesu]                       [32-bit Panic Entry]
   Stos KCB_EXC_STACK, kernel/ktask.c              Skok do SEL_CODE32
                  │                                       │
                  ▼                                       ▼
    task_fault_c() -> MessageBox            panic_screen() w panic.c
 "Program wykonał niedozwoloną operację"       Pomarańczowy ekran VESA + COM1
                  │                                       │
                  ▼                                       ▼
  Zwolnienie zasobów procesu (kmem/user)              CPU Halted
  Powrót do pulpitu Calmiry (system żyje!)
```

### A. Izolacja procesu w trybie wielozadaniowym (`task_fault_c`)
Gdy uruchomione są co najmniej 2 zadania (`KCB_NTASKS >= 2`, np. Calmira i program użytkownika):
1. Obsługa wyjątku nie zawiesza systemu.
2. Zapisywany jest zrzut parametrów błędu (numer wyjątku, kod błędu, `CS:IP`).
3. Procesor przełącza stos na odizolowany, bezpieczny bufor `KCB_EXC_STACK`.
4. Wywoływana jest funkcja [`task_fault_c()`](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/kernel/ktask.c#L352):
   * Do logu szeregowego COM1 wypisywany jest szczegółowy komunikat:
     ```text
     KERNEL: wyjatek konczy zadanie: Program NOTEPAD wykonal niedozwolona operacje i zostal zamkniety.
       numer=0D CS:IP=08C0:1234
     ```
   * Wyświetlane jest okienko błędu Windows (`MessageBox` z ikoną `MB_ICONSTOP` `0x10`).
   * Zwalniane są wszystkie okna, kolejki wejściowe, obiekty GDI i timery powiązane z winnym zadaniem ([user/umsg.c:UserTaskExit](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/user/umsg.c#L907)).
   * Zwalniana jest dynamiczna pamięć XMS procesu ([kernel/kmem.c:kmem_free_task](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/kernel/kmem.c#L307)).
   * Następuje przełączenie kontekstu zadania (`switch_to`) na Calmirę. **Pulpit i pozostałe aplikacje działają bez zakłóceń.**

### B. Panic Screen (tryb pojedynczego zadania / błąd krytyczny)
Gdy błąd nastąpi w trybie pojedynczego zadania lub w kodzie jądra:
1. `exc_common` zapisuje pełny stan rejestrów do struktury `ExcDbg`.
2. Następuje skok do 32-bitowego trybu chronionego flat (`panic_entry_32`).
3. Wywoływana jest funkcja `panic_screen()` w [panic.c](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/panic.c):
   * Ekran VESA LFB zostaje zalany pomarańczowym kolorem ostrzegawczym (`#FFA000`) z ciemnoczerwonym paskiem nagłówka:  
     `*** EXCEPTION FAULT - SYSTEM HALTED ***`.
   * Informacje renderowane są fontem BIOS 8x16 bezpośrednio w pamięci wideo LFB.
   * Równocześnie wszystkie dane trafiają na port szeregowy COM1.

---

## 3. Zrzut diagnostyczny (Crash Dump & Backtrace)

Mechanizm `DEBUG_STACK` (włączony domyślnie flagą `DEBUG_STACK=1` w [Makefile](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/Makefile#L46)) gromadzi pełne informacje o stanie maszyny w chwili awarii:

### Rejestry i stan procesora
```text
*** EXCEPTION 000D  CS:IP=08C0:1E30  ERR=0000  SP=29F8 ***
Exception : 0x000D
#GP General Protection
CS : IP   =  08C0 : 1E30
AX=0000  BX=0081  CX=0000  DX=0000
SI=0000  DI=0000  BP=29FE  SP=29F8
DS=08C8  ES=2FA0  SS=08C8  FLAGS=3202  SS_lim=27FF
```

### Zrzut stosu (Stack Dump)
* **Ekran VESA**: 48 słów (96 bajtów) pamięci stosu wokół `SP`.
* **Port szeregowy COM1**: 256 słów (512 bajtów) zawartości stosu.

### Odtwarzanie łańcucha wywołań (Call Stack / Backtrace)
Funkcja `dump_dbg` w [panic.c](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/panic.c#L35) automatycznie analizuje ramki wskaźnika bazowego `BP` (`[BP]` = poprzedni `BP`, `[BP+2]` = adres powrotu `IP`, `[BP+4]` = opcjonalny selektor powrotu `CS`):
```text
Call Stack (BP frames):
  #0  BP=2A00  ret=08C0:14A2
  #1  BP=2A14  ret=08C0:005E
  #2  BP=2A30  ret=0618:0410
```
Działa to jak pełnoprawny `backtrace`, pozwalając natychmiast zlokalizować łańcuch funkcji, który doprowadził do przekroczenia pamięci.

---

## 4. Śledzenie wywołań API (KCB_TRACE)

W strukturze bloku kontrolnego jądra dostępna jest flaga:
* `KCB_TRACE` (offset 536 w KCB)

Gdy `KCB_TRACE != 0`, dyspozytor przerwania `INT 3Fh` w [pm_call.asm](file:///home/tulip/Dokumenty/Projekty_moje/osproject/win16Loader/pm_call.asm#L80) wypisuje na port szeregowy COM1 każde wywołanie API Win16:
* Nazwę modułu docelowego (np. `USER`, `GDI`, `KERNEL`),
* Numer ordinalu lub nazwę funkcji,
* Selektor i offset procedury docelowej,
* Przekazywane parametry ze stosu.

---

## 5. Jak odczytywać logi diagnostyczne

Wszystkie komunikaty jądra, ostrzeżenia ochrony pamięci oraz zrzuty wyjątków są kierowane na wirtualny port szeregowy COM1 emulatora:
* **W QEMU (`make run-calmira` / `make run-qemu`)**:
  Wyjście portu szeregowego jest mapowane na konsolę terminala (`-serial stdio`) oraz zapisywane w pliku `serial.log`.
* **W Bochs (`make run`)**:
  Komunikaty trafiają do pliku `bochs.log`.
