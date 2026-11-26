/* mmsystem.c - MMSYSTEM.DLL: timer, joystick stub, waveOut (SB16 DMA + fallback) */
#include "w16.h"

#define MMSYSERR_NOERROR      0
#define MMSYSERR_INVALHANDLE  5
#define JOYERR_UNPLUGGED      167

#define WHDR_DONE             0x0001
#define WHDR_PREPARED         0x0002
#define WHDR_INQUEUE          0x0010

#define TIME_MS               0x0001
#define TIME_SAMPLES          0x0002
#define TIME_BYTES            0x0004

#define CALLBACK_TYPEMASK     0x00070000UL
#define CALLBACK_WINDOW       0x00010000UL
#define CALLBACK_FUNCTION     0x00030000UL

#define MM_WOM_OPEN           0x03BB
#define MM_WOM_CLOSE          0x03BC
#define MM_WOM_DONE           0x03BD
#define WOM_OPEN              MM_WOM_OPEN
#define WOM_CLOSE             MM_WOM_CLOSE
#define WOM_DONE              MM_WOM_DONE

/* Sound Blaster (SB 2.0 / SB Pro / ES1688 / SB16): port, kanal DMA 8-bit z BLASTER (KCB) */
#define SEL_KCB               0x98
#define KCB_SB_PORT           570
#define KCB_SB_DMA            572
static WORD g_sb_base = 0x220;
static WORD g_sb_dma  = 1;
#define SB_RESET              (g_sb_base + 0x06)
#define SB_READ               (g_sb_base + 0x0A)
#define SB_WRITE              (g_sb_base + 0x0C)
#define SB_READ_STATUS        (g_sb_base + 0x0E)

static void io_outb(WORD port, BYTE val);
#pragma aux io_outb = "out dx, al" parm [dx] [al];

static BYTE io_inb(WORD port);
#pragma aux io_inb = "in al, dx" parm [dx] value [al];

typedef struct {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
} PCMWAVEFORMAT;

typedef struct {
    LPSTR  lpData;
    DWORD  dwBufferLength;
    DWORD  dwBytesRecorded;
    DWORD  dwUser;
    DWORD  dwFlags;
    DWORD  dwLoops;
    LPVOID lpNext;
    DWORD  reserved;
} WAVEHDR;

typedef struct {
    WORD wType;
    union {
        DWORD ms;
        DWORD sample;
        DWORD cb;
    } u;
} MMTIME;

typedef struct {
    WORD  wMid;
    WORD  wPid;
    WORD  vDriverVersion;
    char  szPname[32];
    DWORD dwFormats;
    WORD  wChannels;
    DWORD dwSupport;
} WAVEOUTCAPS;

typedef void (API *WAVECALLBACK)(WORD hwo, WORD uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);

static BOOL  g_wo_open = FALSE;
static BOOL  g_wo_looping = FALSE;
static DWORD g_wo_callback = 0;
static DWORD g_wo_instance = 0;
static DWORD g_wo_flags = 0;
static DWORD g_wo_rate = 22050;
static DWORD g_wo_bps = 22050;
static WORD  g_wo_channels = 1;
static WORD  g_wo_align = 1;
static DWORD g_wo_start_tick = 0;
static DWORD g_wo_written_bytes = 0;

/* Bufor DMA 64KB wyrownany do 64KB (<16MB) = pierscien o dlugosci bufora gry (<= 64KB):
 * offset w DMA == offset w buforze petli gry, wiec pozycja odtwarzania to wprost licznik DMA. */
static int   g_sb_present = -1;     /* -1=nie sprawdzono, 0=brak/wylaczony, 1=jest */
static BYTE  g_dsp_major = 0, g_dsp_minor = 0;
static BOOL  g_sb_playing = FALSE;
static BOOL  g_sb_autoinit = FALSE;
static WORD  g_dma_hmem = 0;
static WORD  g_dma_sel = 0;
static DWORD g_dma_phys = 0;
static DWORD g_dma_len = 0;          /* dlugosc pierscienia DMA (bajty) */
static LPSTR g_wo_src = NULL;
static DWORD g_wo_src_len = 0;
static WORD  g_dma_last_hw = 0;
static DWORD g_dma_wraps = 0;
static DWORD g_dma_moved_tick = 0;   /* ostatni ruch licznika DMA (watchdog) */
static DWORD g_last_ms = 0;
static BOOL  g_dma_ever_moved = FALSE;
static DWORD g_last_bytes = 0;       /* ostatnia zwrocona pozycja (bajty) */
static WORD  g_poll = 0;             /* licznik wywolan waveOutGetPosition */
static WORD  g_sync_hw = 0;          /* licznik DMA przy ostatnim kopiowaniu do pierscienia */          /* ostatnia zwrocona pozycja (ms) - ciaglosc przy watchdogu */

#define SYNC_AHEAD  0x3000u          /* tyle bajtow przed pozycja odtwarzania kopiujemy do DMA */
#define STALL_MS    500UL            /* licznik DMA stoi tyle ms -> zegar systemowy */

static void fast_copy(void __far *d, const void __far *s, WORD n);
#pragma aux fast_copy = \
    "push ds"             \
    "mov ds, dx"          \
    "shr cx, 1"           \
    "rep movsw"           \
    "adc cx, cx"          \
    "rep movsb"           \
    "pop ds"              \
    parm [es di] [dx si] [cx] modify [si di cx];

static void fast_fill(void __far *d, BYTE v, WORD n);
#pragma aux fast_fill = \
    "rep stosb"           \
    parm [es di] [al] [cx] modify [di cx];

/* Kanal DMA 8-bit: port adresu, licznika, strony */
static WORD dma_addr_port(void)  { return (WORD)(g_sb_dma * 2); }
static WORD dma_count_port(void) { return (WORD)(g_sb_dma * 2 + 1); }
static WORD dma_page_port(void)  { return g_sb_dma == 0 ? 0x87 : g_sb_dma == 3 ? 0x82 : 0x83; }

static void sb_wait_write(void)
{
    WORD t;
    for (t = 0; t < 30000; t++)
        if (!(io_inb(SB_WRITE) & 0x80)) break;
}

static void sb_write(BYTE b)
{
    sb_wait_write();
    io_outb(SB_WRITE, b);
}

static int sb_read(void)
{
    WORD t;
    for (t = 0; t < 30000; t++)
        if (io_inb(SB_READ_STATUS) & 0x80) return io_inb(SB_READ);
    return -1;
}

static BOOL sb_reset(void)
{
    WORD t;
    io_outb(SB_RESET, 1);
    for (t = 0; t < 200; t++) (void)io_inb(SB_RESET);   /* >= 3 us */
    io_outb(SB_RESET, 0);
    for (t = 0; t < 10000; t++) {
        if (io_inb(SB_READ_STATUS) & 0x80) {
            if (io_inb(SB_READ) == 0xAA) return TRUE;
        }
    }
    return FALSE;
}

static void sb_disable(LPCSTR why)
{
    dbg("MMSYSTEM: "); dbg(why); dbg(" -> zegar systemowy zamiast DMA\n");
    g_sb_present = 0;
    g_sb_playing = FALSE;
}

static BOOL sb_init_once(void)
{
    int v;
    if (g_sb_present >= 0) return g_sb_present != 0;
    {
        WORD port = *(WORD __far *)MK_FP(SEL_KCB, KCB_SB_PORT);
        WORD dma  = *(WORD __far *)MK_FP(SEL_KCB, KCB_SB_DMA);
        if (port >= 0x210 && port <= 0x280) g_sb_base = port;
        if (dma == 0 || dma == 1 || dma == 3) g_sb_dma = dma;
    }
    if (!sb_reset()) {
        dbg("MMSYSTEM: brak Sound Blastera na "); dbg_hex(g_sb_base, 3); dbg(" (tryb wirtualny)\n");
        g_sb_present = 0;
        return FALSE;
    }
    sb_write(0xE1);                                 /* wersja DSP */
    v = sb_read(); g_dsp_major = v < 0 ? 0 : (BYTE)v;
    v = sb_read(); g_dsp_minor = v < 0 ? 0 : (BYTE)v;
    g_dma_hmem = GlobalAlloc(0x0040, 0x20000UL);
    if (!g_dma_hmem) {
        g_sb_present = 0;
        return FALSE;
    }
    {
        DWORD base = GetSelectorBase(g_dma_hmem);
        g_dma_phys = (base + 0xFFFFUL) & ~0xFFFFUL;
        if (g_dma_phys >= 0x01000000UL) {
            dbg("MMSYSTEM: bufor DMA >= 16MB\n");
            GlobalFree(g_dma_hmem); g_dma_hmem = 0;
            g_sb_present = 0;
            return FALSE;
        }
        g_dma_sel = AllocSelector(g_dma_hmem);
        if (!g_dma_sel) {
            GlobalFree(g_dma_hmem); g_dma_hmem = 0;
            g_sb_present = 0;
            return FALSE;
        }
        SetSelectorBase(g_dma_sel, g_dma_phys);
        SetSelectorLimit(g_dma_sel, 0xFFFFUL);
    }
    fast_fill(MK_FP(g_dma_sel, 0), 0x80, 0x8000);   /* cisza 8-bit unsigned */
    fast_fill(MK_FP(g_dma_sel, 0x8000), 0x80, 0x8000);
    g_sb_present = 1;
    dbg("MMSYSTEM: Sound Blaster port="); dbg_hex(g_sb_base, 3);
    dbg(" DSP="); dbg_hex(g_dsp_major, 2); dbg("."); dbg_hex(g_dsp_minor, 2);
    dbg(g_dsp_major >= 4 ? " (SB16)" : " (SB 2.0/Pro/ES688)");
    dbg(" DMA="); dbg_hex(g_sb_dma, 1); dbg(" phys="); dbg_hex(g_dma_phys, 8);
    dbg(" sel="); dbg_hex(g_dma_sel, 4); dbg("\n");
    return TRUE;
}

/* Licznik adresu DMA = offset aktualnie czytanego bajtu w pierscieniu.
 * 16 bit czytane dwoma bajtami: mlodszy moze sie przekrecic miedzy odczytami,
 * wiec czytamy do skutku dwa razy te sama wartosc (+-1 bajt). */
static WORD dma_hw_read(void)
{
    BYTE lo, hi;
    io_outb(0x0C, 0x00);                            /* clear flip-flop */
    lo = io_inb(dma_addr_port());
    hi = io_inb(dma_addr_port());
    return (WORD)lo | ((WORD)hi << 8);
}

static WORD dma_hw_pos(void)
{
    WORD a = dma_hw_read(), b, i;
    for (i = 0; i < 4; i++) {
        b = dma_hw_read();
        if ((WORD)(b - a) <= 4 || (WORD)(a - b) <= 4) return b;
        a = b;
    }
    return b;
}

/* Kopiuj src[from .. from+n) do DMA (z zawinieciem w pierscieniu) */
static void sb_copy_ring(WORD from, WORD n)
{
    while (n) {
        WORD k = (DWORD)from + n > g_dma_len ? (WORD)(g_dma_len - from) : n;
        fast_copy(MK_FP(g_dma_sel, from), g_wo_src + from, k);
        n -= k;
        from = (WORD)(((DWORD)from + k) % g_dma_len);
    }
}

/* Petla: odswiez kawalek przed pozycja odtwarzania (gra dopisuje dane w biegu) */
static void sb_sync_ahead(WORD hw)
{
    WORD n;
    if (!g_dma_sel || !g_wo_src || !g_dma_len) return;
    n = g_dma_len < SYNC_AHEAD ? (WORD)g_dma_len : SYNC_AHEAD;
    sb_copy_ring((WORD)(hw % g_dma_len), n);
}

static void sb_stop(void)
{
    if (!g_sb_playing) return;
    sb_write(0xD0);                                 /* pauza 8-bit DMA */
    if (g_sb_autoinit) sb_write(g_dsp_major >= 4 ? 0xD9 : 0xDA);  /* koniec auto-init */
    sb_write(0xD3);                                 /* glosnik off */
    io_outb(0x0A, (BYTE)(0x04 | g_sb_dma));         /* maskuj kanal */
    (void)io_inb(SB_READ_STATUS);
    if (g_dma_sel) {
        fast_fill(MK_FP(g_dma_sel, 0), 0x80, 0x8000);
        fast_fill(MK_FP(g_dma_sel, 0x8000), 0x80, 0x8000);
    }
    g_sb_playing = FALSE;
}

static void sb_start(BOOL looping, DWORD buf_len)
{
    WORD dma_cnt, blk;
    if (!sb_init_once()) return;
    if (looping && g_dsp_major < 2) looping = FALSE;    /* SB 1.x: brak auto-init */
    sb_stop();
    sb_reset();
    if (!buf_len) return;
    g_dma_len = buf_len > 0x10000UL ? 0x10000UL : buf_len;
    if (looping && buf_len > 0x10000UL) {
        /* pierscien gry dluzszy niz kanal DMA: bez wsparcia, zostaje zegar systemowy */
        sb_disable("bufor petli > 64KB");
        return;
    }
    g_dma_ever_moved = FALSE;
    sb_copy_ring(0, (WORD)(g_dma_len - 1));                   /* dlugosc do 64KB (WORD) */
    sb_copy_ring((WORD)(g_dma_len - 1), 1);

    dma_cnt = (WORD)(g_dma_len - 1);
    io_outb(0x0A, (BYTE)(0x04 | g_sb_dma));                   /* maskuj */
    io_outb(0x0C, 0x00);                                      /* flip-flop */
    io_outb(0x0B, (BYTE)((looping ? 0x58 : 0x48) | g_sb_dma)); /* single, inc, [auto], odczyt */
    io_outb(dma_page_port(), (BYTE)((g_dma_phys >> 16) & 0xFF));
    io_outb(dma_addr_port(), 0x00);                           /* 64KB wyrownany */
    io_outb(dma_addr_port(), 0x00);
    io_outb(dma_count_port(), (BYTE)(dma_cnt & 0xFF));
    io_outb(dma_count_port(), (BYTE)(dma_cnt >> 8));
    io_outb(0x0A, (BYTE)g_sb_dma);                            /* odmaskuj */

    sb_write(0xD1);                                           /* glosnik on */
    blk = looping ? (WORD)(g_dma_len / 2 - 1) : dma_cnt;      /* IRQ co pol pierscienia */
    if (g_dsp_major >= 4) {
        /* SB16: czestotliwosc wprost, 0xC6/0xC0 */
        sb_write(0x41);
        sb_write((BYTE)((g_wo_rate >> 8) & 0xFF));
        sb_write((BYTE)(g_wo_rate & 0xFF));
        sb_write(looping ? 0xC6 : 0xC0);
        sb_write(g_wo_channels >= 2 ? 0x20 : 0x00);           /* 8-bit unsigned mono/stereo */
        sb_write((BYTE)(blk & 0xFF));
        sb_write((BYTE)(blk >> 8));
    } else {
        /* SB 2.0 / SB Pro / ES688/ES1688: stala czasu, rozmiar bloku 0x48, start 0x1C/0x14 */
        DWORD rate = g_wo_rate * (g_wo_channels >= 2 ? 2UL : 1UL);
        BYTE tc = (BYTE)(256UL - 1000000UL / (rate ? rate : 22050UL));
        if (g_wo_channels >= 2 && g_dsp_major >= 3) {         /* SB Pro: stereo w mikserze */
            BYTE m;
            io_outb(g_sb_base + 4, 0x0E);
            m = io_inb(g_sb_base + 5);
            io_outb(g_sb_base + 5, (BYTE)(m | 0x02));
        }
        sb_write(0x40);
        sb_write(tc);
        if (looping) {
            sb_write(0x48);
            sb_write((BYTE)(blk & 0xFF));
            sb_write((BYTE)(blk >> 8));
            sb_write(0x1C);                                   /* 8-bit auto-init */
        } else {
            sb_write(0x14);                                   /* 8-bit single-cycle */
            sb_write((BYTE)(blk & 0xFF));
            sb_write((BYTE)(blk >> 8));
        }
    }

    g_sb_autoinit = looping;
    g_dma_last_hw = 0;
    g_dma_wraps = 0;
    g_dma_moved_tick = GetTickCount();
    g_sync_hw = 0;
    g_sb_playing = TRUE;
}

static void wo_notify(WORD uMsg, DWORD p1, DWORD p2)
{
    DWORD cbt = g_wo_flags & CALLBACK_TYPEMASK;
    if (!g_wo_callback) return;
    if (cbt == CALLBACK_FUNCTION) {
        WAVECALLBACK fn = (WAVECALLBACK)g_wo_callback;
        fn(1, uMsg, g_wo_instance, p1, p2);
    } else if (cbt == CALLBACK_WINDOW) {
        PostMessage((HWND)(WORD)g_wo_callback, uMsg, 1, (LONG)p1);
    }
}

BOOL API sndPlaySound(LPCSTR s, WORD fl) { (void)s; (void)fl; return FALSE; }
BOOL API PlaySound(LPCSTR s, WORD m, DWORD fl) { (void)s; (void)m; (void)fl; return FALSE; }
WORD API waveOutGetNumDevs(void) { return 1; }
WORD API midiOutGetNumDevs(void) { return 0; }
WORD API auxGetNumDevs(void)     { return 0; }
DWORD API timeGetTime(void)      { return GetTickCount(); }

/* ordinal 101 */
WORD API joyGetNumDevs(void) { return 0; }

/* ordinal 103 */
WORD API joyGetPos(WORD id, LPVOID lpInfo)
{
    (void)id; (void)lpInfo;
    return JOYERR_UNPLUGGED;
}

/* ordinal 402 */
WORD API waveOutGetDevCaps(WORD id, LPVOID lpCaps, WORD uSize)
{
    WAVEOUTCAPS __far *c = (WAVEOUTCAPS __far *)lpCaps;
    (void)id;
    if (!c || uSize < sizeof(WAVEOUTCAPS)) return MMSYSERR_INVALHANDLE;
    c->wMid = 1;
    c->wPid = 1;
    c->vDriverVersion = 0x0100;
    w_strncpy(c->szPname, "Sound Blaster 16", 32);
    c->dwFormats = 0x00000FFFUL;
    c->wChannels = 2;
    c->dwSupport = 0;
    return MMSYSERR_NOERROR;
}

/* ordinal 404 */
WORD API waveOutOpen(WORD __far *lphWaveOut, WORD uDeviceID, LPVOID lpFormat,
                     DWORD dwCallback, DWORD dwInstance, DWORD dwFlags)
{
    PCMWAVEFORMAT __far *wf = (PCMWAVEFORMAT __far *)lpFormat;
    dbg("MMSYSTEM: waveOutOpen dev="); dbg_hex(uDeviceID, 4);
    dbg(" cb="); dbg_hex(dwCallback, 8);
    dbg(" fl="); dbg_hex(dwFlags, 8);
    if (wf) {
        dbg(" rate="); dbg_hex(wf->nSamplesPerSec, 8);
        dbg(" bps="); dbg_hex(wf->nAvgBytesPerSec, 8);
        g_wo_rate = wf->nSamplesPerSec ? wf->nSamplesPerSec : 22050;
        g_wo_bps = wf->nAvgBytesPerSec ? wf->nAvgBytesPerSec : g_wo_rate;
        g_wo_channels = wf->nChannels ? wf->nChannels : 1;
        g_wo_align = wf->nBlockAlign ? wf->nBlockAlign : 1;
    }
    dbg("\n");
    if (dwFlags & 0x0001) /* WAVE_FORMAT_QUERY */
        return MMSYSERR_NOERROR;
    sb_init_once();
    g_wo_open = TRUE;
    g_wo_looping = FALSE;
    g_wo_callback = dwCallback;
    g_wo_instance = dwInstance;
    g_wo_flags = dwFlags;
    g_wo_start_tick = GetTickCount();
    g_wo_written_bytes = 0;
    g_wo_src = NULL;
    g_wo_src_len = 0;
    if (lphWaveOut) *lphWaveOut = 1;
    wo_notify(WOM_OPEN, 0, 0);
    return MMSYSERR_NOERROR;
}

/* ordinal 405 */
WORD API waveOutClose(WORD hWaveOut)
{
    dbg("MMSYSTEM: waveOutClose h="); dbg_hex(hWaveOut, 4); dbg("\n");
    if (!g_wo_open || hWaveOut != 1) return MMSYSERR_INVALHANDLE;
    sb_stop();
    wo_notify(WOM_CLOSE, 0, 0);
    g_wo_open = FALSE;
    return MMSYSERR_NOERROR;
}

/* ordinal 406 */
WORD API waveOutPrepareHeader(WORD hWaveOut, LPVOID lpWaveOutHdr, WORD uSize)
{
    WAVEHDR __far *wh = (WAVEHDR __far *)lpWaveOutHdr;
    (void)uSize;
    if (!g_wo_open || hWaveOut != 1 || !wh) return MMSYSERR_INVALHANDLE;
    wh->dwFlags |= WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

/* ordinal 407 */
WORD API waveOutUnprepareHeader(WORD hWaveOut, LPVOID lpWaveOutHdr, WORD uSize)
{
    WAVEHDR __far *wh = (WAVEHDR __far *)lpWaveOutHdr;
    (void)uSize;
    if (!g_wo_open || hWaveOut != 1 || !wh) return MMSYSERR_INVALHANDLE;
    wh->dwFlags &= ~(DWORD)WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

/* ordinal 408 */
WORD API waveOutWrite(WORD hWaveOut, LPVOID lpWaveOutHdr, WORD uSize)
{
    WAVEHDR __far *wh = (WAVEHDR __far *)lpWaveOutHdr;
    BOOL looping;
    (void)uSize;
    if (!g_wo_open || hWaveOut != 1 || !wh) return MMSYSERR_INVALHANDLE;
    if (g_wo_written_bytes == 0) g_wo_start_tick = GetTickCount();
    g_wo_written_bytes += wh->dwBufferLength;
    g_wo_src = wh->lpData;
    g_wo_src_len = wh->dwBufferLength;
    looping = (wh->dwFlags & 0x0C) && wh->dwLoops > 1;
    if (g_wo_written_bytes == wh->dwBufferLength) {
        dbg("MMSYSTEM: waveOutWrite len="); dbg_hex(wh->dwBufferLength, 8);
        dbg(looping ? " petla\n" : " jednorazowo\n");
    }
    if (looping) {
        g_wo_looping = TRUE;
        wh->dwFlags = (wh->dwFlags & ~(DWORD)WHDR_DONE) | WHDR_INQUEUE;
        sb_start(TRUE, wh->dwBufferLength);
    } else {
        sb_start(FALSE, wh->dwBufferLength);
        wh->dwFlags = (wh->dwFlags & ~(DWORD)WHDR_INQUEUE) | WHDR_DONE;
        wo_notify(WOM_DONE, (DWORD)wh, 0);
    }
    return MMSYSERR_NOERROR;
}

/* ordinal 411 */
WORD API waveOutReset(WORD hWaveOut)
{
    dbg("MMSYSTEM: waveOutReset h="); dbg_hex(hWaveOut, 4); dbg("\n");
    if (!g_wo_open || hWaveOut != 1) return MMSYSERR_INVALHANDLE;
    sb_stop();
    g_wo_looping = FALSE;
    g_wo_start_tick = GetTickCount();
    g_wo_written_bytes = 0;
    g_wo_src = NULL;
    g_wo_src_len = 0;
    g_last_ms = 0;
    g_last_bytes = 0;
    return MMSYSERR_NOERROR;
}

/* ordinal 412 */
WORD API waveOutGetPosition(WORD hWaveOut, LPVOID lpInfo, WORD uSize)
{
    MMTIME __far *mt = (MMTIME __far *)lpInfo;
    DWORD elapsed_ms, bytes;
    (void)uSize;
    if (!g_wo_open || hWaveOut != 1 || !mt) return MMSYSERR_INVALHANDLE;

    if (g_sb_playing && (g_poll++ & 63) && g_sb_autoinit) {
        /* Gry odpytuja pozycje w petli (Woodruff ~19000/s). Sprzet czytamy co 8. raz:
         * kazdy odczyt to ~7 operacji na portach ISA (wolne na 486, a w QEMU z oknem GTK
         * kazde wyjscie z KVM czeka na watek ekranu i zabiera czas odczytom dysku). */
        bytes = g_last_bytes;
        elapsed_ms = g_last_ms;
    } else if (g_sb_playing) {
        DWORD now = GetTickCount();
        WORD hw;
        (void)io_inb(SB_READ_STATUS);   /* ack ewentualnego IRQ 8-bit */
        hw = dma_hw_pos();
        if (hw != g_dma_last_hw) {
            /* zawiniecie pierscienia = duzy skok w dol (nie drobny rozjazd odczytu) */
            if (g_sb_autoinit && hw < g_dma_last_hw && (DWORD)(g_dma_last_hw - hw) > g_dma_len / 2)
                g_dma_wraps++;
            else if (hw < g_dma_last_hw && (DWORD)(g_dma_last_hw - hw) <= g_dma_len / 2) {
                /* odczyt sprzed chwili byl o kilka bajtow za wysoki - pozycja nie cofa sie */
                hw = g_dma_last_hw;
            }
            g_dma_last_hw = hw;
            g_dma_moved_tick = now;
            g_dma_ever_moved = TRUE;
        } else if ((g_sb_autoinit || !g_dma_ever_moved) && now - g_dma_moved_tick > STALL_MS) {
            /* karta nie pobiera danych (zle DMA/port, brak backendu audio w emulatorze):
             * dalej odmierzamy czas zegarem, inaczej wideo .VMD stoi na pierwszej klatce */
            sb_stop();
            sb_disable("licznik DMA stoi");
            g_wo_start_tick = now - g_last_ms;
            goto clock;
        }
        if (g_sb_autoinit) {
            if ((WORD)(hw - g_sync_hw) >= 0x800 || hw < g_sync_hw) {   /* co >= 2KB odtwarzania */
                sb_sync_ahead(hw);
                g_sync_hw = hw;
            }
            bytes = g_dma_wraps * g_dma_len + hw;
        } else {
            /* single-cycle: po koncu licznik stoi na koncu bufora (lub zawinal do 0) */
            bytes = (g_dma_ever_moved && (hw == 0 || now - g_dma_moved_tick > 50)) ? g_dma_len : hw;
        }
        elapsed_ms = (bytes / g_wo_bps) * 1000UL + ((bytes % g_wo_bps) * 1000UL) / g_wo_bps;
    } else {
    clock:
        elapsed_ms = GetTickCount() - g_wo_start_tick;
        bytes = (elapsed_ms / 1000UL) * g_wo_bps + ((elapsed_ms % 1000UL) * g_wo_bps) / 1000UL;
        if (!g_wo_looping && g_wo_written_bytes && bytes > g_wo_written_bytes)
            bytes = g_wo_written_bytes;
    }
    g_last_ms = elapsed_ms;
    g_last_bytes = bytes;

    if (mt->wType == TIME_MS) {
        mt->u.ms = elapsed_ms;
    } else if (mt->wType == TIME_SAMPLES) {
        mt->u.sample = bytes / g_wo_align;
    } else {
        mt->wType = TIME_BYTES;
        mt->u.cb = bytes;
    }
    return MMSYSERR_NOERROR;
}

int API LibMain(WORD hi, WORD ds, WORD heap, LPCSTR cmd) { (void)hi; (void)ds; (void)heap; (void)cmd; return 1; }
