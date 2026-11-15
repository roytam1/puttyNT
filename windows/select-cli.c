/*
 * Network activity for the command-line tools (Plink, PSCP, PSFTP,
 * PSocks).
 *
 * They use the same method as the GUI tools (select-gui.c, which
 * provides do_select): WSAAsyncSelect turns network activity into
 * messages to a window. Here that window is never shown. WinSock 1.1,
 * which is all that Windows NT 3.1 and 3.5 have, has no other way to
 * report network activity that a program can wait for together with
 * its other handles. cli_main_loop waits with
 * MsgWaitForMultipleObjects and dispatches the messages.
 *
 * A program that Windows NT 3.1 starts from a service, such as a
 * remote shell daemon, can't have a window at all (RegisterClass
 * fails with ERROR_NOT_ENOUGH_MEMORY). Then cli_main_loop looks at the
 * sockets itself, with sk_poll, every few milliseconds.
 */

#include "putty.h"

static LRESULT CALLBACK winselcli_wndproc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NETEVENT || message == WM_DONE_WITH_SOCKET) {
        if (message == WM_NETEVENT)
            noise_ultralight(NOISE_SOURCE_IOID, wParam);
        winselgui_response(message, wParam, lParam);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

static bool no_window = false;

void winselcli_setup(void)
{
    static bool done = false;
    WNDCLASS wc;
    HWND hwnd = NULL;

    if (done)
        return;
    done = true;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = winselcli_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "PuTTYNetworkEvents";
    if (RegisterClass(&wc))
        hwnd = CreateWindow("PuTTYNetworkEvents", "", 0, 0, 0, 0, 0,
                            NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hwnd)
        winselgui_set_hwnd(hwnd);
    else
        no_window = true;
}

/* True if cli_main_loop has to poll the sockets (see above) */
bool winselcli_polling(void)
{
    return no_window;
}
