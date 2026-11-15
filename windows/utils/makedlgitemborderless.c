/*
 * Helper function to remove the border around a dialog item such as
 * a read-only edit control.
 */

#include "putty.h"

void MakeDlgItemBorderless(HWND parent, int id)
{
    HWND child = GetDlgItem(parent, id);
    LONG style = GetWindowLong(child, GWL_STYLE);
    LONG exstyle = GetWindowLong(child, GWL_EXSTYLE);
    style &= ~WS_BORDER;
#ifdef WS_EX_CLIENTEDGE                /* 3-D edges came with NT 4.0 */
    exstyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
#endif
    SetWindowLong(child, GWL_STYLE, style);
    SetWindowLong(child, GWL_EXSTYLE, exstyle);
    SetWindowPos(child, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
