#include "putty.h"

typedef enum SavedDir { SD_NONE, SD_WCHAR, SD_CHAR } SavedDir;

struct filereq_saved_dir {
    SavedDir which;
    union {
        WCHAR wcwd[MAX_PATH];
        TCHAR cwd[MAX_PATH];
    };
};

filereq_saved_dir *filereq_saved_dir_new(void)
{
    filereq_saved_dir *state = snew(filereq_saved_dir);
    state->which = SD_NONE;
    return state;
}

void filereq_saved_dir_free(filereq_saved_dir *state)
{
    sfree(state);
}

static void save_dir(filereq_saved_dir *state)
{
    DWORD dirlen;

    dirlen = GetCurrentDirectoryW(lenof(state->wcwd), state->wcwd);
    if (dirlen > 0 && dirlen < lenof(state->wcwd)) {
        state->which = SD_WCHAR;
        return;
    }

    dirlen = GetCurrentDirectoryA(lenof(state->cwd), state->cwd);
    if (dirlen > 0 && dirlen < lenof(state->cwd)) {
        state->which = SD_CHAR;
        return;
    }

    state->which = SD_NONE;
}

static void restore_dir(filereq_saved_dir *state)
{
    switch (state->which) {
      case SD_WCHAR:
        SetCurrentDirectoryW(state->wcwd);
        break;
      case SD_CHAR:
        SetCurrentDirectoryA(state->cwd);
        break;
      case SD_NONE:
        break;
    }
}

/*
 * Internal function that brings up a file dialog, returning a raw
 * wchar_t buffer containing the output.
 *
 * Inputs:
 *  - hwnd: the parent window for the dialog, or NULL if none
 *  - title: the window title
 *  - initial: a filename to populate the new dialog with, or NULL
 *  - dir: a location in which to persist the logical cwd used by
 *    successive file dialogs
 *  - save: true if the file dialog is for write rather than loading a file
 *  - filter: the default type of file being asked about, which will inform
 *    the choice of which files to display in the dialog, and also a default
 *    file extension for saving files
 *  - multi_filename_offset: NULL if you want to return exactly one file.
 *    Otherwise points to a size_t which gets nFileOffset from the result
 *    structure. This is passed to request_multi_file_populate below.
 *  - filename: buffer to put the output in
 *  - filename_size: size of the buffer.
 */
static bool do_filereq(
    HWND hwnd, const char *title, Filename *initial, filereq_saved_dir *dir,
    bool save, FilereqFilter filter, size_t *multi_filename_offset,
    wchar_t *filename, size_t filename_size)
{
    OPENFILENAMEW of;
    void *tofree1 = NULL, *tofree2 = NULL;
    bool toret;

    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);

    if (dir && dir->which == SD_WCHAR)
        of.lpstrInitialDir = dir->wcwd;
    else if (dir && dir->which == SD_CHAR) {
        wchar_t *winitdir = dup_mb_to_wc(CP_ACP, dir->cwd);
        tofree1 = winitdir;
        of.lpstrInitialDir = winitdir;
    }

    switch (filter) {
      default: /* FILTER_ALL_FILES */
        of.lpstrFilter = L"All Files (*.*)\0*\0\0\0";
        break;
      case FILTER_KEY_FILES:
        of.lpstrFilter = L"PuTTY Private Key Files (*.ppk)\0*.ppk\0"
            L"All Files (*.*)\0*\0\0\0";
        of.lpstrDefExt = L".ppk";
        break;
      case FILTER_DYNLIB_FILES:
        of.lpstrFilter = L"Dynamic Library Files (*.dll)\0*.dll\0"
            L"All Files (*.*)\0*\0\0\0";
        of.lpstrDefExt = L".dll";
        break;
      case FILTER_SOUND_FILES:
        of.lpstrFilter = L"Wave Files (*.wav)\0*.WAV\0"
            L"All Files (*.*)\0*\0\0\0";
        of.lpstrDefExt = L".wav";
        break;
    }
    of.nFilterIndex = 1;

    of.hwndOwner = hwnd;

    if (initial) {
        wcsncpy(filename, filename_to_wstr(initial), filename_size - 1);
        filename[filename_size - 1] = L'\0';
    } else {
        *filename = L'\0';
    }
    of.lpstrFile = filename;
    of.nMaxFile = filename_size;

    if (title) {
        wchar_t *wtitle = dup_mb_to_wc(CP_ACP, title);
        tofree2 = wtitle;
        of.lpstrTitle = wtitle;
    }

#ifdef OFN_EXPLORER
    /* Only the Explorer-style dialog of Windows NT 4.0 returns several
     * files in the form request_multi_file_populate expects; the older
     * one separates them with spaces. So older SDKs, and older
     * versions of Windows, get one file at a time. */
    if (multi_filename_offset && LOBYTE(LOWORD(GetVersion())) >= 4)
        of.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
#endif

    toret = (save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of)) != 0;

    if (dir)
        save_dir(dir);

    sfree(tofree1);
    sfree(tofree2);

    if (multi_filename_offset)
        *multi_filename_offset = of.nFileOffset;

    return toret;
}

Filename *request_file(
    HWND hwnd, const char *title, Filename *initial, bool save,
    filereq_saved_dir *dir, bool preserve_cwd, FilereqFilter filter)
{
    filereq_saved_dir saved_cwd[1];
    Filename *filename = NULL;
    wchar_t namebuf[MAX_PATH];

    if (preserve_cwd)
        save_dir(saved_cwd);

    if (do_filereq(
            hwnd, title, initial, dir, save, filter,
            NULL, namebuf, lenof(namebuf)))
        filename = filename_from_wstr(namebuf);

    if (preserve_cwd)
        restore_dir(saved_cwd);

    return filename;
}

static struct request_multi_file_return *request_multi_file_populate(
    const wchar_t *buf, size_t first_filename_offset)
{
    struct request_multi_file_return *rmf =
        snew(struct request_multi_file_return);

    /*
     * We expect one of two situations (guaranteed by the return from
     * the OFN_MULTISELECT file dialog API function):
     *
     * 1. There is a single NUL-terminated filename string in buf,
     * potentially including a path, and first_filename_offset points
     * to the leaf name part of it.
     *
     * 2. There are multiple NUL-terminated strings in buf, with the
     * first being a path, and the remaining ones being leaf names to
     * concatenate to that path. An empty string / extra NUL
     * terminates the whole list. first_filename_offset points to the
     * start of the first leaf name.
     *
     * Hence, we can tell these apart by finding out whether a NUL
     * appears in the buffer before first_filename_offset. If no,
     * we're in case 1; if yes, case 2.
     */
    if (wcslen(buf) > first_filename_offset) {
        /* Case 1: a single filename. */
        rmf->nfilenames = 1;
        rmf->filenames = snewn(1, Filename *);
        rmf->filenames[0] = filename_from_wstr(buf);
    } else {
        /* Case 2: multiple filenames preceded by a path. */
        size_t filenamesize = 16;
        const wchar_t *dir;
        const wchar_t *sep;
        const wchar_t *p;
        rmf->nfilenames = 0;
        rmf->filenames = snewn(filenamesize, Filename *);

        dir = buf;
        sep = (*dir && dir[wcslen(dir)-1] == L'\\') ? L"" : L"\\";
        p = buf + wcslen(dir) + 1;
        for (; *p; p += wcslen(p) + 1) {
            wchar_t *filename = dupwcscat(dir, sep, p, NULL);
            sgrowarray(rmf->filenames, filenamesize, rmf->nfilenames);
            rmf->filenames[rmf->nfilenames++] = filename_from_wstr(filename);
            sfree(filename);
        }
    }
    return rmf;
}

#define MULTI_FACTOR 32

struct request_multi_file_return *request_multi_file(
    HWND hwnd, const char *title, Filename *initial, bool save,
    filereq_saved_dir *dir, bool preserve_cwd, FilereqFilter filter)
{
    filereq_saved_dir saved_cwd[1];
    struct request_multi_file_return *rmf = NULL;
    size_t first_filename_offset;
    wchar_t namebuf[MAX_PATH * MULTI_FACTOR];

    if (preserve_cwd)
        save_dir(saved_cwd);

    if (do_filereq(
            hwnd, title, initial, dir, save, filter,
            &first_filename_offset, namebuf, lenof(namebuf)))
        rmf = request_multi_file_populate(namebuf, first_filename_offset);

    if (preserve_cwd)
        restore_dir(saved_cwd);

    return rmf;
}

void request_multi_file_free(struct request_multi_file_return *rmf)
{
    size_t i;
    for (i = 0; i < rmf->nfilenames; i++)
        filename_free(rmf->filenames[i]);
    sfree(rmf->filenames);
    sfree(rmf);
}
