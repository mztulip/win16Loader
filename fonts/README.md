# Bitmap fonts (from Wine)

Win16 bitmap fonts (`.FON`, FNT 2.0/3.0 resources in an NE container) loaded by
our GDI. The loader preloads every `*.FON` from the system directory (`A:\`),
`make` copies the files below onto `boot.img`.

| File | Face | Wine source (`wine/fonts/`) |
|------|------|-----------------------------|
| `sserife.fon` | MS Sans Serif (8/10/12 pt) | `ms_sans_serif.sfd` |
| `vgasys.fon`  | System | `system.sfd` |
| `vgafix.fon`  | Fixedsys | `fixedsys.sfd` |
| `coure.fon`   | Courier | `courier.sfd` |
| `smalle.fon`  | Small Fonts | `small_fonts.sfd` |

`sserifb.fon` is not from Wine, see below.

## Bold MS Sans Serif (`sserifb.fon`)

Wine has no bold bitmap MS Sans Serif, so GDI would embolden the regular face by adding
1 px per letter. That does not fit layouts sized for narrow text: "Start" in the Calmira
button needs 27 px with 22 px available. `sserifb.fon` is a real bold face, exactly 22 px for
"Start", which `font_match` selects for bold requests.

Rendered from **PT Sans Narrow Bold**, copyright (c) 2010 ParaType Ltd., under the SIL Open
Font License 1.1: [`OFL-PTSansNarrow.txt`](OFL-PTSansNarrow.txt).

- Font page: <https://fonts.google.com/specimen/PT+Sans+Narrow>
- Source and license: <https://github.com/google/fonts/tree/main/ofl/ptsansnarrow>
- File used: <https://raw.githubusercontent.com/google/fonts/main/ofl/ptsansnarrow/PT_Sans-Narrow-Web-Bold.ttf>

The license covers this file only. It is a different design carrying the face name
"MS Sans Serif" so applications get it by that name. Regenerate with:

```bash
tools/ttf2fon.py fonts/sserife.fon PT_Sans-Narrow-Web-Bold.ttf fonts/sserifb.fon \
    --face "MS Sans Serif" --module "MS SansSerifB" \
    --size 12:13:11:2:8 --size 14:16:13:3:10 --size 17:20:16:4:12
```

## Origin of the Wine fonts

Copied unmodified from the Wine 11.20 binary package (Arch Linux `wine 11.20-1`,
`/usr/share/wine/fonts/`). In Wine they are generated from the FontForge sources
listed above by `tools/sfnt2fon` during the build.

- Wine sources: <https://gitlab.winehq.org/wine/wine> (directory `fonts/`)
- Wine releases: <https://www.winehq.org>

To refresh them from a local Wine install:

```bash
for f in sserife vgasys vgafix coure smalle; do cp /usr/share/wine/fonts/$f.fon fonts/; done
```

## License

Copyright (C) 2004-2013 Huw D M Davies, Dmitry Timoshkov, et al. (the Wine project).

The fonts are free software; you can redistribute them and/or modify them under the
terms of the GNU Lesser General Public License as published by the Free Software
Foundation; either version 2.1 of the License, or (at your option) any later version.
See [`COPYING.LIB`](COPYING.LIB). The corresponding source (`.sfd` files) is
available from the Wine repository linked above.
