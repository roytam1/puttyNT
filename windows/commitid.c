/*
 * commitid.c: the upstream PuTTY git commit this source is based on.
 *
 * Upstream CMake writes this at build time (cmake_commit.c). The NT
 * port has no git or CMake at build time, so it is fixed here: it is
 * the commit of the 0.85 release, with the NT port changes on top.
 */

#include "putty.h"

const char commitid[] = "a3cce3a48d4ab12e0fccb32998b9042d4f7c3ff1 + NT port";
