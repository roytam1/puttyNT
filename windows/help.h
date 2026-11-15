/*
 * help.h - help contexts.
 *
 * The cross-platform dialog code names a section of the manual for
 * each control (HELPCTX), and some message boxes name one too
 * (HELPCTXID). This port has no help file, so they all mean "no
 * help".
 */

typedef const char *HelpCtx;
#define NULL_HELPCTX NULL
#define HELPCTX(x) NULL
#define HELPCTXID(x) 0
