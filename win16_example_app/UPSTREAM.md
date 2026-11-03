# Origin of this directory

Windows 1 Example Application by **Transmission Zero**, released into the public domain
(Unlicense, see `License.txt`).

- Repository: <https://github.com/TransmissionZero/Windows-1-Example-Application>
- Article: <https://www.transmissionzero.co.uk/computing/win16-apps-in-c/>

## Local changes (win16Loader)

- `Makefile`, `Win1App.lnk` - build with Open Watcom (`wcc`, `wrc`, `wlink`) instead of
  Microsoft C 4 + Windows 1 SDK
- `Resource.rc` - the Windows 1.x icon is commented out (not accepted by `wrc`),
  copyright character fixed
- `CLAUDE.md` - notes for working on this directory
