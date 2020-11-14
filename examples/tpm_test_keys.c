/* tpm_test_keys.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfTPM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* use ANSI stdio for support of format strings, must be set before
 * including stdio.h
 */
#if defined(__MINGW32__) || defined(__MINGW64__)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include "tpm_test.h"
#include "tpm_test_keys.h"
#include <wolftpm/tpm2_wrap.h>

#define RSA_FILENAME  "rsa_test_blob.raw"
#define ECC_FILENAME  "ecc_test_blob.raw"

#if 0
static int writeKeyBlob(const char* filename,
                        WOLFTPM2_KEYBLOB* key)
{
    int rc = 0;
#if !defined(NO_FILESYSTEM) && !defined(NO_WRITE_TEMP_FILES)
    XFILE  fp = NULL;
    size_t fileSz = 0;

    fp = XFOPEN(filename, "wb");
    if (fp != XBADFILE) {
        key->pub.size = sizeof(key->pub);
        fileSz += XFWRITE(&key->pub, 1, sizeof(key->pub), fp);
        fileSz += XFWRITE(&key->priv, 1, sizeof(UINT16) + key->priv.size, fp);
        XFCLOSE(fp);
    }
    printf("Wrote %d bytes to %s\n", (int)fileSz, filename);
#else
    (void)filename;
    (void)key;
#endif /* !NO_FILESYSTEM && !NO_WRITE_TEMP_FILES */
    return rc;
}
#endif

static int readKeyBlob(const char* filename, WOLFTPM2_KEYBLOB* key)
{
    int rc = 0;
#if !defined(NO_FILESYSTEM) && !defined(NO_WRITE_TEMP_FILES)
    XFILE  fp = NULL;
    size_t fileSz = 0;
    size_t bytes_read = 0;

    XMEMSET(key, 0, sizeof(WOLFTPM2_KEYBLOB));

    fp = XFOPEN(filename, "rb");
    if (fp != XBADFILE) {
        XFSEEK(fp, 0, XSEEK_END);
        fileSz = XFTELL(fp);
        XREWIND(fp);
        if (fileSz > sizeof(key->priv) + sizeof(key->pub)) {
            printf("File size check failed\n");
            rc = BUFFER_E; goto exit;
        }
        printf("Reading %d bytes from %s\n", (int)fileSz, filename);

        bytes_read = XFREAD(&key->pub, 1, sizeof(key->pub), fp);
        if (bytes_read != sizeof(key->pub)) {
            printf("Read %zu, expected public blob %zu bytes\n", bytes_read, sizeof(key->pub));
            goto exit;
        }
        if (fileSz > sizeof(key->pub)) {
            fileSz -= sizeof(key->pub);
            bytes_read = XFREAD(&key->priv, 1, fileSz, fp);
            if (bytes_read != fileSz) {
                printf("Read %zu, expected private blob %zu bytes\n", bytes_read, fileSz);
                goto exit;
            }
        }

        /* sanity check the sizes */
        if (key->pub.size != sizeof(key->pub) || key->priv.size > sizeof(key->priv.buffer)) {
            printf("Struct size check failed (pub %d, priv %d)\n",
                   key->pub.size, key->priv.size);
            rc = BUFFER_E;
        }
    }
    else {
        rc = BUFFER_E;
        printf("File %s not found!\n", filename);
        printf("Keys can be generated by running:\n"
               "  ./examples/keygen/keygen rsa_test_blob.raw RSA T\n"
               "  ./examples/keygen/keygen ecc_test_blob.raw ECC T\n");
    }

exit:
    if (fp)
      XFCLOSE(fp);
#else
    (void)filename;
    (void)key;
#endif /* !NO_FILESYSTEM && !NO_WRITE_TEMP_FILES */
    return rc;
}

static int readAndLoadKey(WOLFTPM2_DEV* pDev,
                          WOLFTPM2_KEY* key,
                          WOLFTPM2_HANDLE* parent,
                          const char* filename,
                          const byte* auth,
                          int authSz)
{
    int rc;
    WOLFTPM2_KEYBLOB keyblob;

    /* clear output key buffer */
    XMEMSET(key, 0, sizeof(WOLFTPM2_KEY));

    rc = readKeyBlob(filename, &keyblob);
    if (rc != 0) return rc;

    rc = wolfTPM2_LoadKey(pDev, &keyblob, parent);
    if (rc != TPM_RC_SUCCESS) {
        printf("wolfTPM2_LoadKey failed\n");
        return rc;
    }
    printf("Loaded key to 0x%x\n",
        (word32)keyblob.handle.hndl);

    key->handle = keyblob.handle;
    key->pub    = keyblob.pub;
    key->name   = keyblob.name;
    key->handle.auth.size = authSz;
    XMEMCPY(key->handle.auth.buffer, auth, authSz);

    return rc;
}

#if !defined(WOLFTPM2_NO_WRAPPER)
WOLFTPM_LOCAL int getPrimaryStoragekey(WOLFTPM2_DEV* pDev,
                                       WOLFTPM2_KEY* pStorageKey,
                                       TPMT_PUBLIC* pPublicTemplate)
{
    int rc = 0;

    /* Create primary storage key */
    rc = wolfTPM2_GetKeyTemplate_RSA(pPublicTemplate,
            TPMA_OBJECT_fixedTPM | TPMA_OBJECT_fixedParent |
            TPMA_OBJECT_sensitiveDataOrigin | TPMA_OBJECT_userWithAuth |
            TPMA_OBJECT_restricted | TPMA_OBJECT_decrypt | TPMA_OBJECT_noDA);
    if (rc != 0) return rc;
    rc = wolfTPM2_CreatePrimaryKey(pDev, pStorageKey, TPM_RH_OWNER,
            pPublicTemplate, (byte*)gStorageKeyAuth, sizeof(gStorageKeyAuth)-1);
    if (rc != 0) return rc;

    return rc;
}

#ifndef NO_RSA
#ifdef WOLFTPM2_NO_WOLFCRYPT
WOLFTPM_LOCAL int getRSAkey(WOLFTPM2_DEV* pDev,
                            WOLFTPM2_KEY* pStorageKey,
                            WOLFTPM2_KEY* key)
#else
WOLFTPM_LOCAL int getRSAkey(WOLFTPM2_DEV* pDev,
                            WOLFTPM2_KEY* pStorageKey,
                            WOLFTPM2_KEY* key,
                            RsaKey* pWolfRsaKey,
                            int tpmDevId)
#endif /* WOLFTPM2_NO_WOLFCRYPT */

{
    int rc = 0;

    rc = readAndLoadKey(pDev, key, &pStorageKey->handle,
                        RSA_FILENAME,
                        (byte*)gKeyAuth, sizeof(gKeyAuth)-1);
    if (rc != 0) {
        return rc;
    }

#if !defined(WOLFTPM2_NO_WOLFCRYPT)
    /* setup wolf RSA key with TPM deviceID, so crypto callbacks are used */
    rc = wc_InitRsaKey_ex(pWolfRsaKey, NULL, tpmDevId);
    if (rc != 0) return rc;

    /* load public portion of key into wolf RSA Key */
    rc = wolfTPM2_RsaKey_TpmToWolf(pDev, key, pWolfRsaKey);
#endif /* !defined(WOLFTPM2_NO_WOLFCRYPT) */

    return rc;
}
#endif /* !NO_RSA */


#ifdef HAVE_ECC
#ifdef WOLFTPM2_NO_WOLFCRYPT
WOLFTPM_LOCAL int getECCkey(WOLFTPM2_DEV* pDev,
                            WOLFTPM2_KEY* pStorageKey,
                            WOLFTPM2_KEY* key)
#else
WOLFTPM_LOCAL int getECCkey(WOLFTPM2_DEV* pDev,
                            WOLFTPM2_KEY* pStorageKey,
                            WOLFTPM2_KEY* key,
                            ecc_key* pWolfEccKey,
                            int tpmDevId)
#endif
{
    int rc = 0;

    /* Create/Load ECC key */
    rc = readAndLoadKey(pDev, key, &pStorageKey->handle,
                        ECC_FILENAME,
                        (byte*)gKeyAuth, sizeof(gKeyAuth)-1);
    if (rc != 0) {
        return rc;
    }
#if !defined(WOLFTPM2_NO_WOLFCRYPT)
    /* setup wolf ECC key with TPM deviceID, so crypto callbacks are used */
    rc = wc_ecc_init_ex(pWolfEccKey, NULL, tpmDevId);
    if (rc != 0) return rc;

    /* load public portion of key into wolf ECC Key */
    rc = wolfTPM2_EccKey_TpmToWolf(pDev, key, pWolfEccKey);
#endif /* !defined(WOLFTPM2_NO_WOLFCRYPT) */

    return rc;
}
#endif /* HAVE_ECC */
#endif /* !defined(WOLFTPM2_NO_WRAPPER) */