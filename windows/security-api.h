/*
 * security-api.h: some miscellaneous security-related helper functions,
 * defined in utils/security.c. They use only the security functions
 * of advapi32.dll that Windows NT 3.1 already has.
 */

/*
 * Find the SID describing the current user. The return value (if not
 * NULL for some error-related reason) is smalloced.
 */
PSID get_user_sid(void);

/*
 * Find the owner of a kernel object, such as a named pipe or a file
 * mapping, from a handle with READ_CONTROL access to it. On success,
 * *owner points into *psd, which must be freed later using LocalFree.
 * On failure, GetLastError() says why.
 */
bool get_object_owner(HANDLE h, PSID *owner, PSECURITY_DESCRIPTOR *psd);

/*
 * Construct a PSECURITY_DESCRIPTOR of the type used for named pipe
 * servers, i.e. allowing access only to the current user id and also
 * only local (i.e. not over SMB) connections.
 *
 * If this function returns true, then 'psd' and 'acl' will have been
 * filled in with memory allocated using LocalAlloc (and hence must be
 * freed later using LocalFree). If it returns false, then instead
 * 'error' has been filled with a dynamically allocated error message.
 */
bool make_private_security_descriptor(
    DWORD permissions, PSECURITY_DESCRIPTOR *psd, PACL *acl, char **error);
