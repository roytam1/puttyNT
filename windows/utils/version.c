#include "putty.h"

DWORD osMajorVersion;

/*
 * Find out the major version of Windows NT. GetVersion is the only
 * way that Windows NT 3.1 has (GetVersionEx came with NT 3.5).
 */
void init_winver(void)
{
    static bool initialised = false;

    if (initialised)
        return;
    initialised = true;

    osMajorVersion = LOBYTE(LOWORD(GetVersion()));
}
