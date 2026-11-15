/*
 * no-jump-list.c: stub jump list functions for Windows executables
 * that don't update the jump list.
 */

#include "putty.h"

void add_session_to_jumplist(const char * const sessionname) {}
void remove_session_from_jumplist(const char * const sessionname) {}
void clear_jumplist(void) {}
/* Taskbar app IDs came with Windows 7: nothing to set on NT 3.x / 4.0 */
bool set_explicit_app_user_model_id(void) { return true; }
