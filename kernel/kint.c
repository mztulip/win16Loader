/* kint.c - KERNEL: przerwania programowe wolane przez apki w PM
 *
 * pm_call.asm (softint_common) przekazuje tu kazdy INT nieobslugiwany inaczej.
 * INT 21h  -> ta sama translacja co DOS3Call (kdos.c),
 * INT 25h/26h -> odczyt/zapis sektorow przez bufor RM,
 * pozostale -> odbicie rejestrow do trybu rzeczywistego (bez wskaznikow).
 */
#include "kernel.h"

typedef struct { WORD bx, cx, dx, si, di, es, ds, bp, ax, ip, cs, flags; } INTFRAME;
typedef struct { WORD ax, bx, cx, dx, si, di, ds, es, flags; } D3REGS;
extern D3REGS g_d3;
void dos3call_c(void);

extern void __far int_entry(void);

void kint_init(void)
{
    KCBD(538) = (DWORD)(void __far *)int_entry;
}

static void frame_to_rm(INTFRAME __far *f, RMREGS __far *r)
{
    kmemset(r, 0, sizeof(RMREGS));
    r->ax = f->ax; r->bx = f->bx; r->cx = f->cx; r->dx = f->dx;
    r->si = f->si; r->di = f->di; r->bp = f->bp;
}

static void rm_to_frame(RMREGS __far *r, INTFRAME __far *f)
{
    f->ax = r->ax; f->bx = r->bx; f->cx = r->cx; f->dx = r->dx;
    f->si = r->si; f->di = r->di;
    f->flags = (f->flags & ~0x08D5) | (r->flags & 0x08D5);   /* CF PF AF ZF SF OF */
}

/* INT 25h/26h: AL=naped, CX=liczba sektorow (0xFFFF -> pakiet w DS:BX),
 * DX=sektor startowy, DS:BX=bufor */
static WORD absdisk(INTFRAME __far *f, BYTE vec)
{
    RMREGS __far *r = rmregs();
    BYTE __far *buf;
    DWORD start;
    WORD count, done = 0;
    BOOL packet = f->cx == 0xFFFF;
    if (packet) {
        BYTE __far *p = (BYTE __far *)MK_FP(f->ds, f->bx);
        start = *(DWORD __far *)p;
        count = *(WORD __far *)(p + 4);
        buf = (BYTE __far *)MK_FP(*(WORD __far *)(p + 8), *(WORD __far *)(p + 6));
    } else {
        start = f->dx;
        count = f->cx;
        buf = (BYTE __far *)MK_FP(f->ds, f->bx);
    }
    while (done < count) {
        WORD n = count - done > 64 ? 64 : count - done;
        BYTE __far *pk = (BYTE __far *)MK_FP(SEL_RMBUF, RMBUF_DATA);
        if (vec == 0x26) kmemcpy(pk + 16, buf + done * 512u, n * 512u);
        *(DWORD __far *)pk = start + done;          /* pakiet: zawsze forma CX=FFFF */
        *(WORD __far *)(pk + 4) = n;
        *(WORD __far *)(pk + 6) = 16 + RMBUF_DATA;
        *(WORD __far *)(pk + 8) = KCBW(KCB_RMBUF_SEG);
        kmemset(r, 0, sizeof(RMREGS));
        r->ax = f->ax & 0xFF;
        r->cx = 0xFFFF;
        r->bx = RMBUF_DATA;
        if (rm_int(vec, r)) {
            /* stara forma (dyski < 32MB) */
            kmemset(r, 0, sizeof(RMREGS));
            r->ax = f->ax & 0xFF;
            r->cx = n;
            r->dx = (WORD)(start + done);
            r->bx = RMBUF_DATA + 16;
            if (rm_int(vec, r)) {
                f->ax = r->ax;
                f->flags |= CF_FLAG;
                return 1;
            }
        }
        if (vec == 0x25) kmemcpy(buf + done * 512u, pk + 16, n * 512u);
        done += n;
    }
    f->flags &= ~CF_FLAG;
    return 1;
}

WORD int_dispatch_c(WORD vec, WORD ss, WORD sp)
{
    INTFRAME __far *f = (INTFRAME __far *)MK_FP(ss, sp);
    RMREGS __far *r;
    switch (vec) {
    case 0x21:
        g_d3.ax = f->ax; g_d3.bx = f->bx; g_d3.cx = f->cx; g_d3.dx = f->dx;
        g_d3.si = f->si; g_d3.di = f->di; g_d3.ds = f->ds; g_d3.es = f->es;
        g_d3.flags = f->flags;
        dos3call_c();
        f->ax = g_d3.ax; f->bx = g_d3.bx; f->cx = g_d3.cx; f->dx = g_d3.dx;
        f->si = g_d3.si; f->di = g_d3.di; f->es = g_d3.es;
        f->flags = (f->flags & ~CF_FLAG) | (g_d3.flags & CF_FLAG);
        return 0;
    case 0x25:
    case 0x26:
        return absdisk(f, (BYTE)vec);
    case 0x31:
        dbg_puts("KERNEL: INT 31h (DPMI) AX="); dbg_hex(f->ax, 4); dbg_puts(" - brak\n");
        f->flags |= CF_FLAG;
        return 0;
    default:
        r = rmregs();
        frame_to_rm(f, r);
        rm_int((BYTE)vec, r);
        rm_to_frame(r, f);
        return 0;
    }
}
