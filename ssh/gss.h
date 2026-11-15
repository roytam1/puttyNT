#ifndef PUTTY_SSHGSS_H
#define PUTTY_SSHGSS_H
#include "putty.h"
#include "pgssapi.h"

/* This struct is defined even in NO_GSSAPI mode, so that stubs/no-gss.c can
 * return an instance of it containing no libraries */
struct ssh_gss_liblist {
    struct ssh_gss_library *libraries;
    int nlibraries;
};

#endif /*PUTTY_SSHGSS_H*/
