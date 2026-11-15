/*
 * ntconfig.h: build settings for the Windows NT 3.1 / 3.51 / 4.0 port.
 *
 * Upstream PuTTY gets these from cmake.h, which CMake writes after
 * probing the compiler. The NT port is built with nmake by the
 * compilers of 1993-1996, so the answers are fixed here: none of
 * the newer headers, APIs or CPU instructions are there.
 */

#ifndef PUTTY_NTCONFIG_H
#define PUTTY_NTCONFIG_H

#define NO_IPV6                 /* no IPv6 in the old SDK headers */
#define NO_GSSAPI               /* no SSPI on NT 3.x / 4.0 */
#define NO_MULTIMON             /* MonitorFromWindow: Windows 98/2000 */
#define NO_MANIFESTS            /* manifests came with Windows XP */
#define NO_HTMLHELP             /* no HTML Help (.chm) on NT 3.x / 4.0 */

#define HAVE_COUNTOF 0

#define HAVE_WINRESRC_H 0
#define HAVE_WINRES_H 0
#define HAVE_WIN_H 0
#define HAVE_AFUNIX_H 0
#define HAVE_ADDDLLDIRECTORY 0
#define HAVE_GETNAMEDPIPECLIENTPROCESSID 0
#define HAVE_SETDEFAULTDLLDIRECTORIES 0
#define HAVE_DWMAPI_H 0

/* MinGW (used for test builds on other machines) has GCP_RESULTSW */
#if defined __GNUC__
#define HAVE_GCP_RESULTSW 1
#else
#define HAVE_GCP_RESULTSW 0
#endif

#define HAVE_AES_NI 0
#define HAVE_SHA_NI 0
#define HAVE_SHAINTRIN_H 0
#define HAVE_CLMUL 0
#define HAVE_NEON_CRYPTO 0
#define HAVE_NEON_PMULL 0
#define HAVE_NEON_VADDQ_P128 0
#define HAVE_NEON_SHA512 0
#define HAVE_NEON_SHA512_INTRINSICS 0
#define USE_ARM64_NEON_H 0
#define HAVE_ARM_DIT 0

#endif /* PUTTY_NTCONFIG_H */
