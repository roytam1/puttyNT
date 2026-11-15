# PuTTY NT

[PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/) 0.85, the SSH and Telnet client, for Windows NT 3.1, 3.51 and 4.0 on Intel x86, MIPS, Alpha and PowerPC.

Upstream PuTTY needs CMake, a C99 compiler and a newer Windows. This port is C89 with one nmake Makefile. It builds with the original NT compilers of 1993 to 1996 and runs on the first versions of Windows NT.

## Download

Get the zip for your NT version and CPU from [Releases](https://github.com/tenox7/puttyNT/releases). Each zip has all eight programs: PuTTY, PuTTYtel, Plink, PSCP, PSFTP, PSOCKS, Pageant and PuTTYgen.

| | x86 | MIPS | Alpha | PowerPC |
|---|---|---|---|---|
| NT 3.1 | `nt31-x86` | `nt31-mips` | `nt31-alpha` | |
| NT 3.51 | `nt351-x86` | `nt351-mips` | `nt351-alpha` | `nt351-ppc` |
| NT 4.0 | `nt40-x86` | `nt40-mips` | `nt40-alpha` | `nt40-ppc` |

NT 3.1 has no PowerPC version. Each set was built on its own NT version and CPU, and tested there, in emulators.

## Differences from upstream PuTTY

- No Kerberos (GSSAPI), no IPv6 and no help file. NT 3.x and 4.0 don't have them.
- Bignum arithmetic uses 16-bit words, because the compilers of 1993 have no 64-bit integer type. So key exchange and key generation are slower.
- The MIPS programs are not optimised. Both MIPS compilers (NT 3.1 SDK and Visual C++ 4.x) make wrong code for some functions when they optimise, the AES S-box among them. So the MIPS programs are about twice as big, and slower.
- Both MIPS compilers also lose the cast in `(unsigned short)(-x)` and `(unsigned short)(~x)`. `crypto/mpint.c` and `crypto/mpint_i.h` work around it.

## Build

Open a command prompt set up for your compiler, go to the source folder and run nmake:

| NT | Compiler | Command |
|---|---|---|
| 3.1 | Win32 SDK for Windows NT 3.1 (cl386, mcl, claxp) | `nmake` |
| 3.51 | Visual C++ 4.x | `nmake APPVER=3.51` |
| 4.0 | Visual C++ 4.x | `nmake` |

The `CPU` environment variable (`i386`, `MIPS`, `ALPHA` or `PPC`) picks the processor. `nmake clean` deletes what nmake made. Visual C++ 2.0 builds it too.

## Testing

- PuTTY's own crypto tests (testcrypt): 222,037 tests recorded on x86, then run with each of the 7 compilers. All results match. (A few BLAKE2b answers differ only in buffer bytes that the test reads without filling.)
- Pageant unlocks a passphrase-protected Ed25519 key on all 11 builds.

## Source

The layout is the same as upstream PuTTY, without the Unix port, CMake, the documentation and the tests. The git history starts with the PuTTY 0.85 source zip as released. The next commit removes what NT doesn't use, and the one after it is the port. So every change can be seen.

## License

MIT, the same as PuTTY. See [LICENCE](LICENCE).
