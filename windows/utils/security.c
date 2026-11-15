/*
 * windows/utils/security.c: implementation of security-api.h.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

#include "security-api.h"

/* Process access rights that the Windows NT 3.1 headers don't name */
#ifndef PROCESS_SET_QUOTA
#define PROCESS_SET_QUOTA 0x0100
#endif
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME 0x0800
#endif

/* Initialised once, then kept around to reuse forever */
static PSID worldsid, networksid, usersid;

PSID get_user_sid(void)
{
    HANDLE proc = NULL, tok = NULL;
    TOKEN_USER *user = NULL;
    DWORD toklen, sidlen;
    PSID sid = NULL, ret = NULL;

    if (usersid)
        return usersid;

    if ((proc = OpenProcess(MAXIMUM_ALLOWED, false,
                            GetCurrentProcessId())) == NULL)
        goto cleanup;

    if (!OpenProcessToken(proc, TOKEN_QUERY, &tok))
        goto cleanup;

    if (!GetTokenInformation(tok, TokenUser, NULL, 0, &toklen) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        goto cleanup;

    if ((user = (TOKEN_USER *)LocalAlloc(LPTR, toklen)) == NULL)
        goto cleanup;

    if (!GetTokenInformation(tok, TokenUser, user, toklen, &toklen))
        goto cleanup;

    sidlen = GetLengthSid(user->User.Sid);

    sid = (PSID)smalloc(sidlen);

    if (!CopySid(sidlen, sid, user->User.Sid))
        goto cleanup;

    /* Success. Move sid into the return value slot, and null it out
     * to stop the cleanup code freeing it. */
    ret = usersid = sid;
    sid = NULL;

  cleanup:
    if (proc != NULL)
        CloseHandle(proc);
    if (tok != NULL)
        CloseHandle(tok);
    if (user != NULL)
        LocalFree(user);
    if (sid != NULL)
        sfree(sid);

    return ret;
}

static bool getsids(char **error)
{
    SID_IDENTIFIER_AUTHORITY world_auth = SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;

    bool ret = false;

    *error = NULL;

    if (!usersid) {
        if ((usersid = get_user_sid()) == NULL) {
            *error = dupprintf("unable to construct SID for current user: %s",
                               win_strerror(GetLastError()));
            goto cleanup;
        }
    }

    if (!worldsid) {
        if (!AllocateAndInitializeSid(&world_auth, 1, SECURITY_WORLD_RID,
                                      0, 0, 0, 0, 0, 0, 0, &worldsid)) {
            *error = dupprintf("unable to construct SID for world: %s",
                               win_strerror(GetLastError()));
            goto cleanup;
        }
    }

    if (!networksid) {
        if (!AllocateAndInitializeSid(&nt_auth, 1, SECURITY_NETWORK_RID,
                                      0, 0, 0, 0, 0, 0, 0, &networksid)) {
            *error = dupprintf("unable to construct SID for "
                               "local same-user access only: %s",
                               win_strerror(GetLastError()));
            goto cleanup;
        }
    }

    ret = true;

  cleanup:
    return ret;
}


bool get_object_owner(HANDLE h, PSID *owner, PSECURITY_DESCRIPTOR *psd)
{
    DWORD needed = 0, err;
    BOOL defaulted;

    *psd = NULL;
    *owner = NULL;

    /* The first call only finds out how much space is needed */
    GetKernelObjectSecurity(h, OWNER_SECURITY_INFORMATION, NULL, 0, &needed);
    if (needed == 0)
        return false;

    *psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, needed);
    if (!*psd)
        return false;

    if (GetKernelObjectSecurity(h, OWNER_SECURITY_INFORMATION, *psd,
                                needed, &needed) &&
        GetSecurityDescriptorOwner(*psd, owner, &defaulted) && *owner)
        return true;

    err = GetLastError();
    LocalFree(*psd);
    *psd = NULL;
    *owner = NULL;
    SetLastError(err);
    return false;
}

/*
 * Make an ACL, allocated with LocalAlloc, that denies 'deny' to
 * 'denysid' and then allows 'allow' to 'allowsid'.
 */
static PACL make_acl(PSID denysid, DWORD deny, PSID allowsid, DWORD allow,
                     char **error)
{
    /* An ACCESS_DENIED_ACE is the same size as an ACCESS_ALLOWED_ACE.
     * Each ends with the first DWORD of its SID. */
    DWORD size = sizeof(ACL) +
        2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
        GetLengthSid(denysid) + GetLengthSid(allowsid);
    PACL acl = (PACL)LocalAlloc(LPTR, size);

    if (!acl) {
        *error = dupprintf("unable to allocate ACL: %s",
                           win_strerror(GetLastError()));
        return NULL;
    }

    if (!InitializeAcl(acl, size, ACL_REVISION) ||
        !AddAccessDeniedAce(acl, ACL_REVISION, deny, denysid) ||
        !AddAccessAllowedAce(acl, ACL_REVISION, allow, allowsid)) {
        *error = dupprintf("unable to construct ACL: %s",
                           win_strerror(GetLastError()));
        LocalFree(acl);
        return NULL;
    }

    return acl;
}

bool make_private_security_descriptor(DWORD permissions,
                                      PSECURITY_DESCRIPTOR *psd,
                                      PACL *acl,
                                      char **error)
{
    bool ret = false;

    *psd = NULL;
    *acl = NULL;
    *error = NULL;

    if (!getsids(error))
        goto cleanup;

    /*
     * Only the current user, and not over the network. (Later
     * versions of Windows can make a named pipe refuse remote clients
     * outright; Windows NT 3.x and 4.0 need this ACE for that.)
     */
    *acl = make_acl(networksid, permissions, usersid, permissions, error);
    if (!*acl)
        goto cleanup;

    *psd = (PSECURITY_DESCRIPTOR)
        LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!*psd) {
        *error = dupprintf("unable to allocate security descriptor: %s",
                           win_strerror(GetLastError()));
        goto cleanup;
    }

    if (!InitializeSecurityDescriptor(*psd, SECURITY_DESCRIPTOR_REVISION)) {
        *error = dupprintf("unable to initialise security descriptor: %s",
                           win_strerror(GetLastError()));
        goto cleanup;
    }

    if (!SetSecurityDescriptorOwner(*psd, usersid, false)) {
        *error = dupprintf("unable to set owner in security descriptor: %s",
                           win_strerror(GetLastError()));
        goto cleanup;
    }

    if (!SetSecurityDescriptorDacl(*psd, true, *acl, false)) {
        *error = dupprintf("unable to set DACL in security descriptor: %s",
                           win_strerror(GetLastError()));
        goto cleanup;
    }

    ret = true;

  cleanup:
    if (!ret) {
        if (*psd) {
            LocalFree(*psd);
            *psd = NULL;
        }
        if (*acl) {
            LocalFree(*acl);
            *acl = NULL;
        }
    } else {
        sfree(*error);
        *error = NULL;
    }
    return ret;
}

static bool acl_restricted = false;
bool restricted_acl(void) { return acl_restricted; }

static bool really_restrict_process_acl(char **error)
{
    bool ret = false;
    PACL acl = NULL;
    SECURITY_DESCRIPTOR sd;

    static const DWORD nastyace=WRITE_DAC | WRITE_OWNER |
        PROCESS_CREATE_PROCESS | PROCESS_CREATE_THREAD |
        PROCESS_DUP_HANDLE |
        PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
        PROCESS_SUSPEND_RESUME;

    if (!getsids(error))
        goto cleanup;

    /* Everyone: deny the nasty rights. User: allow the rest. */
    acl = make_acl(worldsid, nastyace, usersid, ~nastyace & 0x1fff, error);
    if (!acl)
        goto cleanup;

    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(&sd, usersid, false) ||
        !SetSecurityDescriptorDacl(&sd, true, acl, false) ||
        !SetKernelObjectSecurity(
            GetCurrentProcess(),
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &sd)) {
        *error = dupprintf("Unable to set process ACL: %s",
                           win_strerror(GetLastError()));
        goto cleanup;
    }

    acl_restricted = true;
    ret=true;

  cleanup:
    /* The process's security descriptor holds a copy of the ACL */
    if (acl)
        LocalFree(acl);
    return ret;
}

/*
 * Lock down our process's ACL, to present an obstacle to malware
 * trying to write into its memory. This can't be a full defence,
 * because well timed malware could attack us before this code runs -
 * even if it was unconditionally run at the very start of main(),
 * which we wouldn't want to do anyway because it turns out in practie
 * that interfering with other processes in this way has significant
 * non-infringing uses on Windows (e.g. screen reader software).
 *
 * If we've been requested to do this and are unsuccessful, bomb out
 * via modalfatalbox rather than continue in a less protected mode.
 *
 * This function is intentionally outside the #ifndef NO_SECURITY that
 * covers the rest of this file, because when PuTTY is compiled
 * without the ability to restrict its ACL, we don't want it to
 * silently pretend to honour the instruction to do so.
 */
void restrict_process_acl(void)
{
    char *error = NULL;
    bool ret;

    ret = really_restrict_process_acl(&error);
    if (!ret)
        modalfatalbox("Could not restrict process ACL: %s", error);
}
