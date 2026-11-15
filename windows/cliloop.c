#include "putty.h"

/*
 * Without a window for network events (see select-cli.c), wake up at
 * least this often, in milliseconds, to look at the sockets.
 */
#define POLL_INTERVAL 20

void cli_main_loop(cliloop_pre_t pre, cliloop_post_t post, void *ctx)
{
    unsigned long now, next, then;
    bool polling = winselcli_polling();
    now = GETTICKCOUNT();

    while (true) {
        DWORD n;
        DWORD ticks, wait;
        bool netbusy;

        const HANDLE *extra_handles = NULL;
        size_t n_extra_handles = 0;
        HandleWaitList *hwl;
        size_t extra_base;
        size_t total_handles;
        size_t extra_handle_index;
        size_t i;
        MSG msg;
        if (!pre(ctx, &extra_handles, &n_extra_handles))
            break;

        /* Without a window for network events (see select-cli.c),
         * look at the sockets now, before taking the handle list,
         * which the events can change */
        netbusy = polling && sk_poll();

        if (toplevel_callback_pending()) {
            ticks = 0;
            next = now;
        } else if (run_timers(now, &next)) {
            then = now;
            now = GETTICKCOUNT();
            if (now - then > next - then)
                ticks = 0;
            else
                ticks = next - now;
        } else {
            ticks = INFINITE;
            /* no need to initialise next here: an INFINITE wait never
             * times out, and after a shorter one we read the clock */
        }

        hwl = get_handle_wait_list();
        extra_base = hwl->nhandles;
        total_handles = extra_base + n_extra_handles;
        assert(total_handles < MAXIMUM_WAIT_OBJECTS);
        for (i = 0; i < n_extra_handles; i++)
            hwl->handles[extra_base + i] = extra_handles[i];

        if (!polling) {
            /* Network activity arrives as window messages (see
             * select-cli.c) */
            wait = ticks;
            n = MsgWaitForMultipleObjects(total_handles, hwl->handles,
                                          false, wait, QS_ALLINPUT);
        } else {
            /* No window: wait only a short while, so as to look at the
             * sockets again soon, or not at all if they had something */
            if (netbusy)
                wait = 0;
            else
                wait = ticks < POLL_INTERVAL ? ticks : POLL_INTERVAL;
            if (total_handles > 0) {
                n = WaitForMultipleObjects(total_handles, hwl->handles,
                                           false, wait);
            } else {
                Sleep(wait);
                n = WAIT_TIMEOUT;
            }
        }

        extra_handle_index = n_extra_handles;

        if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)hwl->nhandles) {
            handle_wait_activate(hwl, n - WAIT_OBJECT_0);
        } else if (n >= WAIT_OBJECT_0 + extra_base &&
                   n < WAIT_OBJECT_0 + total_handles) {
            extra_handle_index = n - (WAIT_OBJECT_0 + extra_base);
        }

        if (!polling) {
            /* Pass every waiting message to its window procedure,
             * which queues the network events as toplevel callbacks */
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessage(&msg);
        }

        run_toplevel_callbacks();

        if (n == WAIT_TIMEOUT && wait == ticks) {
            now = next;
        } else {
            now = GETTICKCOUNT();
        }

        handle_wait_list_free(hwl);

        if (!post(ctx, extra_handle_index))
            break;
    }
}

bool cliloop_null_pre(void *vctx, const HANDLE **eh, size_t *neh)
{ return true; }
bool cliloop_null_post(void *vctx, size_t ehi) { return true; }
