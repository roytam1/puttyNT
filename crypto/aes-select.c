/*
 * Top-level vtables to select an AES implementation.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "aes.h"

static ssh_cipher *aes_select(const ssh_cipheralg *alg)
{
    const ssh_cipheralg *const *real_algs = (const ssh_cipheralg **)alg->extra;
    size_t i;

    for (i = 0; real_algs[i]; i++) {
        const ssh_cipheralg *alg = real_algs[i];
        const struct aes_extra *alg_extra =
            (const struct aes_extra *)alg->extra;
        if (check_availability(alg_extra))
            return ssh_cipher_new(alg);
    }

    /* We should never reach the NULL at the end of the list, because
     * the last non-NULL entry should be software-only AES, which is
     * always available. */
    unreachable("aes_select ran off the end of its list");
}

/*
 * NT port: upstream lists the AES-NI and NEON implementations here too,
 * when the compiler can build them, and passes the extra vtable fields
 * through a variadic macro. The NT compilers can build neither, and
 * have no variadic macros, so each selector has only the software
 * implementation and takes its flags and MAC as plain arguments.
 */
#define AES_SELECTOR_VTABLE(mode_c, id, mode_display, bits, flags_, mac_) \
    static const ssh_cipheralg *                                        \
    ssh_aes ## bits ## _ ## mode_c ## _impls[] = {                      \
        &ssh_aes ## bits ## _ ## mode_c ## _sw,                         \
        NULL,                                                           \
    };                                                                  \
    const ssh_cipheralg ssh_aes ## bits ## _ ## mode_c = {              \
        /* .new = */ aes_select,                                        \
        /* .free = */ NULL,                                             \
        /* .setiv = */ NULL,                                            \
        /* .setkey = */ NULL,                                           \
        /* .encrypt = */ NULL,                                          \
        /* .decrypt = */ NULL,                                          \
        /* .encrypt_length = */ NULL,                                   \
        /* .decrypt_length = */ NULL,                                   \
        /* .next_message = */ NULL,                                     \
        /* .ssh2_id = */ id,                                            \
        /* .blksize = */ 16,                                            \
        /* .real_keybits = */ bits,                                     \
        /* .padded_keybytes = */ bits/8,                                \
        /* .flags = */ flags_,                                          \
        /* .text_name = */ "AES-" #bits " " mode_display                \
        " (dummy selector vtable)",                                     \
        /* .required_mac = */ mac_,                                     \
        /* .extra = */ ssh_aes ## bits ## _ ## mode_c ## _impls,        \
    }

AES_SELECTOR_VTABLE(cbc, "aes128-cbc", "CBC", 128, SSH_CIPHER_IS_CBC, NULL);
AES_SELECTOR_VTABLE(cbc, "aes192-cbc", "CBC", 192, SSH_CIPHER_IS_CBC, NULL);
AES_SELECTOR_VTABLE(cbc, "aes256-cbc", "CBC", 256, SSH_CIPHER_IS_CBC, NULL);
AES_SELECTOR_VTABLE(sdctr, "aes128-ctr", "SDCTR", 128, 0, NULL);
AES_SELECTOR_VTABLE(sdctr, "aes192-ctr", "SDCTR", 192, 0, NULL);
AES_SELECTOR_VTABLE(sdctr, "aes256-ctr", "SDCTR", 256, 0, NULL);
AES_SELECTOR_VTABLE(gcm, "aes128-gcm@openssh.com", "GCM", 128,
                    SSH_CIPHER_SEPARATE_LENGTH, &ssh2_aesgcm_mac);
AES_SELECTOR_VTABLE(gcm, "aes256-gcm@openssh.com", "GCM", 256,
                    SSH_CIPHER_SEPARATE_LENGTH, &ssh2_aesgcm_mac);

/* 192-bit AES-GCM is included only so that testcrypt can run standard
 * test vectors against it. OpenSSH doesn't define a protocol id for
 * it. Hence setting its ssh2_id to NULL here, and more importantly,
 * leaving it out of aesgcm_list[] below. */
AES_SELECTOR_VTABLE(gcm, NULL, "GCM", 192,
                    SSH_CIPHER_SEPARATE_LENGTH, &ssh2_aesgcm_mac);

static const ssh_cipheralg ssh_rijndael_lysator = {
    /* Same as aes256_cbc, but with a different protocol ID */
    /* .new = */ aes_select,
    /* .free = */ NULL,
    /* .setiv = */ NULL,
    /* .setkey = */ NULL,
    /* .encrypt = */ NULL,
    /* .decrypt = */ NULL,
    /* .encrypt_length = */ NULL,
    /* .decrypt_length = */ NULL,
    /* .next_message = */ NULL,
    /* .ssh2_id = */ "rijndael-cbc@lysator.liu.se",
    /* .blksize = */ 16,
    /* .real_keybits = */ 256,
    /* .padded_keybytes = */ 256/8,
    /* .flags = */ SSH_CIPHER_IS_CBC,
    /* .text_name = */ "AES-256 CBC (dummy selector vtable)",
    /* .required_mac = */ NULL,
    /* .extra = */ ssh_aes256_cbc_impls,
};

static const ssh_cipheralg *const aes_list[] = {
    &ssh_aes256_sdctr,
    &ssh_aes256_cbc,
    &ssh_rijndael_lysator,
    &ssh_aes192_sdctr,
    &ssh_aes192_cbc,
    &ssh_aes128_sdctr,
    &ssh_aes128_cbc,
};

const ssh2_ciphers ssh2_aes = { lenof(aes_list), aes_list };

static const ssh_cipheralg *const aesgcm_list[] = {
    /* OpenSSH only defines protocol ids for 128- and 256-bit AES-GCM,
     * not 192-bit. */
    &ssh_aes128_gcm,
    &ssh_aes256_gcm,
};

const ssh2_ciphers ssh2_aesgcm = { lenof(aesgcm_list), aesgcm_list };
