/*
 * Return the full pathname of the global mutex that Pageant uses at
 * startup to atomically decide whether to be a server or a client.
 *
 * (NT port: upstream puts it in the "Local\" namespace, which only
 * Windows 2000 and later have; Windows NT 3.x and 4.0 fail to create
 * it with ERROR_PATH_NOT_FOUND. Without the prefix, 2000 and later
 * put the name in the session's own namespace all the same.)
 */

#include "putty.h"

char *agent_mutex_name(void)
{
    char *username = get_username();
    char *mutexname = dupprintf("pageant-mutex.%s", username);
    sfree(username);
    return mutexname;
}
