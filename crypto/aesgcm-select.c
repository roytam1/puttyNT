#include "ssh.h"
#include "aesgcm.h"

static ssh2_mac *aesgcm_mac_selector_new(const ssh2_macalg *alg,
                                         ssh_cipher *cipher)
{
    static const ssh2_macalg *const real_algs[] = {
        &ssh2_aesgcm_mac_sw,
        NULL,
    };
    size_t i;

    for (i = 0; real_algs[i]; i++) {
        const ssh2_macalg *alg = real_algs[i];
        const struct aesgcm_extra *alg_extra =
            (const struct aesgcm_extra *)alg->extra;
        if (check_aesgcm_availability(alg_extra))
            return ssh2_mac_new(alg, cipher);
    }

    /* We should never reach the NULL at the end of the list, because
     * the last non-NULL entry should be software-only GCM, which is
     * always available. */
    unreachable("aesgcm_select ran off the end of its list");
}

const ssh2_macalg ssh2_aesgcm_mac = {
    /* .new = */ aesgcm_mac_selector_new,
    /* .free = */ NULL,
    /* .setkey = */ NULL,
    /* .start = */ NULL,
    /* .genresult = */ NULL,
    /* .next_message = */ NULL,
    /* .text_name = */ NULL,
    /* .name = */ "",
    /* .etm_name = */ "", /* Not selectable independently */
    /* .len = */ 16,
    /* .keylen = */ 0,
};
