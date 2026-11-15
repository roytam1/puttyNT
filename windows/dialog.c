/*
 * dialog.c - dialogs for PuTTY(tel), including the configuration dialog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#include "putty.h"
#include "ssh.h"
#include "putty-rc.h"
#include "win-gui-seat.h"
#include "storage.h"
#include "dialog.h"
#include "licence.h"

#include <shellapi.h>

typedef struct PortableDialogStuff {
    /*
     * These are the various bits of data required to handle a dialog
     * box that's been built up from the cross-platform dialog.c
     * system.
     */

    /* The 'controlbox' that was returned from the portable setup function */
    struct controlbox *ctrlbox;

    /* The dlgparam that's passed to all the runtime dlg_* functions.
     * Declared as an array of 1 so it's convenient to pass it as a pointer. */
    struct dlgparam dp[1];

    /*
     * Collections of instantiated controls. There can be more than
     * one of these, because sometimes you want to destroy and
     * recreate a subset of them - e.g. when switching panes in the
     * main PuTTY config box, you delete and recreate _most_ of the
     * controls, but not the OK and Cancel buttons at the bottom.
     */
    size_t nctrltrees;
    struct winctrls *ctrltrees;

    /*
     * Flag indicating whether the dialog box has been initialised.
     * Used to suppresss spurious firing of message handlers during
     * setup.
     */
    bool initialised;

    /* The path of the panel the configuration box shows */
    char *panelpath;
} PortableDialogStuff;

/*
 * Initialise a PortableDialogStuff, before launching the dialog box.
 */
static PortableDialogStuff *pds_new(size_t nctrltrees)
{
    PortableDialogStuff *pds = snew(PortableDialogStuff);
    size_t i;
    memset(pds, 0, sizeof(*pds));

    pds->ctrlbox = ctrl_new_box();

    dp_init(pds->dp);

    pds->nctrltrees = nctrltrees;
    pds->ctrltrees = snewn(pds->nctrltrees, struct winctrls);
    for (i = 0; i < pds->nctrltrees; i++) {
        winctrl_init(&pds->ctrltrees[i]);
        dp_add_tree(pds->dp, &pds->ctrltrees[i]);
    }

    pds->dp->errtitle = dupprintf("%s Error", appname);

    pds->initialised = false;

    return pds;
}

static void pds_free(PortableDialogStuff *pds)
{
    size_t i;
    ctrl_free_box(pds->ctrlbox);

    dp_cleanup(pds->dp);

    for (i = 0; i < pds->nctrltrees; i++)
        winctrl_cleanup(&pds->ctrltrees[i]);
    sfree(pds->ctrltrees);

    sfree(pds);
}

static BOOL pds_default_dlgproc(PortableDialogStuff *pds, HWND hwnd,
                                UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_LBUTTONUP:
        /*
         * Button release should trigger WM_OK if there was a
         * previous double click on the host CA list.
         */
        ReleaseCapture();
        if (pds->dp->ended)
            ShinyEndDialog(hwnd, pds->dp->endresult ? 1 : 0);
        break;
      case WM_COMMAND:
      case WM_DRAWITEM:
      default: {                       /* also handle drag list msg here */
        /*
         * Only process WM_COMMAND once the dialog is fully formed.
         */
        int ret;
        if (pds->initialised) {
            ret = winctrl_handle_command(pds->dp, msg, wParam, lParam);
            if (pds->dp->ended && GetCapture() != hwnd)
                ShinyEndDialog(hwnd, pds->dp->endresult ? 1 : 0);
        } else
            ret = 0;
        return ret;
      }
      case WM_CLOSE:
        ShinyEndDialog(hwnd, 0);
        return 0;

        /* Grrr Explorer will maximize Dialogs! */
      case WM_SIZE:
        if (wParam == SIZE_MAXIMIZED)
            force_normal(hwnd);
        return 0;

    }
    return 0;
}

static void pds_initdialog_start(PortableDialogStuff *pds, HWND hwnd)
{
    pds->dp->hwnd = hwnd;

    if (pds->dp->wintitle)     /* apply override title, if provided */
        SetWindowText(hwnd, pds->dp->wintitle);
}

/*
 * Create the panelfuls of controls in the configuration box.
 */
static void pds_create_controls(
    PortableDialogStuff *pds, size_t which_tree, int base_id,
    int left, int right, int top, char *path)
{
    struct ctlpos cp;
    int index;

    ctlposinit(&cp, pds->dp->hwnd, left, right, top);

    for (index = -1; (index = ctrl_find_path(
                              pds->ctrlbox, path, index)) >= 0 ;) {
        struct controlset *s = pds->ctrlbox->ctrlsets[index];
        winctrl_layout(pds->dp, &pds->ctrltrees[which_tree], &cp, s, &base_id);
    }
}

static void pds_initdialog_finish(PortableDialogStuff *pds)
{
    /*
     * Set focus into the first available control in ctrltree #0,
     * which the caller was expected to set up to be the one
     * containing the dialog controls likely to be used first.
     */
    struct winctrl *c;
    int i;
    for (i = 0; (c = winctrl_findbyindex(&pds->ctrltrees[0], i)) != NULL;
         i++) {
        if (c->ctrl) {
            dlg_set_focus(c->ctrl, pds->dp);
            break;
        }
    }

    /*
     * Now we've finished creating our initial set of controls,
     * it's safe to actually show the window without risking setup
     * flicker.
     */
    ShowWindow(pds->dp->hwnd, SW_SHOWNORMAL);

    pds->initialised = true;
}

#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

static char *events_initial[LOGEVENT_INITIAL_MAX];
static char *events_circular[LOGEVENT_CIRCULAR_MAX];
static int ninitial = 0, ncircular = 0, circular_first = 0;

#define PRINTER_DISABLED_STRING "None (printing disabled)"

void force_normal(HWND hwnd)
{
    static bool recurse = false;

    WINDOWPLACEMENT wp;

    if (recurse)
        return;
    recurse = true;

    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
        wp.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(hwnd, &wp);
    }
    recurse = false;
}

static char *getevent(int i)
{
    if (i < ninitial)
        return events_initial[i];
    if ((i -= ninitial) < ncircular)
        return events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX];
    return NULL;
}

static HWND logbox;
HWND event_log_window(void) { return logbox; }

static void update_logbox_horizontal_extent(HWND logbox)
{
    /* Find the width of every entry in the current Event Log, and set
     * the horizontal scrollbar so that you can scroll right to see
     * all of them. */
    HWND listbox = GetDlgItem(logbox, IDN_LIST);
    HDC hdc = GetDC(listbox);
    int count = SendMessage(listbox, LB_GETCOUNT, 0, 0);
    strbuf *sb = strbuf_new();
    WPARAM maxwidth = 0;
    int i;
    for (i = 0; i < count; i++) {
        size_t len = SendMessage(listbox, LB_GETTEXTLEN, i, 0);
        SIZE size;
        strbuf_clear(sb);
        SendMessage(listbox, LB_GETTEXT, i, (LPARAM)strbuf_append(sb, len));
        if (GetTextExtentPoint(hdc, sb->s, sb->len, &size)) {
            if (maxwidth < size.cx)
                maxwidth = size.cx;
        }
    }
    strbuf_free(sb);
    ReleaseDC(listbox, hdc);

    /*
     * This doesn't seem to set _exactly_ the right width. If I scroll
     * the scrollbar right as far as it will go, I find I end up with
     * a bit of extra space on the right of the longest piece of text.
     * I don't know why that is.
     *
     * Testing with debug(), GetTextExtentPoint seems to be correctly
     * returning the exact width in pixels of each log entry's text.
     * And the docs for LB_SETHORIZONTALEXTENT say that it wants a
     * width in pixels too. So I don't think it can be the usual
     * problem of confusing logical dialog units and pixels. (In any
     * case, on the high-DPI display where I tested, the difference
     * between those is more than a factor of 2, whereas I'm seeing a
     * discrepancy of only about 10%.)
     */
    SendMessage(listbox, LB_SETHORIZONTALEXTENT, maxwidth, 0);
}

static BOOL CALLBACK LogProc(HWND hwnd, UINT msg,
                             WPARAM wParam, LPARAM lParam)
{
    int i;

    switch (msg) {
      case WM_INITDIALOG: {
        char *str = dupprintf("%s Event Log", appname);
        static int tabs[4] = { 78, 108 };
        SetWindowText(hwnd, str);
        sfree(str);

        SendDlgItemMessage(hwnd, IDN_LIST, LB_SETTABSTOPS, 2,
                           (LPARAM) tabs);

        for (i = 0; i < ninitial; i++)
            SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
                               0, (LPARAM) events_initial[i]);
        for (i = 0; i < ncircular; i++)
            SendDlgItemMessage(hwnd, IDN_LIST, LB_ADDSTRING,
                               0, (LPARAM) events_circular[(circular_first + i) % LOGEVENT_CIRCULAR_MAX]);
        update_logbox_horizontal_extent(hwnd);

        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            logbox = NULL;
            SetActiveWindow(GetParent(hwnd));
            DestroyWindow(hwnd);
            return 0;
          case IDN_COPY:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED) {
                int selcount;
                int *selitems;
                selcount = SendDlgItemMessage(hwnd, IDN_LIST,
                                              LB_GETSELCOUNT, 0, 0);
                if (selcount == 0) {   /* don't even try to copy zero items */
                    MessageBeep(0);
                    break;
                }

                selitems = snewn(selcount, int);
                if (selitems) {
                    int count = SendDlgItemMessage(hwnd, IDN_LIST,
                                                   LB_GETSELITEMS,
                                                   selcount,
                                                   (LPARAM) selitems);
                    static const unsigned char sel_nl[] = SEL_NL;
                    strbuf *sb;

                    if (count == 0) {  /* can't copy zero stuff */
                        MessageBeep(0);
                        break;
                    }

                    sb = strbuf_new();
                    for (i = 0; i < count; i++) {
                        char *q = getevent(selitems[i]);
                        put_datapl(sb, ptrlen_from_asciz(q));
                        put_data(sb, sel_nl, sizeof(sel_nl));
                    }
                    write_aclip(hwnd, CLIP_SYSTEM, sb->s, sb->len);
                    strbuf_free(sb);
                    sfree(selitems);

                    for (i = 0; i < (ninitial + ncircular); i++)
                        SendDlgItemMessage(hwnd, IDN_LIST, LB_SETSEL,
                                           false, i);
                }
            }
            return 0;
        }
        return 0;
      case WM_CLOSE:
        logbox = NULL;
        SetActiveWindow(GetParent(hwnd));
        DestroyWindow(hwnd);
        return 0;
    }
    return 0;
}

static BOOL CALLBACK LicenceProc(HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        char *str = dupprintf("%s Licence", appname);
        SetWindowText(hwnd, str);
        sfree(str);
        SetDlgItemText(hwnd, IDA_TEXT, LICENCE_TEXT("\r\n\r\n"));
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            EndDialog(hwnd, 1);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, 1);
        return 0;
    }
    return 0;
}

static BOOL CALLBACK AboutProc(HWND hwnd, UINT msg,
                               WPARAM wParam, LPARAM lParam)
{
    char *str;

    switch (msg) {
      case WM_INITDIALOG: {
        char *buildinfo_text;
        char *text;
        str = dupprintf("About %s", appname);
        SetWindowText(hwnd, str);
        sfree(str);
        buildinfo_text = buildinfo("\r\n");
        text = dupprintf(
            "%s\r\n\r\n%s\r\n\r\n%s\r\n\r\n%s",
            appname, ver, buildinfo_text,
            "\251 " SHORT_COPYRIGHT_DETAILS ". All rights reserved.");
        sfree(buildinfo_text);
        SetDlgItemText(hwnd, IDA_TEXT, text);
        MakeDlgItemBorderless(hwnd, IDA_TEXT);
        sfree(text);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
          case IDCANCEL:
            EndDialog(hwnd, true);
            return 0;
          case IDA_LICENCE:
            EnableWindow(hwnd, 0);
            DialogBox(hinst, MAKEINTRESOURCE(IDD_LICENCEBOX),
                      hwnd, LicenceProc);
            EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
            return 0;

          case IDA_WEB:
            /* Load web browser */
            ShellExecute(hwnd, "open",
                         "https://www.chiark.greenend.org.uk/~sgtatham/putty/",
                         0, 0, SW_SHOWDEFAULT);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        EndDialog(hwnd, true);
        return 0;
    }
    return 0;
}

/*
 * Null dialog procedure.
 */
static BOOL CALLBACK NullDlgProc(HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam)
{
    return 0;
}

enum {
    IDCX_ABOUT = IDC_ABOUT,
    IDCX_CATSTATIC,
    IDCX_CATLIST,
    IDCX_STDBASE,
    IDCX_PANELBASE = IDCX_STDBASE + 32
};

/* ctrltrees indices for the main dialog box */
enum {
    TREE_PANEL, /* things we swap out every time a new panel is chosen */
    TREE_BASE, /* fixed things at the bottom like OK and Cancel buttons */
};

/*
 * Replace the panel of controls on the right of the configuration
 * box with the one for 'path'.
 */
static void pds_show_panel(PortableDialogStuff *pds, HWND hwnd, char *path)
{
    struct winctrl *c;
    HWND item;
    int k;

    if (path == pds->panelpath)
        return;                        /* already showing that one */
    pds->panelpath = path;

    SendMessage(hwnd, WM_SETREDRAW, false, 0);

    /* Destroy all controls in the currently visible panel. */
    while ((c = winctrl_findbyindex(
                &pds->ctrltrees[TREE_PANEL], 0)) != NULL) {
        for (k = 0; k < c->num_ids; k++) {
            item = GetDlgItem(hwnd, c->base_id + k);
            if (item)
                DestroyWindow(item);
        }
        winctrl_rem_shortcuts(pds->dp, c);
        winctrl_remove(&pds->ctrltrees[TREE_PANEL], c);
        sfree(c->data);
        sfree(c);
    }
    pds_create_controls(pds, TREE_PANEL, IDCX_PANELBASE, 100, 3, 13, path);

    dlg_refresh(NULL, pds->dp);    /* set up control values */

    SendMessage(hwnd, WM_SETREDRAW, true, 0);
    InvalidateRect(hwnd, NULL, true);
}

/*
 * This function is the configuration box.
 * (Being a dialog procedure, in general it returns 0 if the default
 * dialog processing should be performed, and 1 if it should not.)
 */
static BOOL GenericMainDlgProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam, void *ctx)
{
    PortableDialogStuff *pds = (PortableDialogStuff *)ctx;
    HWND catlist;

    switch (msg) {
      case WM_INITDIALOG:
        pds_initdialog_start(pds, hwnd);

        pds_create_controls(pds, TREE_BASE, IDCX_STDBASE, 3, 3, 235, "");

        SetClassLong(hwnd, GCL_HICON,
                     (LONG)LoadIcon(hinst, MAKEINTRESOURCE(IDI_CFGICON)));

        centre_window(hwnd);

        /*
         * Create the list of panels down the left. (A tree view would
         * need COMCTL32, which Windows NT 3.1 and 3.5 don't have.)
         */
        {
            RECT r;
            WPARAM font;
            HWND catstatic;

            r.left = 3;
            r.right = r.left + 95;
            r.top = 3;
            r.bottom = r.top + 10;
            MapDialogRect(hwnd, &r);
            catstatic = CreateWindowEx(0, "STATIC", "Cate&gory:",
                                       WS_CHILD | WS_VISIBLE,
                                       r.left, r.top,
                                       r.right - r.left, r.bottom - r.top,
                                       hwnd, (HMENU) IDCX_CATSTATIC, hinst,
                                       NULL);
            font = SendMessage(hwnd, WM_GETFONT, 0, 0);
            SendMessage(catstatic, WM_SETFONT, font, MAKELPARAM(true, 0));

            r.left = 3;
            r.right = r.left + 95;
            r.top = 13;
            r.bottom = r.top + 219;
            MapDialogRect(hwnd, &r);
            catlist = CreateWindowEx(EDGE_EXSTYLE, "LISTBOX", "",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                     WS_VSCROLL | EDGE_STYLE | LBS_NOTIFY |
                                     LBS_NOINTEGRALHEIGHT, r.left, r.top,
                                     r.right - r.left, r.bottom - r.top,
                                     hwnd, (HMENU) IDCX_CATLIST, hinst,
                                     NULL);
            font = SendMessage(hwnd, WM_GETFONT, 0, 0);
            SendMessage(catlist, WM_SETFONT, font, MAKELPARAM(true, 0));
        }

        /*
         * Fill in the list: one line per panel, indented by its depth
         * in the path, with the path as the line's item data.
         */
        {
            int i;
            char *path = NULL;
            char *firstpath = NULL;

            for (i = 0; i < pds->ctrlbox->nctrlsets; i++) {
                struct controlset *s = pds->ctrlbox->ctrlsets[i];
                int j;
                LRESULT item;
                char *c;
                char *text;

                if (!s->pathname[0])
                    continue;
                j = path ? ctrl_path_compare(s->pathname, path) : 0;
                if (j == INT_MAX)
                    continue;          /* same path, nothing to add to list */

                /*
                 * We expect never to find an implicit path
                 * component. For example, we expect never to see
                 * A/B/C followed by A/D/E, because that would
                 * _implicitly_ create A/D. All our path prefixes
                 * are expected to contain actual controls and be
                 * selectable in the list; so we would expect
                 * to see A/D _explicitly_ before encountering
                 * A/D/E.
                 */
                assert(j == ctrl_path_elements(s->pathname) - 1);

                c = strrchr(s->pathname, '/');
                if (!c)
                    c = s->pathname;
                else
                    c++;

                text = dupprintf("%*s%s", j * 4, "", c);
                item = SendMessage(catlist, LB_ADDSTRING, 0, (LPARAM)text);
                SendMessage(catlist, LB_SETITEMDATA, item,
                            (LPARAM)s->pathname);
                sfree(text);
                if (!firstpath)
                    firstpath = s->pathname;

                path = s->pathname;
            }

            /*
             * Select the first panel in the ctrlbox, and create the
             * actual control set for it.
             */
            SendMessage(catlist, LB_SETCURSEL, 0, 0);
            assert(firstpath);   /* config.c must have given us _something_ */
            pds->panelpath = firstpath;
            pds_create_controls(pds, TREE_PANEL, IDCX_PANELBASE,
                                100, 3, 13, firstpath);
            dlg_refresh(NULL, pds->dp);    /* and set up control values */
        }

        pds_initdialog_finish(pds);
        return 0;

      case WM_COMMAND:
        if (LOWORD(wParam) != IDCX_CATLIST)
            return pds_default_dlgproc(pds, hwnd, msg, wParam, lParam);

        /*
         * Choosing a line in the list replaces the panel - but only
         * after WM_INITDIALOG has finished.
         */
        if (HIWORD(wParam) == LBN_SELCHANGE && pds->initialised) {
            LRESULT sel;
            catlist = (HWND)lParam;
            sel = SendMessage(catlist, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                pds_show_panel(pds, hwnd, (char *)SendMessage(
                                   catlist, LB_GETITEMDATA, sel, 0));
                SetFocus(catlist);     /* ensure focus stays */
            }
        }
        return 0;

      default:
        return pds_default_dlgproc(pds, hwnd, msg, wParam, lParam);
    }
}

void modal_about_box(HWND hwnd)
{
    EnableWindow(hwnd, 0);
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
    EnableWindow(hwnd, 1);
    SetActiveWindow(hwnd);
}

void defuse_showwindow(void)
{
    /*
     * Work around the fact that the app's first call to ShowWindow
     * will ignore the default in favour of the shell-provided
     * setting.
     */
    {
        HWND hwnd;
        hwnd = CreateDialog(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
                            NULL, NullDlgProc);
        ShowWindow(hwnd, SW_HIDE);
        SetActiveWindow(hwnd);
        DestroyWindow(hwnd);
    }
}

bool do_config(Conf *conf)
{
    bool ret;
    PortableDialogStuff *pds = pds_new(2);

    setup_config_box(pds->ctrlbox, false, 0, 0);
    win_setup_config_box(pds->ctrlbox, &pds->dp->hwnd, false, 0);

    pds->dp->wintitle = dupprintf("%s Configuration", appname);
    pds->dp->data = conf;

    dlg_auto_set_fixed_pitch_flag(pds->dp);

    pds->dp->shortcuts['g'] = true;          /* the list: `Cate&gory' */

    ret = ShinyDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), "PuTTYConfigBox",
                         NULL, GenericMainDlgProc, pds) != 0;

    pds_free(pds);

    return ret;
}

bool do_reconfig(HWND hwnd, Conf *conf, int protcfginfo)
{
    Conf *backup_conf;
    bool ret;
    int protocol;
    PortableDialogStuff *pds = pds_new(2);

    backup_conf = conf_copy(conf);

    protocol = conf_get_int(conf, CONF_protocol);
    setup_config_box(pds->ctrlbox, true, protocol, protcfginfo);
    win_setup_config_box(pds->ctrlbox, &pds->dp->hwnd, true, protocol);

    pds->dp->wintitle = dupprintf("%s Reconfiguration", appname);
    pds->dp->data = conf;

    dlg_auto_set_fixed_pitch_flag(pds->dp);

    pds->dp->shortcuts['g'] = true;          /* the list: `Cate&gory' */

    ret = ShinyDialogBox(hinst, MAKEINTRESOURCE(IDD_MAINBOX), "PuTTYConfigBox",
                         NULL, GenericMainDlgProc, pds) != 0;

    pds_free(pds);

    if (!ret)
        conf_copy_into(conf, backup_conf);

    conf_free(backup_conf);

    return ret;
}

static void win_gui_eventlog(LogPolicy *lp, const char *string)
{
    char timebuf[40];
    char **location;
    struct tm tm;

    tm=ltime();
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);

    if (ninitial < LOGEVENT_INITIAL_MAX)
        location = &events_initial[ninitial];
    else
        location = &events_circular[(circular_first + ncircular) % LOGEVENT_CIRCULAR_MAX];

    if (*location)
        sfree(*location);
    *location = dupcat(timebuf, string, NULL);
    if (logbox) {
        int count;
        SendDlgItemMessage(logbox, IDN_LIST, LB_ADDSTRING,
                           0, (LPARAM) *location);
        count = SendDlgItemMessage(logbox, IDN_LIST, LB_GETCOUNT, 0, 0);
        SendDlgItemMessage(logbox, IDN_LIST, LB_SETTOPINDEX, count - 1, 0);

        update_logbox_horizontal_extent(logbox);
    }
    if (ninitial < LOGEVENT_INITIAL_MAX) {
        ninitial++;
    } else if (ncircular < LOGEVENT_CIRCULAR_MAX) {
        ncircular++;
    } else if (ncircular == LOGEVENT_CIRCULAR_MAX) {
        circular_first = (circular_first + 1) % LOGEVENT_CIRCULAR_MAX;
        sfree(events_circular[circular_first]);
        events_circular[circular_first] = dupstr("..");
    }
}

static void win_gui_logging_error(LogPolicy *lp, const char *event)
{
    WinGuiSeat *wgs = container_of(lp, WinGuiSeat, logpolicy);

    /* Send 'can't open log file' errors to the terminal window.
     * (Marked as stderr, although terminal.c won't care.) */
    seat_stderr_pl(&wgs->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&wgs->seat, PTRLEN_LITERAL("\r\n"));
}

void showeventlog(HWND hwnd)
{
    if (!logbox) {
        logbox = CreateDialog(hinst, MAKEINTRESOURCE(IDD_LOGBOX),
                              hwnd, LogProc);
        ShowWindow(logbox, SW_SHOWNORMAL);
    }
    SetActiveWindow(logbox);
}

void showabout(HWND hwnd)
{
    DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutProc);
}

struct hostkey_dialog_ctx {
    SeatDialogText *text;
    bool has_title;
    HFONT title_font;
};

static BOOL HostKeyMoreInfoProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam, void *vctx)
{
    struct hostkey_dialog_ctx *ctx = (struct hostkey_dialog_ctx *)vctx;

    switch (msg) {
      case WM_INITDIALOG: {
        int index = 100, y = 12;

        WPARAM font = SendMessage(hwnd, WM_GETFONT, 0, 0);

        const char *key = NULL;
        RECT r;
        HWND ctl;
        int oldheight;
        int newheight;
        SeatDialogTextItem *item;
        SeatDialogTextItem *end;
        for (item = ctx->text->items, end = item + ctx->text->nitems;
             item < end; item++) {
            switch (item->type) {
              case SDT_MORE_INFO_KEY:
                key = item->text;
                break;
              case SDT_MORE_INFO_VALUE_SHORT:
              case SDT_MORE_INFO_VALUE_BLOB: {
                RECT rk, rv;
                DWORD editstyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_AUTOHSCROLL | ES_READONLY;
                HWND ctl;
                if (item->type == SDT_MORE_INFO_VALUE_BLOB) {
                    rk.left = 12;
                    rk.right = 376;
                    rk.top = y;
                    rk.bottom = 8;
                    y += 10;

                    editstyle |= ES_MULTILINE;
                    rv.left = 12;
                    rv.right = 376;
                    rv.top = y;
                    rv.bottom = 64;
                    y += 68;
                } else {
                    rk.left = 12;
                    rk.right = 80;
                    rk.top = y+2;
                    rk.bottom = 8;

                    rv.left = 100;
                    rv.right = 288;
                    rv.top = y;
                    rv.bottom = 12;

                    y += 16;
                }

                MapDialogRect(hwnd, &rk);
                ctl = CreateWindowEx(
                    0, "STATIC", key, WS_CHILD | WS_VISIBLE,
                    rk.left, rk.top, rk.right, rk.bottom,
                    hwnd, (HMENU)(UINT)index++, hinst, NULL);
                SendMessage(ctl, WM_SETFONT, font, MAKELPARAM(true, 0));

                MapDialogRect(hwnd, &rv);
                ctl = CreateWindowEx(
                    EDGE_EXSTYLE, "EDIT", item->text, editstyle | EDGE_STYLE,
                    rv.left, rv.top, rv.right, rv.bottom,
                    hwnd, (HMENU)(UINT)index++, hinst, NULL);
                SendMessage(ctl, WM_SETFONT, font, MAKELPARAM(true, 0));
                break;
              }
              default:
                break;
            }
        }

        /*
         * Now resize the overall window, and move the Close button at
         * the bottom.
         */
        r.left = 176;
        r.top = y + 10;
        r.right = r.bottom = 0;
        MapDialogRect(hwnd, &r);
        ctl = GetDlgItem(hwnd, IDOK);
        SetWindowPos(ctl, NULL, r.left, r.top, 0, 0,
                     SWP_NOSIZE | SWP_NOREDRAW | SWP_NOZORDER);

        r.left = r.top = r.right = 0;
        r.bottom = 300;
        MapDialogRect(hwnd, &r);
        oldheight = r.bottom;

        r.left = r.top = r.right = 0;
        r.bottom = y + 30;
        MapDialogRect(hwnd, &r);
        newheight = r.bottom;

        GetWindowRect(hwnd, &r);

        SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left,
                     r.bottom - r.top + newheight - oldheight,
                     SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

        ShowWindow(hwnd, SW_SHOWNORMAL);
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK:
            ShinyEndDialog(hwnd, 0);
            return 0;
        }
        return 0;
      case WM_CLOSE:
        ShinyEndDialog(hwnd, 0);
        return 0;
    }
    return 0;
}

static const char *process_seatdialogtext(
    strbuf *dlg_text, const char **scary_heading, SeatDialogText *text)
{
    const char *dlg_title = "";
    SeatDialogTextItem *item;
    SeatDialogTextItem *end;

    for (item = text->items, end = item + text->nitems; item < end; item++) {
        switch (item->type) {
          case SDT_PARA:
            BinarySink_put_fmt(BinarySink_UPCAST(dlg_text), "%s\r\n\r\n", item->text);
            break;
          case SDT_DISPLAY:
            BinarySink_put_fmt(BinarySink_UPCAST(dlg_text), "%s\r\n\r\n", item->text);
            break;
          case SDT_SCARY_HEADING:
            assert(scary_heading != NULL && "only expect a scary heading if "
                   "the dialog has somewhere to put it");
            *scary_heading = item->text;
            break;
          case SDT_TITLE:
            dlg_title = item->text;
            break;
          default:
            break;
        }
    }

    /* Trim any trailing newlines */
    while (strbuf_chomp(dlg_text, '\r') || strbuf_chomp(dlg_text, '\n'));

    return dlg_title;
}

static BOOL HostKeyDialogProc(HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam, void *vctx)
{
    struct hostkey_dialog_ctx *ctx = (struct hostkey_dialog_ctx *)vctx;

    switch (msg) {
      case WM_INITDIALOG: {
        static const struct {
            int id, x;
        } buttons[] = {
            { IDCANCEL, 288 },
            { IDC_HK_ACCEPT, 168 },
            { IDC_HK_ONCE, 216 },
            { IDC_HK_MOREINFO, 60 },
        };
        strbuf *dlg_text = strbuf_new();
        const char *scary_heading = NULL;
        const char *dlg_title = process_seatdialogtext(
            dlg_text, &scary_heading, ctx->text);

        LPCTSTR iconid = IDI_QUESTION;
        int height;
        int edittop;
        RECT r;
        int oldheight;
        int newheight;
        size_t i;
        ctx->has_title = (scary_heading != NULL);
        if (scary_heading) {
            /* The heading is in a larger, bold form of the system font */
            LOGFONT lf;
            SetDlgItemText(hwnd, IDC_HK_TITLE, scary_heading);
            if (GetObject(GetStockObject(SYSTEM_FONT), sizeof(lf), &lf)) {
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = lf.lfHeight * 3 / 2;
                ctx->title_font = CreateFontIndirect(&lf);
                if (ctx->title_font)
                    SendDlgItemMessage(hwnd, IDC_HK_TITLE, WM_SETFONT,
                                       (WPARAM)ctx->title_font, 0);
            }
            iconid = IDI_EXCLAMATION;
        }

        SetDlgItemText(hwnd, IDC_HK_TEXT, dlg_text->s);
        MakeDlgItemBorderless(hwnd, IDC_HK_TEXT);
        strbuf_free(dlg_text);

        SetWindowText(hwnd, dlg_title);

        if (!ctx->has_title) {
            HWND item = GetDlgItem(hwnd, IDC_HK_TITLE);
            if (item)
                DestroyWindow(item);
        }

        /*
         * Find out how tall the text in the edit control really ended
         * up (after line wrapping), and adjust the height of the
         * whole box to match it.
         */
        height = SendDlgItemMessage(hwnd, IDC_HK_TEXT,
                                    EM_GETLINECOUNT, 0, 0);
        height *= 8; /* height of a text line, by definition of dialog units */

        edittop = ctx->has_title ? 40 : 20;

        r.left = 40;
        r.top = edittop;
        r.right = 290;
        r.bottom = height;
        MapDialogRect(hwnd, &r);
        SetWindowPos(GetDlgItem(hwnd, IDC_HK_TEXT), NULL,
                     r.left, r.top, r.right, r.bottom,
                     SWP_NOREDRAW | SWP_NOZORDER);

        for (i = 0; i < lenof(buttons); i++) {
            HWND ctl = GetDlgItem(hwnd, buttons[i].id);
            r.left = buttons[i].x;
            r.top = edittop + height + 20;
            r.right = r.bottom = 0;
            MapDialogRect(hwnd, &r);
            SetWindowPos(ctl, NULL, r.left, r.top, 0, 0,
                         SWP_NOSIZE | SWP_NOREDRAW | SWP_NOZORDER);
        }

        r.left = r.top = r.right = 0;
        r.bottom = 240;
        MapDialogRect(hwnd, &r);
        oldheight = r.bottom;

        r.left = r.top = r.right = 0;
        r.bottom = edittop + height + 40;
        MapDialogRect(hwnd, &r);
        newheight = r.bottom;

        GetWindowRect(hwnd, &r);

        SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left,
                     r.bottom - r.top + newheight - oldheight,
                     SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

        SendDlgItemMessage(hwnd, IDC_HK_ICON, STM_SETICON,
                           (WPARAM)LoadIcon(NULL, iconid), 0);

        ShowWindow(hwnd, SW_SHOWNORMAL);

        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_HK_ACCEPT:
          case IDC_HK_ONCE:
          case IDCANCEL:
            ShinyEndDialog(hwnd, LOWORD(wParam));
            return 0;
          case IDC_HK_MOREINFO: {
            ShinyDialogBox(hinst, MAKEINTRESOURCE(IDD_HK_MOREINFO),
                           "PuTTYHostKeyMoreInfo", hwnd,
                           HostKeyMoreInfoProc, ctx);
          }
        }
        return 0;
      case WM_CLOSE:
        ShinyEndDialog(hwnd, IDCANCEL);
        return 0;
    }
    return 0;
}

const SeatDialogPromptDescriptions *win_seat_prompt_descriptions(Seat *seat)
{
    static const SeatDialogPromptDescriptions descs = {
        /* .hk_accept_action = */ "press \"Accept\"",
        /* .hk_connect_once_action = */ "press \"Connect Once\"",
        /* .hk_cancel_action = */ "press \"Cancel\"",
        /* .hk_cancel_action_Participle = */ "Pressing \"Cancel\"",
        /* .weak_accept_action = */ "press \"Yes\"",
        /* .weak_cancel_action = */ "press \"No\"",
    };
    return &descs;
}

SeatPromptResult win_seat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *cbctx)
{
    WinGuiSeat *wgs = container_of(seat, WinGuiSeat, seat);

    struct hostkey_dialog_ctx ctx[1];
    int mbret;
    ctx->text = text;
    ctx->has_title = false;
    ctx->title_font = NULL;

    mbret = ShinyDialogBox(
        hinst, MAKEINTRESOURCE(IDD_HOSTKEY), "PuTTYHostKeyDialog",
        wgs->term_hwnd, HostKeyDialogProc, ctx);
    if (ctx->title_font)
        DeleteObject(ctx->title_font);
    assert(mbret==IDC_HK_ACCEPT || mbret==IDC_HK_ONCE || mbret==IDCANCEL);
    if (mbret == IDC_HK_ACCEPT) {
        store_host_key(seat, host, port, keytype, keystr);
        return SPR_OK;
    } else if (mbret == IDC_HK_ONCE) {
        return SPR_OK;
    }

    return SPR_USER_ABORT;
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
SeatPromptResult win_seat_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    strbuf *dlg_text = strbuf_new();
    const char *dlg_title = process_seatdialogtext(dlg_text, NULL, text);

    int mbret = MessageBox(NULL, dlg_text->s, dlg_title,
                           MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    strbuf_free(dlg_text);

    if (mbret == IDYES)
        return SPR_OK;
    else
        return SPR_USER_ABORT;
}

SeatPromptResult win_seat_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    strbuf *dlg_text = strbuf_new();
    const char *dlg_title = process_seatdialogtext(dlg_text, NULL, text);

    int mbret = MessageBox(NULL, dlg_text->s, dlg_title,
                           MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    socket_reselect_all();
    strbuf_free(dlg_text);

    if (mbret == IDYES)
        return SPR_OK;
    else
        return SPR_USER_ABORT;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
static int win_gui_askappend(LogPolicy *lp, Filename *filename,
                             void (*callback)(void *ctx, int result),
                             void *ctx)
{
    static const char msgtemplate[] =
        "The session log file \"%.*s\" already exists.\n"
        "You can overwrite it with a new session log,\n"
        "append your session log to the end of it,\n"
        "or disable session logging for this session.\n"
        "Hit Yes to wipe the file, No to append to it,\n"
        "or Cancel to disable logging.";
    char *message;
    char *mbtitle;
    int mbret;

    message = dupprintf(msgtemplate, FILENAME_MAX, filename->utf8path);
    mbtitle = dupprintf("%s Log to File", appname);

    mbret = message_box(NULL, message, mbtitle,
                        MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON3,
                        true, 0);

    socket_reselect_all();

    sfree(message);
    sfree(mbtitle);

    if (mbret == IDYES)
        return 2;
    else if (mbret == IDNO)
        return 1;
    else
        return 0;
}

const LogPolicyVtable win_gui_logpolicy_vt = {
    /* .eventlog = */ win_gui_eventlog,
    /* .askappend = */ win_gui_askappend,
    /* .logging_error = */ win_gui_logging_error,
    /* .verbose = */ null_lp_verbose_yes,
};

/*
 * Warn about the obsolescent key file format.
 *
 * Uniquely among these functions, this one does _not_ expect a
 * frontend handle. This means that if PuTTY is ported to a
 * platform which requires frontend handles, this function will be
 * an anomaly. Fortunately, the problem it addresses will not have
 * been present on that platform, so it can plausibly be
 * implemented as an empty function.
 */
void old_keyfile_warning(void)
{
    static const char mbtitle[] = "%s Key File Warning";
    static const char message[] =
        "You are loading an SSH-2 private key which has an\n"
        "old version of the file format. This means your key\n"
        "file is not fully tamperproof. Future versions of\n"
        "%s may stop supporting this private key format,\n"
        "so we recommend you convert your key to the new\n"
        "format.\n"
        "\n"
        "You can perform this conversion by loading the key\n"
        "into PuTTYgen and then saving it again.";

    char *msg, *title;
    msg = dupprintf(message, appname);
    title = dupprintf(mbtitle, appname);

    MessageBox(NULL, msg, title, MB_OK);

    socket_reselect_all();

    sfree(msg);
    sfree(title);
}

static BOOL CAConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                         void *ctx)
{
    PortableDialogStuff *pds = (PortableDialogStuff *)ctx;

    switch (msg) {
      case WM_INITDIALOG:
        pds_initdialog_start(pds, hwnd);

        SetClassLong(hwnd, GCL_HICON,
                     (LONG)LoadIcon(hinst, MAKEINTRESOURCE(IDI_CFGICON)));

        centre_window(hwnd);

        pds_create_controls(pds, 0, IDCX_PANELBASE, 3, 3, 3, "Main");
        pds_create_controls(pds, 0, IDCX_STDBASE, 3, 3, 243, "");
        dlg_refresh(NULL, pds->dp);    /* and set up control values */

        pds_initdialog_finish(pds);
        return 0;

      default:
        return pds_default_dlgproc(pds, hwnd, msg, wParam, lParam);
    }
}

void show_ca_config_box(dlgparam *dp)
{
    PortableDialogStuff *pds = pds_new(1);

    setup_ca_config_box(pds->ctrlbox);

    ShinyDialogBox(hinst, MAKEINTRESOURCE(IDD_CA_CONFIG), "PuTTYConfigBox",
                   dp ? dp->hwnd : NULL, CAConfigProc, pds);

    pds_free(pds);
}
