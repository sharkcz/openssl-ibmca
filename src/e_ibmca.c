/*
 * Copyright [2005-2017] International Business Machines Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * Digest and Cipher support added by Robert H Burroughs (burrough@us.ibm.com).
 *
 * DES/3DES/AES-CFB/OFB support added by Kent Yoder (yoder1@us.ibm.com)
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/sha.h>
#include <openssl/obj_mac.h>
#include <openssl/aes.h>

#ifndef OPENSSL_NO_HW
#ifndef OPENSSL_NO_HW_IBMCA

#if OPENSSL_VERSION_NUMBER < 0x10100000L
 #define OLDER_OPENSSL
#endif

#include <ica_api.h>
#include "e_ibmca_err.h"

#define IBMCA_LIB_NAME "ibmca engine"
#define LIBICA_SHARED_LIB "libica.so"

#define AP_PATH "/sys/devices/ap"

/* COMPAT MACROS */
#ifdef OLDER_OPENSSL
 #define EVP_CIPHER_CTX_get_cipher_data(ctx)	((ctx)->cipher_data)
 #define EVP_CIPHER_CTX_original_iv(ctx)	((ctx)->oiv)
 #define EVP_CIPHER_CTX_iv_noconst(ctx)		((ctx)->iv)
 #define EVP_CIPHER_CTX_encrypting(ctx)		((ctx)->encrypt)
 #define EVP_CIPHER_CTX_buf_noconst(ctx)	((ctx)->buf)
#else
 #define EVP_CTRL_GCM_SET_IVLEN			EVP_CTRL_AEAD_SET_IVLEN
 #define EVP_CTRL_GCM_SET_TAG			EVP_CTRL_AEAD_SET_TAG
 #define EVP_CTRL_GCM_GET_TAG			EVP_CTRL_AEAD_GET_TAG
#endif

#if !defined(NID_aes_128_gcm) || \
    !defined(NID_aes_192_gcm) || \
    !defined(NID_aes_256_gcm)
 #ifndef OPENSSL_NO_AES_GCM
  #define OPENSSL_NO_AES_GCM
 #endif
#endif
#ifndef EVP_AEAD_TLS1_AAD_LEN
 #define EVP_AEAD_TLS1_AAD_LEN			13
#endif
#ifndef EVP_MD_FLAG_PKEY_METHOD_SIGNATURE
 #define EVP_MD_FLAG_PKEY_METHOD_SIGNATURE	0
#endif

typedef struct ibmca_des_context {
	unsigned char key[sizeof(ica_des_key_triple_t)];
} ICA_DES_CTX;

typedef struct ibmca_aes_128_context {
	unsigned char key[sizeof(ica_aes_key_len_128_t)];
} ICA_AES_128_CTX;

typedef struct ibmca_aes_192_context {
	unsigned char key[sizeof(ica_aes_key_len_192_t)];
} ICA_AES_192_CTX;

typedef struct ibmca_aes_256_context {
	unsigned char key[sizeof(ica_aes_key_len_256_t)];
} ICA_AES_256_CTX;

typedef struct ibmca_aes_gcm_context {
	unsigned char key[32];
	int key_set;
	int iv_set;

	unsigned char tag[16];
	unsigned char subkey[16];
	unsigned char icb[16];
	unsigned char ucb[16];
	unsigned long long ptlen;
	unsigned long long aadlen;

	unsigned char *iv;
	int ivlen;
	int taglen;
	int iv_gen;
	int tls_aadlen;

} ICA_AES_GCM_CTX;

#ifndef OPENSSL_NO_SHA1
#define SHA_BLOCK_SIZE 64
typedef struct ibmca_sha1_ctx {
	sha_context_t c;
	unsigned char tail[SHA_BLOCK_SIZE];
	unsigned int tail_len;
} IBMCA_SHA_CTX;
#endif

#ifndef OPENSSL_NO_SHA256
#define SHA256_BLOCK_SIZE 64
typedef struct ibmca_sha256_ctx {
	sha256_context_t c;
	unsigned char tail[SHA256_BLOCK_SIZE];
	unsigned int tail_len;
} IBMCA_SHA256_CTX;
#endif

#ifndef OPENSSL_NO_SHA512
#define SHA512_BLOCK_SIZE 128
typedef struct ibmca_sha512_ctx {
	sha512_context_t c;
	unsigned char tail[SHA512_BLOCK_SIZE];
	unsigned int tail_len;
} IBMCA_SHA512_CTX;
#endif

static const char *LIBICA_NAME = "ica";

#if defined(NID_aes_128_cfb128) && ! defined (NID_aes_128_cfb)
#define NID_aes_128_cfb NID_aes_128_cfb128
#endif

#if defined(NID_aes_128_ofb128) && ! defined (NID_aes_128_ofb)
#define NID_aes_128_ofb NID_aes_128_ofb128
#endif

#if defined(NID_aes_192_cfb128) && ! defined (NID_aes_192_cfb)
#define NID_aes_192_cfb NID_aes_192_cfb128
#endif

#if defined(NID_aes_192_ofb128) && ! defined (NID_aes_192_ofb)
#define NID_aes_192_ofb NID_aes_192_ofb128
#endif

#if defined(NID_aes_256_cfb128) && ! defined (NID_aes_256_cfb)
#define NID_aes_256_cfb NID_aes_256_cfb128
#endif

#if defined(NID_aes_256_ofb128) && ! defined (NID_aes_256_ofb)
#define NID_aes_256_ofb NID_aes_256_ofb128
#endif

#if defined(NID_des_ofb64) && ! defined (NID_des_ofb)
#define NID_des_ofb NID_des_ofb64
#endif

#if defined(NID_des_ede3_ofb64) && ! defined (NID_des_ede3_ofb)
#define NID_des_ede3_ofb NID_des_ede3_ofb64
#endif

#if defined(NID_des_cfb64) && ! defined (NID_des_cfb)
#define NID_des_cfb NID_des_cfb64
#endif

#if defined(NID_des_ede3_cfb64) && ! defined (NID_des_ede3_cfb)
#define NID_des_ede3_cfb NID_des_ede3_cfb64
#endif

/*
 * ibmca_crypto_algos lists the supported crypto algos by ibmca.
 * This list is matched against all algo support by libica. Only if
 * the algo is in this list it is activated in ibmca.
 * The defines can be found in the libica header file.
 */
static int ibmca_crypto_algos[] = {
        SHA1,
        SHA256,
        SHA512,
        P_RNG,
        RSA_ME,
        RSA_CRT,
        DES_ECB,
        DES_CBC,
        DES_OFB,
        DES_CFB,
        DES3_ECB,
        DES3_CBC,
        DES3_OFB,
        DES3_CFB,
        DES3_CTR,
        AES_ECB,
        AES_CBC,
        AES_OFB,
        AES_CFB,
	AES_GCM_KMA,
        0
};


#define MAX_CIPHER_NIDS sizeof(ibmca_crypto_algos)
/*
 * This struct maps one NID to one crypto algo.
 * So we can tell OpenSSL thsi NID maps to this function.
 */
struct crypto_pair
{
        int nids[MAX_CIPHER_NIDS];
        const void *crypto_meths[MAX_CIPHER_NIDS];
};

/* We can not say how much crypto algos are
 * supported by libica. We can only say the
 * size is not greater as the supported
 * crypto algos by ibmca.
 * The actual number of supported crypto algos
 * is saved to the size_****_nid variabes
 */
static size_t size_cipher_list = 0;
static size_t size_digest_list = 0;

static struct crypto_pair ibmca_cipher_lists;
static struct crypto_pair ibmca_digest_lists;


static int ibmca_destroy(ENGINE * e);
static int ibmca_init(ENGINE * e);
static int ibmca_finish(ENGINE * e);
static int ibmca_ctrl(ENGINE * e, int cmd, long i, void *p, void (*f) ());

static ica_adapter_handle_t ibmca_handle = 0;

/* BIGNUM stuff */
static int ibmca_mod_exp(BIGNUM * r, const BIGNUM * a, const BIGNUM * p,
			 const BIGNUM * m, BN_CTX * ctx);

static int ibmca_mod_exp_crt(BIGNUM * r, const BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * q,
			     const BIGNUM * dmp1, const BIGNUM * dmq1,
			     const BIGNUM * iqmp, BN_CTX * ctx);

#ifndef OPENSSL_NO_RSA
/* RSA stuff */
static int ibmca_rsa_mod_exp(BIGNUM * r0, const BIGNUM * I, RSA * rsa,
                             BN_CTX *ctx);

static int ibmca_rsa_init(RSA *rsa);
#endif

/* This function is aliased to mod_exp (with the mont stuff dropped). */
static int ibmca_mod_exp_mont(BIGNUM * r, const BIGNUM * a,
			      const BIGNUM * p, const BIGNUM * m,
			      BN_CTX * ctx, BN_MONT_CTX * m_ctx);

#ifndef OPENSSL_NO_DSA
/* DSA stuff */
#ifdef OLDER_OPENSSL
static int ibmca_dsa_mod_exp(DSA * dsa, BIGNUM * rr, BIGNUM * a1,
			     BIGNUM * p1, BIGNUM * a2, BIGNUM * p2,
			     BIGNUM * m, BN_CTX * ctx,
			     BN_MONT_CTX * in_mont);
static int ibmca_mod_exp_dsa(DSA * dsa, BIGNUM * r, BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * m_ctx);
#else
static int ibmca_dsa_mod_exp(DSA * dsa, BIGNUM * rr, const BIGNUM * a1,
			     const BIGNUM * p1, const BIGNUM * a2,
			     const BIGNUM * p2, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * in_mont);
static int ibmca_mod_exp_dsa(DSA * dsa, BIGNUM * r, const BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * m_ctx);
#endif
#endif

#ifndef OPENSSL_NO_DH
/* DH stuff */
/* This function is alised to mod_exp (with the DH and mont dropped). */
static int ibmca_mod_exp_dh(const DH * dh, BIGNUM * r,
			    const BIGNUM * a, const BIGNUM * p,
			    const BIGNUM * m, BN_CTX * ctx,
			    BN_MONT_CTX * m_ctx);
#endif

/* RAND stuff */
static int ibmca_rand_bytes(unsigned char *buf, int num);
static int ibmca_rand_status(void);

/* DES, TDES, AES declarations */
static int ibmca_usable_ciphers(const int **nids);

static int ibmca_engine_ciphers(ENGINE * e, const EVP_CIPHER ** cipher,
				const int **nids, int nid);

static int ibmca_init_key(EVP_CIPHER_CTX * ctx, const unsigned char *key,
			  const unsigned char *iv, int enc);

static int ibmca_des_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
			    const unsigned char *in, size_t inlen);

static int ibmca_tdes_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
			     const unsigned char *in, size_t inlen);

static int ibmca_aes_128_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
				const unsigned char *in, size_t inlen);

static int ibmca_aes_192_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
				const unsigned char *in, size_t inlen);

static int ibmca_aes_256_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
				const unsigned char *in, size_t inlen);

static int ibmca_cipher_cleanup(EVP_CIPHER_CTX * ctx);

#ifndef OPENSSL_NO_AES_GCM
static int ibmca_aes_gcm_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc);
static int ibmca_aes_gcm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                    const unsigned char *in, size_t len);
static int ibmca_gcm_aad(ICA_AES_GCM_CTX *ctx, const unsigned char *aad,
			 size_t len, int enc, int keylen);
static int ibmca_aes_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len);
static int ibmca_aes_gcm(ICA_AES_GCM_CTX *ctx, const unsigned char *in,
                         unsigned char *out, size_t len, int enc, int keylen);
static int ibmca_aes_gcm_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg,
			      void *ptr);
static int ibmca_aes_gcm_setiv(EVP_CIPHER_CTX *c);
static int ibmca_gcm_tag(EVP_CIPHER_CTX *ctx, unsigned char *out,
			 const unsigned char *in, int taglen);
#endif

/* Sha1 stuff */
static int ibmca_usable_digests(const int **nids);

static int ibmca_engine_digests(ENGINE * e, const EVP_MD ** digest,
				const int **nids, int nid);

#ifndef OPENSSL_NO_SHA1
static int ibmca_sha1_init(EVP_MD_CTX * ctx);

static int ibmca_sha1_update(EVP_MD_CTX * ctx, const void *data,
			     unsigned long count);

static int ibmca_sha1_final(EVP_MD_CTX * ctx, unsigned char *md);

static int ibmca_sha1_cleanup(EVP_MD_CTX * ctx);
#endif

#ifndef OPENSSL_NO_SHA256
static int ibmca_sha256_init(EVP_MD_CTX * ctx);

static int ibmca_sha256_update(EVP_MD_CTX * ctx, const void *data,
			       unsigned long count);

static int ibmca_sha256_final(EVP_MD_CTX * ctx, unsigned char *md);

static int ibmca_sha256_cleanup(EVP_MD_CTX * ctx);
#endif

#ifndef OPENSSL_NO_SHA512
static int ibmca_sha512_init(EVP_MD_CTX * ctx);

static int ibmca_sha512_update(EVP_MD_CTX * ctx, const void *data,
			       unsigned long count);

static int ibmca_sha512_final(EVP_MD_CTX * ctx, unsigned char *md);

static int ibmca_sha512_cleanup(EVP_MD_CTX * ctx);
#endif

/* WJH - check for more commands, like in nuron */

/* The definitions for control commands specific to this engine */
#define IBMCA_CMD_SO_PATH		ENGINE_CMD_BASE
static const ENGINE_CMD_DEFN ibmca_cmd_defns[] = {
	{IBMCA_CMD_SO_PATH,
	 "SO_PATH",
	 "Specifies the path to the 'atasi' shared library",
	 ENGINE_CMD_FLAG_STRING},
	{0, NULL, NULL, 0}
};

#ifndef OPENSSL_NO_RSA
/* Our internal RSA_METHOD that we provide pointers to */
#ifdef OLDER_OPENSSL
static RSA_METHOD ibmca_rsa = {
	"Ibmca RSA method",      /* name */
	NULL,                    /* rsa_pub_enc */
	NULL,                    /* rsa_pub_dec */
	NULL,                    /* rsa_priv_enc */
	NULL,                    /* rsa_priv_dec */
	ibmca_rsa_mod_exp,       /* rsa_mod_exp */
	ibmca_mod_exp_mont,      /* bn_mod_exp */
	ibmca_rsa_init,          /* init */
	NULL,                    /* finish */
	0,                       /* flags */
	NULL,                    /* app_data */
	NULL,                    /* rsa_sign */
	NULL,                    /* rsa_verify */
	NULL                     /* rsa_keygen */
};
#else
static RSA_METHOD *ibmca_rsa = NULL;
#endif
#endif

#ifndef OPENSSL_NO_DSA
/* Our internal DSA_METHOD that we provide pointers to */
#ifdef OLDER_OPENSSL
static DSA_METHOD ibmca_dsa = {
	"Ibmca DSA method",     /* name */
	NULL,			/* dsa_do_sign */
	NULL,			/* dsa_sign_setup */
	NULL,			/* dsa_do_verify */
	ibmca_dsa_mod_exp,	/* dsa_mod_exp */
	ibmca_mod_exp_dsa,	/* bn_mod_exp */
	NULL,			/* init */
	NULL,			/* finish */
	0,			/* flags */
	NULL			/* app_data */
};
#else
static DSA_METHOD *ibmca_dsa = NULL;
#endif
#endif

#ifndef OPENSSL_NO_DH
/* Our internal DH_METHOD that we provide pointers to */
#ifdef OLDER_OPENSSL
static DH_METHOD ibmca_dh = {
	"Ibmca DH method",     /* name */
	NULL,                  /* generate_key */
	NULL,                  /* compute_key */
	ibmca_mod_exp_dh,      /* bn_mod_exp */
	NULL,                  /* init */
	NULL,                  /* finish */
	0,                     /* flags */
	NULL                   /* app_data */
};
#else
static DH_METHOD *ibmca_dh = NULL;
#endif
#endif

static RAND_METHOD ibmca_rand = {
	/* "IBMCA RAND method", */
	NULL,                  /* seed */
	ibmca_rand_bytes,      /* bytes */
	NULL,                  /* cleanup */
	NULL,                  /* add */
	ibmca_rand_bytes,      /* pseudorand */
	ibmca_rand_status,     /* status */
};

#ifdef OLDER_OPENSSL
/* DES ECB EVP */
const EVP_CIPHER ibmca_des_ecb = {
	NID_des_ecb,                  /* nid */
	sizeof(ica_des_vector_t),     /* block_size */
	sizeof(ica_des_key_single_t), /* key_len */
	sizeof(ica_des_vector_t),     /* iv_len */
	EVP_CIPH_ECB_MODE,            /* flags */
	ibmca_init_key,               /* init */
	ibmca_des_cipher,             /* do_cipher */
	ibmca_cipher_cleanup,         /* cleanup */
	sizeof(struct ibmca_des_context), /* ctx_size */
	EVP_CIPHER_set_asn1_iv,       /* set_asn1_parameters */
	EVP_CIPHER_get_asn1_iv,       /* get_asn1_parameters */
	NULL,                         /* ctrl */
	NULL                          /* app_data */
};

/* DES CBC EVP */
const EVP_CIPHER ibmca_des_cbc = {
	NID_des_cbc,
	sizeof(ica_des_vector_t),
	sizeof(ica_des_key_single_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_CBC_MODE,
	ibmca_init_key,
	ibmca_des_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};

/* DES OFB EVP */
const EVP_CIPHER ibmca_des_ofb = {
	NID_des_ofb,
	1, // stream cipher needs blocksize set to 1
	sizeof(ica_des_key_single_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_OFB_MODE,
	ibmca_init_key, /* XXX check me */
	ibmca_des_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};

/* DES CFB EVP */
const EVP_CIPHER ibmca_des_cfb = {
	NID_des_cfb,
	1, // stream cipher needs blocksize set to 1
	sizeof(ica_des_key_single_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_CFB_MODE,
	ibmca_init_key, /* XXX check me */
	ibmca_des_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};
#else
#define EVP_CIPHER_block_size_ECB       sizeof(ica_des_vector_t)
#define EVP_CIPHER_block_size_CBC       sizeof(ica_des_vector_t)
#define EVP_CIPHER_block_size_OFB       1
#define EVP_CIPHER_block_size_CFB	1

#define DECLARE_DES_EVP(lmode,umode)								\
static EVP_CIPHER *des_##lmode = NULL;								\
static const EVP_CIPHER *ibmca_des_##lmode(void)						\
{												\
	if (des_##lmode == NULL) {								\
		EVP_CIPHER *cipher;								\
		if (( cipher = EVP_CIPHER_meth_new(NID_des_##lmode,				\
						EVP_CIPHER_block_size_##umode,      	   	\
						sizeof(ica_des_key_single_t))) == NULL  	\
		   || !EVP_CIPHER_meth_set_iv_length(cipher, sizeof(ica_des_vector_t))		\
		   || !EVP_CIPHER_meth_set_flags(cipher,EVP_CIPH_##umode##_MODE)		\
		   || !EVP_CIPHER_meth_set_init(cipher, ibmca_init_key)				\
		   || !EVP_CIPHER_meth_set_do_cipher(cipher, ibmca_des_cipher)			\
		   || !EVP_CIPHER_meth_set_cleanup(cipher, ibmca_cipher_cleanup)		\
		   || !EVP_CIPHER_meth_set_impl_ctx_size(cipher,				\
							sizeof(struct ibmca_des_context))	\
		   || !EVP_CIPHER_meth_set_set_asn1_params(cipher, EVP_CIPHER_set_asn1_iv) 	\
		   || !EVP_CIPHER_meth_set_get_asn1_params(cipher, EVP_CIPHER_get_asn1_iv)) {	\
			EVP_CIPHER_meth_free(cipher);					        \
			cipher = NULL;                           				\
		}										\
		des_##lmode = cipher;								\
	}											\
	return des_##lmode;									\
}												\
												\
static void ibmca_des_##lmode##_destroy(void)							\
{												\
	EVP_CIPHER_meth_free(des_##lmode);							\
	des_##lmode = NULL;									\
}

DECLARE_DES_EVP(ecb, ECB)
DECLARE_DES_EVP(cbc, CBC)
DECLARE_DES_EVP(ofb, OFB)
DECLARE_DES_EVP(cfb, CFB)
#endif

#ifdef OLDER_OPENSSL
/* 3DES ECB EVP	*/
const EVP_CIPHER ibmca_tdes_ecb = {
	NID_des_ede3_ecb,
	sizeof(ica_des_vector_t),
	sizeof(ica_des_key_triple_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_ECB_MODE,
	ibmca_init_key,
	ibmca_tdes_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};

/* 3DES CBC EVP	*/
const EVP_CIPHER ibmca_tdes_cbc = {
	NID_des_ede3_cbc,
	sizeof(ica_des_vector_t),
	sizeof(ica_des_key_triple_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_CBC_MODE,
	ibmca_init_key,
	ibmca_tdes_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};

/* 3DES OFB EVP */
const EVP_CIPHER ibmca_tdes_ofb = {
	NID_des_ede3_ofb,
	1, // stream cipher needs blocksize set to 1
	sizeof(ica_des_key_triple_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_OFB_MODE,
	ibmca_init_key, /* XXX check me */
	ibmca_tdes_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};

/* 3DES CFB EVP */
const EVP_CIPHER ibmca_tdes_cfb = {
	NID_des_ede3_cfb,
	1, // stream cipher needs blocksize set to 1
	sizeof(ica_des_key_triple_t),
	sizeof(ica_des_vector_t),
	EVP_CIPH_CFB_MODE,
	ibmca_init_key, /* XXX check me */
	ibmca_tdes_cipher,
	ibmca_cipher_cleanup,
	sizeof(struct ibmca_des_context),
	EVP_CIPHER_set_asn1_iv,
	EVP_CIPHER_get_asn1_iv,
	NULL,
	NULL
};
#else
#define DECLARE_TDES_EVP(lmode,umode)								\
static EVP_CIPHER *tdes_##lmode = NULL;								\
static const EVP_CIPHER *ibmca_tdes_##lmode(void)						\
{												\
	if (tdes_##lmode == NULL) {								\
		EVP_CIPHER *cipher;								\
		if (( cipher = EVP_CIPHER_meth_new(NID_des_ede3_##lmode,			\
						EVP_CIPHER_block_size_##umode,      	   	\
						sizeof(ica_des_key_triple_t))) == NULL  	\
		   || !EVP_CIPHER_meth_set_iv_length(cipher, sizeof(ica_des_vector_t))		\
		   || !EVP_CIPHER_meth_set_flags(cipher,EVP_CIPH_##umode##_MODE)		\
		   || !EVP_CIPHER_meth_set_init(cipher, ibmca_init_key)				\
		   || !EVP_CIPHER_meth_set_do_cipher(cipher, ibmca_tdes_cipher)			\
		   || !EVP_CIPHER_meth_set_cleanup(cipher, ibmca_cipher_cleanup)		\
		   || !EVP_CIPHER_meth_set_impl_ctx_size(cipher,				\
							   sizeof(struct ibmca_des_context))	\
		   || !EVP_CIPHER_meth_set_set_asn1_params(cipher, EVP_CIPHER_set_asn1_iv) 	\
		   || !EVP_CIPHER_meth_set_get_asn1_params(cipher, EVP_CIPHER_get_asn1_iv)) {	\
			EVP_CIPHER_meth_free(cipher);					        \
			cipher = NULL;                           				\
		}										\
		tdes_##lmode = cipher;								\
	}											\
	return tdes_##lmode;									\
}												\
												\
static void ibmca_tdes_##lmode##_destroy(void)							\
{												\
	EVP_CIPHER_meth_free(tdes_##lmode);							\
	tdes_##lmode = NULL;									\
}

DECLARE_TDES_EVP(ecb, ECB)
DECLARE_TDES_EVP(cbc, CBC)
DECLARE_TDES_EVP(ofb, OFB)
DECLARE_TDES_EVP(cfb, CFB)
#endif

#ifdef OLDER_OPENSSL
# define DECLARE_AES_EVP(kbits, mode, block_size, key_len, iv_len,	\
			flags, ctx_size, init, do_cipher, cleanup,	\
			set_asn1_parameters, get_asn1_parameters, ctrl)	\
const EVP_CIPHER aes_##kbits##_##mode = {				\
	NID_aes_##kbits##_##mode,					\
	block_size,							\
	key_len,							\
	iv_len,								\
	flags,								\
	init,								\
	do_cipher,							\
	cleanup,							\
	ctx_size,							\
	set_asn1_parameters,						\
	get_asn1_parameters,						\
	ctrl,								\
	NULL								\
};									\
static const EVP_CIPHER *ibmca_aes_##kbits##_##mode(void)		\
{									\
	return &aes_##kbits##_##mode;					\
}
#else
# define DECLARE_AES_EVP(kbits, mode, block_size, key_len, iv_len,	\
			 flags,	ctx_size, init, do_cipher, cleanup,	\
			 set_asn1_parameters, get_asn1_parameters, ctrl)\
static EVP_CIPHER *aes_##kbits##_##mode = NULL;				\
static const EVP_CIPHER *ibmca_aes_##kbits##_##mode(void)		\
{									\
	EVP_CIPHER *cipher;						\
									\
	if (aes_##kbits##_##mode != NULL)				\
		goto done;						\
									\
	if ((cipher = EVP_CIPHER_meth_new(NID_aes_##kbits##_##mode,	\
					  block_size, key_len)) == NULL	\
	   || !EVP_CIPHER_meth_set_iv_length(cipher, iv_len)		\
	   || !EVP_CIPHER_meth_set_flags(cipher, flags)			\
	   || !EVP_CIPHER_meth_set_init(cipher, init)			\
	   || !EVP_CIPHER_meth_set_do_cipher(cipher, do_cipher)		\
	   || !EVP_CIPHER_meth_set_cleanup(cipher, cleanup)		\
	   || !EVP_CIPHER_meth_set_impl_ctx_size(cipher, ctx_size)	\
	   || !EVP_CIPHER_meth_set_set_asn1_params(cipher,		\
						   set_asn1_parameters)	\
	   || !EVP_CIPHER_meth_set_get_asn1_params(cipher,		\
						   get_asn1_parameters)	\
	   || !EVP_CIPHER_meth_set_ctrl(cipher, ctrl)) {		\
		EVP_CIPHER_meth_free(cipher);				\
		cipher = NULL;                           		\
	}								\
	aes_##kbits##_##mode = cipher;					\
done:									\
	return aes_##kbits##_##mode;					\
}									\
									\
static void ibmca_aes_##kbits##_##mode##_destroy(void)			\
{									\
	EVP_CIPHER_meth_free(aes_##kbits##_##mode);			\
	aes_##kbits##_##mode = NULL;					\
}
#endif

DECLARE_AES_EVP(128, ecb, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_128_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_ECB_MODE, sizeof(ICA_AES_128_CTX),
		ibmca_init_key, ibmca_aes_128_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(128, cbc, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_128_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_CBC_MODE, sizeof(ICA_AES_128_CTX),
		ibmca_init_key, ibmca_aes_128_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(128, ofb, 1, sizeof(ica_aes_key_len_128_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_OFB_MODE,
		sizeof(ICA_AES_128_CTX), ibmca_init_key,
		ibmca_aes_128_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(128, cfb, 1, sizeof(ica_aes_key_len_128_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_CFB_MODE,
		sizeof(ICA_AES_128_CTX), ibmca_init_key,
		ibmca_aes_128_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
#ifndef OPENSSL_NO_AES_GCM
DECLARE_AES_EVP(128, gcm, 1, sizeof(ica_aes_key_len_128_t),
		sizeof(ica_aes_vector_t) - sizeof(uint32_t),
		EVP_CIPH_GCM_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1
		| EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER
		| EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT
		| EVP_CIPH_CUSTOM_COPY | EVP_CIPH_FLAG_AEAD_CIPHER,
		sizeof(ICA_AES_GCM_CTX),
		ibmca_aes_gcm_init_key, ibmca_aes_gcm_cipher, NULL, NULL,
		NULL, ibmca_aes_gcm_ctrl)
#endif

DECLARE_AES_EVP(192, ecb, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_192_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_ECB_MODE, sizeof(ICA_AES_192_CTX),
		ibmca_init_key, ibmca_aes_192_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(192, cbc, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_192_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_CBC_MODE, sizeof(ICA_AES_192_CTX),
		ibmca_init_key, ibmca_aes_192_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(192, ofb, 1, sizeof(ica_aes_key_len_192_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_OFB_MODE,
		sizeof(ICA_AES_192_CTX), ibmca_init_key,
		ibmca_aes_192_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(192, cfb, 1, sizeof(ica_aes_key_len_192_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_CFB_MODE,
		sizeof(ICA_AES_192_CTX), ibmca_init_key,
		ibmca_aes_192_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
#ifndef OPENSSL_NO_AES_GCM
DECLARE_AES_EVP(192, gcm, 1, sizeof(ica_aes_key_len_192_t),
		sizeof(ica_aes_vector_t) - sizeof(uint32_t),
		EVP_CIPH_GCM_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1
		| EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER
		| EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT
		| EVP_CIPH_CUSTOM_COPY | EVP_CIPH_FLAG_AEAD_CIPHER,
		sizeof(ICA_AES_GCM_CTX),
		ibmca_aes_gcm_init_key, ibmca_aes_gcm_cipher, NULL, NULL,
		NULL, ibmca_aes_gcm_ctrl)
#endif

DECLARE_AES_EVP(256, ecb, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_256_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_ECB_MODE, sizeof(ICA_AES_256_CTX),
		ibmca_init_key, ibmca_aes_256_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(256, cbc, sizeof(ica_aes_vector_t),
		sizeof(ica_aes_key_len_256_t), sizeof(ica_aes_vector_t),
		EVP_CIPH_CBC_MODE, sizeof(ICA_AES_256_CTX),
		ibmca_init_key, ibmca_aes_256_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(256, ofb, 1, sizeof(ica_aes_key_len_256_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_OFB_MODE,
		sizeof(ICA_AES_256_CTX), ibmca_init_key,
		ibmca_aes_256_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
DECLARE_AES_EVP(256, cfb, 1, sizeof(ica_aes_key_len_256_t),
		sizeof(ica_aes_vector_t), EVP_CIPH_CFB_MODE,
		sizeof(ICA_AES_256_CTX), ibmca_init_key,
		ibmca_aes_256_cipher, ibmca_cipher_cleanup,
		EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)
#ifndef OPENSSL_NO_AES_GCM
DECLARE_AES_EVP(256, gcm, 1, sizeof(ica_aes_key_len_256_t),
		sizeof(ica_aes_vector_t) - sizeof(uint32_t),
		EVP_CIPH_GCM_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1
		| EVP_CIPH_CUSTOM_IV | EVP_CIPH_FLAG_CUSTOM_CIPHER
		| EVP_CIPH_ALWAYS_CALL_INIT | EVP_CIPH_CTRL_INIT
		| EVP_CIPH_CUSTOM_COPY | EVP_CIPH_FLAG_AEAD_CIPHER,
		sizeof(ICA_AES_GCM_CTX),
		ibmca_aes_gcm_init_key, ibmca_aes_gcm_cipher, NULL, NULL,
		NULL, ibmca_aes_gcm_ctrl)
#endif

#ifdef OLDER_OPENSSL
#ifndef OPENSSL_NO_SHA1
static const EVP_MD ibmca_sha1 = {
	NID_sha1,
	NID_sha1WithRSAEncryption,
	SHA_HASH_LENGTH,
	EVP_MD_FLAG_PKEY_METHOD_SIGNATURE|EVP_MD_FLAG_FIPS,
	ibmca_sha1_init,
	ibmca_sha1_update,
	ibmca_sha1_final,
	NULL,
	ibmca_sha1_cleanup,
	EVP_PKEY_RSA_method,
	SHA_BLOCK_SIZE,
	sizeof(EVP_MD *) + sizeof(struct ibmca_sha1_ctx)
};
#endif

#ifndef OPENSSL_NO_SHA256
static const EVP_MD ibmca_sha256 = {
	NID_sha256,
	NID_sha256WithRSAEncryption,
	SHA256_HASH_LENGTH,
	EVP_MD_FLAG_PKEY_METHOD_SIGNATURE|EVP_MD_FLAG_FIPS,
	ibmca_sha256_init,
	ibmca_sha256_update,
	ibmca_sha256_final,
	NULL,
	ibmca_sha256_cleanup,
	EVP_PKEY_RSA_method,
	SHA256_BLOCK_SIZE,
	sizeof(EVP_MD *) + sizeof(struct ibmca_sha256_ctx)
};
#endif

#ifndef OPENSSL_NO_SHA512
static const EVP_MD ibmca_sha512 = {
	NID_sha512,
	NID_sha512WithRSAEncryption,
	SHA512_HASH_LENGTH,
	EVP_MD_FLAG_PKEY_METHOD_SIGNATURE|EVP_MD_FLAG_FIPS,
	ibmca_sha512_init,
	ibmca_sha512_update,
	ibmca_sha512_final,
	NULL,
	ibmca_sha512_cleanup,
	EVP_PKEY_RSA_method,
	SHA512_BLOCK_SIZE,
	sizeof(EVP_MD *) + sizeof(struct ibmca_sha512_ctx)
};
#endif

#else

#define DECLARE_SHA_EVP(sha,len)								\
static EVP_MD *sha##_md = NULL;									\
static const EVP_MD *ibmca_##sha(void)								\
{												\
	if (sha##_md == NULL) {									\
		EVP_MD *md;									\
		if (( md = EVP_MD_meth_new(NID_##sha,						\
					   NID_##sha##WithRSAEncryption)) == NULL	  	\
		   || !EVP_MD_meth_set_result_size(md, len##_HASH_LENGTH)			\
		   || !EVP_MD_meth_set_input_blocksize(md, len##_BLOCK_SIZE)			\
		   || !EVP_MD_meth_set_app_datasize(md, sizeof(EVP_MD *) + 			\
							   sizeof(struct ibmca_##sha##_ctx))	\
		   || !EVP_MD_meth_set_flags(md, EVP_MD_FLAG_FIPS)					\
		   || !EVP_MD_meth_set_init(md, ibmca_##sha##_init)				\
		   || !EVP_MD_meth_set_update(md, ibmca_##sha##_update)				\
		   || !EVP_MD_meth_set_final(md, ibmca_##sha##_final)			 	\
		   || !EVP_MD_meth_set_cleanup(md, ibmca_##sha##_cleanup)) {			\
			EVP_MD_meth_free(md);					        	\
			md = NULL;                           					\
		}										\
		sha##_md = md;									\
	}											\
	return sha##_md;									\
}												\
												\
static void ibmca_##sha##_destroy(void)								\
{												\
	EVP_MD_meth_free(sha##_md);								\
	sha##_md = NULL;									\
}

DECLARE_SHA_EVP(sha1, SHA)
DECLARE_SHA_EVP(sha256, SHA256)
DECLARE_SHA_EVP(sha512, SHA512)
#endif

/* Constants used when creating the ENGINE */
static const char *engine_ibmca_id = "ibmca";
static const char *engine_ibmca_name = "Ibmca hardware engine support";


inline static int set_RSA_prop(ENGINE *e)
{
	static int rsa_enabled = 0;
#ifndef OPENSSL_NO_RSA
	const RSA_METHOD *meth1;
#ifndef OLDER_OPENSSL
	ibmca_rsa = RSA_meth_new("Ibmca RSA method", 0);
#endif
#endif
#ifndef OPENSSL_NO_DSA
	const DSA_METHOD *meth2;
#ifndef OLDER_OPENSSL
	ibmca_dsa = DSA_meth_new("Ibmca DSA method", 0);
#endif
#endif
#ifndef OPENSSL_NO_DH
	const DH_METHOD *meth3;
#ifndef OLDER_OPENSSL
	ibmca_dh = DH_meth_new("Ibmca DH method", 0);
#endif
#endif

	if(rsa_enabled){
		return 1;
	}
        if(
#ifndef OPENSSL_NO_RSA
#ifdef OLDER_OPENSSL
	   !ENGINE_set_RSA(e, &ibmca_rsa) ||
#else
	   !ENGINE_set_RSA(e, ibmca_rsa) ||
#endif
#endif
#ifndef OPENSSL_NO_DSA
#ifdef OLDER_OPENSSL
	   !ENGINE_set_DSA(e, &ibmca_dsa) ||
#else
	   !ENGINE_set_DSA(e, ibmca_dsa) ||
#endif
#endif
#ifndef OPENSSL_NO_DH
#ifdef OLDER_OPENSSL
	   !ENGINE_set_DH(e, &ibmca_dh)
#else
	   !ENGINE_set_DH(e, ibmca_dh)
#endif
	  )
#endif
		return 0;
#ifndef OPENSSL_NO_RSA
        /* We know that the "PKCS1_SSLeay()" functions hook properly
         * to the ibmca-specific mod_exp and mod_exp_crt so we use
         * those functions. NB: We don't use ENGINE_openssl() or
         * anything "more generic" because something like the RSAref
         * code may not hook properly, and if you own one of these here
         * cards then you have the right to do RSA operations on it
         * anyway! */
#ifdef OLDER_OPENSSL
        meth1 = RSA_PKCS1_SSLeay();
        ibmca_rsa.rsa_pub_enc = meth1->rsa_pub_enc;
        ibmca_rsa.rsa_pub_dec = meth1->rsa_pub_dec;
        ibmca_rsa.rsa_priv_enc = meth1->rsa_priv_enc;
        ibmca_rsa.rsa_priv_dec = meth1->rsa_priv_dec;
#else
	meth1 = RSA_PKCS1_OpenSSL();
	if (   !RSA_meth_set_pub_enc(ibmca_rsa, RSA_meth_get_pub_enc(meth1))
	    || !RSA_meth_set_pub_dec(ibmca_rsa, RSA_meth_get_pub_dec(meth1))
	    || !RSA_meth_set_priv_enc(ibmca_rsa, RSA_meth_get_priv_enc(meth1))
	    || !RSA_meth_set_priv_dec(ibmca_rsa, RSA_meth_get_priv_dec(meth1))
	    || !RSA_meth_set_mod_exp(ibmca_rsa, ibmca_rsa_mod_exp)
	    || !RSA_meth_set_bn_mod_exp(ibmca_rsa, ibmca_mod_exp_mont)
	    || !RSA_meth_set_init(ibmca_rsa, ibmca_rsa_init) )
		return 0;
#endif
#endif
#ifndef OPENSSL_NO_DSA
	meth2 = DSA_OpenSSL();
#ifdef OLDER_OPENSSL
        ibmca_dsa.dsa_do_sign = meth2->dsa_do_sign;
        ibmca_dsa.dsa_sign_setup = meth2->dsa_sign_setup;
        ibmca_dsa.dsa_do_verify = meth2->dsa_do_verify;
#else
	if (   !DSA_meth_set_sign(ibmca_dsa, DSA_meth_get_sign(meth2))
	    || !DSA_meth_set_verify(ibmca_dsa, DSA_meth_get_verify(meth2))
	    || !DSA_meth_set_mod_exp(ibmca_dsa, ibmca_dsa_mod_exp)
	    || !DSA_meth_set_bn_mod_exp(ibmca_dsa, ibmca_mod_exp_dsa) )
		return 0;
#endif
#endif
#ifndef OPENSSL_NO_DH
        /* Much the same for Diffie-Hellman */
        meth3 = DH_OpenSSL();
#ifdef OLDER_OPENSSL
        ibmca_dh.generate_key = meth3->generate_key;
        ibmca_dh.compute_key = meth3->compute_key;
#else
	if (   !DH_meth_set_generate_key(ibmca_dh, DH_meth_get_generate_key(meth3))
	    || !DH_meth_set_compute_key(ibmca_dh, DH_meth_get_compute_key(meth3))
	    || !DH_meth_set_bn_mod_exp(ibmca_dh, ibmca_mod_exp_dh) )
		return 0;
#endif
#endif
	rsa_enabled = 1;
	return 1;
}


/*
 * dig_nid_cnt and ciph_nid_cnt count the number of enabled crypt mechanims.
 * dig_nid_cnt and ciph_nid_cnt needs to be pointer, because only set_engine_prop
 * knows about how much digest or cipher will be set per call. To count the number of
 * cipher and digest outside of the function is not feasible
 */
inline static int set_engine_prop(ENGINE *e, int algo_id, int *dig_nid_cnt, int *ciph_nid_cnt)
{
        switch(algo_id) {
                case P_RNG:
                        if(!ENGINE_set_RAND(e, &ibmca_rand))
                                return 0;
                        break;
		/*
		 * RSA will be enabled if one of this is set. OpenSSL does not distinguish
		 * between RSA_ME and RSA_CRT. It is not the task of ibmca to route one ME
		 * call to CRT or vice versa.
		 */
                case RSA_ME:
                case RSA_CRT:
                        if(!set_RSA_prop(e))
                                return 0;
			break;
#ifndef OPENSSL_NO_SHA1
		case SHA1:
			ibmca_digest_lists.nids[*dig_nid_cnt] = NID_sha1;
#ifdef OLDER_OPENSSL
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++]=  &ibmca_sha1;
#else
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++]=  ibmca_sha1();
#endif
			break;
#endif
#ifndef OPENSSL_NO_SHA256
                case SHA256:
                        ibmca_digest_lists.nids[*dig_nid_cnt] = NID_sha256;
#ifdef OLDER_OPENSSL
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++] =  &ibmca_sha256;
#else
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++] =  ibmca_sha256();
#endif
			break;
#endif
#ifndef OPENSSL_NO_SHA512
                case SHA512:
                        ibmca_digest_lists.nids[*dig_nid_cnt] = NID_sha512;
#ifdef OLDER_OPENSSL
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++] =  &ibmca_sha512;
#else
			ibmca_digest_lists.crypto_meths[(*dig_nid_cnt)++] =  ibmca_sha512();
#endif
			break;
#endif
                case DES_ECB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt]  = NID_des_ecb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_des_ecb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_des_ecb();
#endif
			break;
		case DES_CBC:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_cbc;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_des_cbc;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_des_cbc();
#endif
			break;
		case DES_OFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_ofb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_des_ofb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_des_ofb();
#endif
			break;
		case DES_CFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_cfb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_des_cfb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_des_cfb();
#endif
			break;
		case DES3_ECB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_ede3_ecb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_tdes_ecb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_tdes_ecb();
#endif
			break;
		case DES3_CBC:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_ede3_cbc;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_tdes_cbc;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_tdes_cbc();
#endif
			break;
		case DES3_OFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_ede3_ofb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_tdes_ofb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_tdes_ofb();
#endif
			break;
		case DES3_CFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_des_ede3_cfb;
#ifdef OLDER_OPENSSL
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = &ibmca_tdes_cfb;
#else
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_tdes_cfb();
#endif
			break;
		case AES_ECB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_128_ecb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_128_ecb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_192_ecb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_192_ecb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_256_ecb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_256_ecb();
			break;
		case AES_CBC:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_128_cbc;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_128_cbc();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_192_cbc;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_192_cbc();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_256_cbc;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_256_cbc();
			break;
		case AES_OFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_128_ofb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_128_ofb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_192_ofb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_192_ofb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_256_ofb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_256_ofb();
			break;
		case AES_CFB:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_128_cfb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_128_cfb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_192_cfb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_192_cfb();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_256_cfb;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_256_cfb();
			break;
#ifndef OPENSSL_NO_AES_GCM
		case AES_GCM_KMA:
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_128_gcm;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_128_gcm();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_192_gcm;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_192_gcm();
			ibmca_cipher_lists.nids[*ciph_nid_cnt] = NID_aes_256_gcm;
			ibmca_cipher_lists.crypto_meths[(*ciph_nid_cnt)++] = ibmca_aes_256_gcm();
			break;
#endif
		default:
			break;	/* do nothing */
	}

	size_cipher_list = *ciph_nid_cnt;
	size_digest_list = *dig_nid_cnt;
	return 1;
}

int is_crypto_card_loaded()
{
	DIR* sysDir;
	FILE *file;
	char dev[PATH_MAX] = AP_PATH;
	struct dirent *direntp;
	char *type = NULL;
	size_t size;
	char c;

	if ((sysDir = opendir(dev)) == NULL )
		return 0;

	while((direntp = readdir(sysDir)) != NULL){
		if(strstr(direntp->d_name, "card") != 0){
			snprintf(dev, PATH_MAX, "%s/%s/type", AP_PATH,
				direntp->d_name);

			if ((file = fopen(dev, "r")) == NULL){
	                        closedir(sysDir);
                                return 0;
			}

			if (getline(&type, &size, file) == -1){
				fclose(file);
				closedir(sysDir);
				return 0;
			}

			/* ignore \n
			 * looking for CEX??A and CEX??C
			 * Skip type CEX??P cards
			 */
			if (type[strlen(type)-2] == 'P'){
				free(type);
				type = NULL;
				fclose(file);
				continue;
			}
			free(type);
			type = NULL;
			fclose(file);

			snprintf(dev, PATH_MAX, "%s/%s/online", AP_PATH,
				direntp->d_name);
			if ((file = fopen(dev, "r")) == NULL){
				closedir(sysDir);
				return 0;
			}
			if((c = fgetc(file)) == '1'){
				fclose(file);
				return 1;
			}
			fclose(file);
		}
	}
	closedir(sysDir);
	return 0;
}


typedef unsigned int (*ica_get_functionlist_t)(libica_func_list_element *, unsigned int *);
ica_get_functionlist_t          p_ica_get_functionlist;

static int set_supported_meths(ENGINE *e)
{
        int i, j;
        unsigned int mech_len;
        libica_func_list_element *pmech_list = NULL;
	int rc = 0;
        int dig_nid_cnt = 0;
        int ciph_nid_cnt = 0;
	int card_loaded;

        if (p_ica_get_functionlist(NULL, &mech_len))
		return 0;

	pmech_list = malloc(sizeof(libica_func_list_element)*mech_len);
	if (pmech_list == NULL)
		return 0;

	if (p_ica_get_functionlist(pmech_list, &mech_len))
		goto out;

	card_loaded = is_crypto_card_loaded();

	for (i = 0; i < mech_len; i++) {
	        for (j = 0; ibmca_crypto_algos[j]; j++){
			/* Disable crypto algorithm if it
			 * is not supported in hardware
			 */
			if (!(pmech_list[i].flags &
			      (ICA_FLAG_SHW | ICA_FLAG_DHW)))
				break;

			/* If no crypto card is available,
			 * disable crypto algos that can
			 * only operate on HW on card
			 */
			if ((pmech_list[i].flags & ICA_FLAG_DHW)
			    && !card_loaded)
				break;

	                if(ibmca_crypto_algos[j]
			   == pmech_list[i].mech_mode_id){
	                        /* Set NID, ibmca struct and the
				 * info for the ENGINE struct
				 */
	                        if(!set_engine_prop(e, ibmca_crypto_algos[j],
	                                            &dig_nid_cnt,
	                                            &ciph_nid_cnt)){
					goto out;
				}
	               }
	       }
	}

        if(dig_nid_cnt > 0) {
                if(!ENGINE_set_digests(e, ibmca_engine_digests))
			goto out;
	}
        if(ciph_nid_cnt > 0) {
                if(!ENGINE_set_ciphers(e, ibmca_engine_ciphers))
			goto out;
	}
	rc = 1;
out:
        free(pmech_list);
	return rc;
}


/* This internal function is used by ENGINE_ibmca() and possibly by the
 * "dynamic" ENGINE support too */
static int bind_helper(ENGINE * e)
{
	if (!ENGINE_set_id(e, engine_ibmca_id) ||
	    !ENGINE_set_name(e, engine_ibmca_name) ||
	    !ENGINE_set_destroy_function(e, ibmca_destroy) ||
	    !ENGINE_set_init_function(e, ibmca_init) ||
	    !ENGINE_set_finish_function(e, ibmca_finish) ||
	    !ENGINE_set_ctrl_function(e, ibmca_ctrl) ||
	    !ENGINE_set_cmd_defns(e, ibmca_cmd_defns))
		return 0;

	/* Ensure the ibmca error handling is set up */
	ERR_load_IBMCA_strings();
	/* initialize the engine implizit */
	ibmca_init(e);
	return 1;
}

static ENGINE *engine_ibmca(void)
{
	ENGINE *ret = ENGINE_new();
	if (!ret)
		return NULL;
	if (!bind_helper(ret)) {
		ENGINE_free(ret);
		return NULL;
	}
	return ret;
}

void ENGINE_load_ibmca(void)
{
	/* Copied from eng_[openssl|dyn].c */
	ENGINE *toadd = engine_ibmca();
	if (!toadd)
		return;
	ENGINE_add(toadd);
	ENGINE_free(toadd);
	ERR_clear_error();
}

/* Destructor (complements the "ENGINE_ibmca()" constructor) */
static int ibmca_destroy(ENGINE * e)
{
	/* Unload the ibmca error strings so any error state including our
	 * functs or reasons won't lead to a segfault (they simply get displayed
	 * without corresponding string data because none will be found). */
#ifndef OLDER_OPENSSL
	ibmca_des_ecb_destroy();
	ibmca_des_cbc_destroy();
	ibmca_des_ofb_destroy();
	ibmca_des_cfb_destroy();
	ibmca_tdes_ecb_destroy();
	ibmca_tdes_cbc_destroy();
	ibmca_tdes_ofb_destroy();
	ibmca_tdes_cfb_destroy();

	ibmca_aes_128_ecb_destroy();
	ibmca_aes_128_cbc_destroy();
	ibmca_aes_128_ofb_destroy();
	ibmca_aes_128_cfb_destroy();
	ibmca_aes_192_ecb_destroy();
	ibmca_aes_192_cbc_destroy();
	ibmca_aes_192_ofb_destroy();
	ibmca_aes_192_cfb_destroy();
	ibmca_aes_256_ecb_destroy();
	ibmca_aes_256_cbc_destroy();
	ibmca_aes_256_ofb_destroy();
	ibmca_aes_256_cfb_destroy();

# ifndef OPENSSL_NO_AES_GCM
	ibmca_aes_128_gcm_destroy();
	ibmca_aes_192_gcm_destroy();
	ibmca_aes_256_gcm_destroy();
# endif

	ibmca_sha1_destroy();
	ibmca_sha256_destroy();
	ibmca_sha512_destroy();
#endif
	ERR_unload_IBMCA_strings();
	return 1;
}

/* This is a process-global DSO handle used for loading and unloading
 * the Ibmca library. NB: This is only set (or unset) during an
 * init() or finish() call (reference counts permitting) and they're
 * operating with global locks, so this should be thread-safe
 * implicitly. */

void *ibmca_dso = NULL;

/* These are the function pointers that are (un)set when the library has
 * successfully (un)loaded. */

typedef unsigned int (*ica_open_adapter_t)(ica_adapter_handle_t *);
typedef unsigned int (*ica_close_adapter_t)(ica_adapter_handle_t);
typedef unsigned int (*ica_rsa_mod_expo_t)(ica_adapter_handle_t, unsigned char *,
			ica_rsa_key_mod_expo_t *, unsigned char *);
typedef unsigned int (*ica_random_number_generate_t)(unsigned int, unsigned char *);
typedef unsigned int (*ica_rsa_crt_t)(ica_adapter_handle_t, unsigned char *,
			ica_rsa_key_crt_t *, unsigned char *);
typedef unsigned int (*ica_sha1_t)(unsigned int, unsigned int, unsigned char *, sha_context_t *,
			unsigned char *);
typedef unsigned int (*ica_des_encrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_des_vector_t *, ica_des_key_single_t *, unsigned char *);
typedef unsigned int (*ica_des_decrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_des_vector_t *, ica_des_key_single_t *, unsigned char *);
typedef unsigned int (*ica_3des_encrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_des_vector_t *, ica_des_key_triple_t *, unsigned char *);
typedef unsigned int (*ica_3des_decrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_des_vector_t *, ica_des_key_triple_t *, unsigned char *);
typedef unsigned int (*ica_aes_encrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_aes_vector_t *, unsigned int, unsigned char *, unsigned char *);
typedef unsigned int (*ica_aes_decrypt_t)(unsigned int, unsigned int, unsigned char *,
			ica_aes_vector_t *, unsigned int, unsigned char *, unsigned char *);
typedef unsigned int (*ica_sha256_t)(unsigned int, unsigned int, unsigned char *,
			sha256_context_t *, unsigned char *);
typedef unsigned int (*ica_des_ofb_t)(const unsigned char *in_data, unsigned char *out_data,
			 unsigned long data_length, const unsigned char *key,
			 unsigned char *iv, unsigned int direction);
typedef unsigned int (*ica_sha512_t)(unsigned int, unsigned int,
				     unsigned char *, sha512_context_t *,
				     unsigned char *);
typedef unsigned int (*ica_des_cfb_t)(const unsigned char *in_data, unsigned char *out_data,
			 unsigned long data_length, const unsigned char *key,
			 unsigned char *iv, unsigned int lcfb,
			 unsigned int direction);
typedef unsigned int (*ica_3des_cfb_t)(const unsigned char *, unsigned char *,
			unsigned long, const unsigned char *, unsigned char *,
			unsigned int, unsigned int);
typedef unsigned int (*ica_3des_ofb_t)(const unsigned char *in_data, unsigned char *out_data,
			  unsigned long data_length, const unsigned char *key,
			  unsigned char *iv, unsigned int direction);
typedef unsigned int (*ica_aes_ofb_t)(const unsigned char *in_data, unsigned char *out_data,
			 unsigned long data_length, const unsigned char *key,
			 unsigned int key_length, unsigned char *iv,
			 unsigned int direction);
typedef unsigned int (*ica_aes_cfb_t)(const unsigned char *in_data, unsigned char *out_data,
			 unsigned long data_length, const unsigned char *key,
			 unsigned int key_length, unsigned char *iv, unsigned int lcfb,
			 unsigned int direction);

typedef unsigned int (*ica_aes_gcm_initialize_t)(const unsigned char *iv,
						 unsigned int iv_length,
						 unsigned char *key,
						 unsigned int key_length,
						 unsigned char *icb,
						 unsigned char *ucb,
						 unsigned char *subkey,
						 unsigned int direction);
typedef unsigned int (*ica_aes_gcm_intermediate_t)(unsigned char *plaintext,
						   unsigned long
						       plaintext_length,
						   unsigned char *ciphertext,
						   unsigned char *ucb,
						   unsigned char *aad,
						   unsigned long aad_length,
						   unsigned char *tag,
						   unsigned int tag_length,
						   unsigned char *key,
						   unsigned int key_length,
						   unsigned char *subkey,
						   unsigned int direction);
typedef unsigned int (*ica_aes_gcm_last_t)(unsigned char *icb,
					   unsigned long aad_length,
					   unsigned long ciph_length,
					   unsigned char *tag,
					   unsigned char *final_tag,
					   unsigned int final_tag_length,
					   unsigned char *key,
					   unsigned int key_length,
					   unsigned char *subkey,
					   unsigned int direction);

/* entry points into libica, filled out at DSO load time */
ica_open_adapter_t		p_ica_open_adapter;
ica_close_adapter_t		p_ica_close_adapter;
ica_rsa_mod_expo_t		p_ica_rsa_mod_expo;
ica_random_number_generate_t	p_ica_random_number_generate;
ica_rsa_crt_t			p_ica_rsa_crt;
ica_sha1_t			p_ica_sha1;
ica_des_encrypt_t		p_ica_des_encrypt;
ica_des_decrypt_t		p_ica_des_decrypt;
ica_3des_encrypt_t		p_ica_3des_encrypt;
ica_3des_decrypt_t		p_ica_3des_decrypt;
ica_aes_encrypt_t		p_ica_aes_encrypt;
ica_aes_decrypt_t		p_ica_aes_decrypt;
ica_sha256_t			p_ica_sha256;
ica_sha512_t			p_ica_sha512;
ica_des_ofb_t			p_ica_des_ofb;
ica_des_cfb_t			p_ica_des_cfb;
ica_3des_cfb_t			p_ica_3des_cfb;
ica_3des_ofb_t			p_ica_3des_ofb;
ica_aes_ofb_t			p_ica_aes_ofb;
ica_aes_cfb_t			p_ica_aes_cfb;
#ifndef OPENSSL_NO_AES_GCM
ica_aes_gcm_initialize_t	p_ica_aes_gcm_initialize;
ica_aes_gcm_intermediate_t	p_ica_aes_gcm_intermediate;
ica_aes_gcm_last_t		p_ica_aes_gcm_last;
#endif

/* utility function to obtain a context */
static int get_context(ica_adapter_handle_t * p_handle)
{
	unsigned int status = 0;

	status = p_ica_open_adapter(p_handle);
	if (status != 0)
		return 0;
	return 1;
}

/* similarly to release one. */
static void release_context(ica_adapter_handle_t i_handle)
{
	p_ica_close_adapter(i_handle);
}

/* initialisation functions. */
#define BIND(dso, sym)	(p_##sym = (sym##_t)dlsym(dso, #sym))
static int ibmca_init(ENGINE * e)
{
	static int init = 0;

	if (init)	/* Engine already loaded. */
		return 1;

	/* Attempt to load libica.so. Needs to be
	 * changed unfortunately because the Ibmca drivers don't have
	 * standard library names that can be platform-translated well. */
	/* TODO: Work out how to actually map to the names the Ibmca
	 * drivers really use - for now a symbollic link needs to be
	 * created on the host system from libica.so to ica.so on
	 * unix variants. */

	/* WJH XXX check name translation */

	ibmca_dso = dlopen(LIBICA_SHARED_LIB, RTLD_NOW);
	if (ibmca_dso == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_INIT, IBMCA_R_DSO_FAILURE);
		goto err;
	}

	if (!BIND(ibmca_dso, ica_open_adapter)
	    || !BIND(ibmca_dso, ica_close_adapter)
	    || !BIND(ibmca_dso, ica_rsa_mod_expo)
	    || !BIND(ibmca_dso, ica_random_number_generate)
	    || !BIND(ibmca_dso, ica_rsa_crt)
	    || !BIND(ibmca_dso, ica_sha1)
	    || !BIND(ibmca_dso, ica_des_encrypt)
	    || !BIND(ibmca_dso, ica_des_decrypt)
	    || !BIND(ibmca_dso, ica_3des_encrypt)
	    || !BIND(ibmca_dso, ica_3des_decrypt)
	    || !BIND(ibmca_dso, ica_aes_encrypt)
	    || !BIND(ibmca_dso, ica_aes_decrypt)
	    || !BIND(ibmca_dso, ica_sha256)
	    || !BIND(ibmca_dso, ica_sha512)
	    || !BIND(ibmca_dso, ica_aes_ofb)
	    || !BIND(ibmca_dso, ica_des_ofb)
	    || !BIND(ibmca_dso, ica_3des_ofb)
	    || !BIND(ibmca_dso, ica_aes_cfb)
	    || !BIND(ibmca_dso, ica_des_cfb)
	    || !BIND(ibmca_dso, ica_get_functionlist)
	    || !BIND(ibmca_dso, ica_3des_cfb)
#ifndef OPENSSL_NO_AES_GCM
	    || !BIND(ibmca_dso, ica_aes_gcm_initialize)
	    || !BIND(ibmca_dso, ica_aes_gcm_intermediate)
	    || !BIND(ibmca_dso, ica_aes_gcm_last)
#endif
	   ) {
		IBMCAerr(IBMCA_F_IBMCA_INIT, IBMCA_R_DSO_FAILURE);
		goto err;
	}

        if(!set_supported_meths(e))
                goto err;

	if (!get_context(&ibmca_handle)) {
		IBMCAerr(IBMCA_F_IBMCA_INIT, IBMCA_R_UNIT_FAILURE);
		goto err;
	}

	init = 1;
	return 1;
err:
	if (ibmca_dso) {
		dlclose(ibmca_dso);
		ibmca_dso = NULL;
	}
	p_ica_open_adapter = NULL;
	p_ica_close_adapter = NULL;
	p_ica_rsa_mod_expo = NULL;
	p_ica_random_number_generate = NULL;
	p_ica_rsa_crt = NULL;
	p_ica_sha1 = NULL;
	p_ica_des_encrypt = NULL;
	p_ica_des_decrypt = NULL;
	p_ica_3des_encrypt = NULL;
	p_ica_3des_decrypt = NULL;
	p_ica_aes_encrypt = NULL;
	p_ica_aes_decrypt = NULL;
	p_ica_sha256 = NULL;
	p_ica_sha512 = NULL;
	p_ica_aes_ofb = NULL;
	p_ica_des_ofb = NULL;
	p_ica_3des_ofb = NULL;
	p_ica_aes_cfb = NULL;
	p_ica_des_cfb = NULL;
	p_ica_3des_cfb = NULL;
#ifndef OPENSSL_NO_AES_GCM
	p_ica_aes_gcm_initialize = NULL;
	p_ica_aes_gcm_intermediate = NULL;
	p_ica_aes_gcm_last = NULL;
#endif
	return 0;
}

static int ibmca_finish(ENGINE * e)
{
	if (ibmca_dso == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_FINISH, IBMCA_R_NOT_LOADED);
		return 0;
	}
	release_context(ibmca_handle);
	if (!dlclose(ibmca_dso)) {
		IBMCAerr(IBMCA_F_IBMCA_FINISH, IBMCA_R_DSO_FAILURE);
		return 0;
	}
	ibmca_dso = NULL;

	return 1;
}

static int ibmca_ctrl(ENGINE * e, int cmd, long i, void *p, void (*f) ())
{
	int initialised = ((ibmca_dso == NULL) ? 0 : 1);
	switch (cmd) {
	case IBMCA_CMD_SO_PATH:
		if (p == NULL) {
			IBMCAerr(IBMCA_F_IBMCA_CTRL,
				 ERR_R_PASSED_NULL_PARAMETER);
			return 0;
		}
		if (initialised) {
			IBMCAerr(IBMCA_F_IBMCA_CTRL,
				 IBMCA_R_ALREADY_LOADED);
			return 0;
		}
		LIBICA_NAME = (const char *) p;
		return 1;
	default:
		break;
	}
	IBMCAerr(IBMCA_F_IBMCA_CTRL, IBMCA_R_CTRL_COMMAND_NOT_IMPLEMENTED);
	return 0;
}

/*
 * ENGINE calls this to find out how to deal with
 * a particular NID in the ENGINE.
 */
static int ibmca_engine_ciphers(ENGINE * e, const EVP_CIPHER ** cipher,
				const int **nids, int nid)
{
        int i;
	if (!cipher)
		return (ibmca_usable_ciphers(nids));

        *cipher = NULL;
        for(i = 0; i < size_cipher_list;i++)
                if(nid == ibmca_cipher_lists.nids[i]){
                        *cipher = (EVP_CIPHER*) ibmca_cipher_lists.crypto_meths [i];
                        break;
		}
        /* Check: how can *cipher be NULL? */
	return (*cipher != NULL);
}

static int ibmca_usable_ciphers(const int **nids)
{

        if(nids)
	        *nids = ibmca_cipher_lists.nids;
	return size_cipher_list;
}

static int ibmca_init_key(EVP_CIPHER_CTX * ctx, const unsigned char *key,
			  const unsigned char *iv, int enc)
{
#ifdef OLDER_OPENSSL
	ICA_DES_CTX *pCtx = ctx->cipher_data;

	memcpy(pCtx->key, key, ctx->cipher->key_len);
#else
	ICA_DES_CTX *pCtx = (ICA_DES_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);

	memcpy(pCtx->key, key, EVP_CIPHER_CTX_key_length(ctx));
#endif

	return 1;
}				// end ibmca_init_key

static int ibmca_des_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
			    const unsigned char *in, size_t inlen)
{
	int mode = 0;
	int rv;
	unsigned int len;
#ifdef OLDER_OPENSSL
	ICA_DES_CTX *pCtx = ctx->cipher_data;
#else
	ICA_DES_CTX *pCtx = (ICA_DES_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);
#endif

	ica_des_vector_t pre_iv;

	if (inlen > UINT32_MAX) {
		IBMCAerr(IBMCA_F_IBMCA_DES_CIPHER, IBMCA_R_OUTLEN_TO_LARGE);
		return 0;
	}
	len = inlen;

	if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_ECB_MODE) {
		mode = MODE_ECB;
	} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CBC_MODE) {
		mode = MODE_CBC;
	} else if ((EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_CFB_MODE) &&
		   (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE)) {
		IBMCAerr(IBMCA_F_IBMCA_DES_CIPHER,
				IBMCA_R_CIPHER_MODE_NOT_SUPPORTED);
		return 0;
	}

#ifdef OLDER_OPENSSL
	if (ctx->encrypt) {
#else
	if (EVP_CIPHER_CTX_encrypting(ctx)) {
#endif
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			rv = p_ica_des_cfb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					   ctx->iv,
#else
					   EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					   8, ICA_ENCRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_des_ofb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					   ctx->iv,
#else
					   EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					   ICA_ENCRYPT);
		} else {
			rv = p_ica_des_encrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_des_vector_t *) ctx->iv,
#else
				(ica_des_vector_t *) EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				(ica_des_key_single_t *) pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_DES_CIPHER,
					IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			       out + len - EVP_CIPHER_CTX_iv_length(ctx),
			       EVP_CIPHER_CTX_iv_length(ctx));
		}
	} else {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			/* Protect against decrypt in place */
			/* FIXME: Shouldn't we use EVP_CIPHER_CTX_iv_length() instead? */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_des_cfb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					   ctx->iv,
#else
					   EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					   8, ICA_DECRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_des_ofb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					   ctx->iv,
#else
					   EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					   ICA_DECRYPT);
		} else {
			/* Protect against decrypt in place */
			/* FIXME: Shouldn't we use EVP_CIPHER_CTX_iv_length() instead? */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_des_decrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_des_vector_t *) ctx->iv,
#else
				(ica_des_vector_t *) EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				(ica_des_key_single_t *) pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_DES_CIPHER,
					IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				pre_iv, EVP_CIPHER_CTX_iv_length(ctx));
		}
	}

	return 1;
}				// end ibmca_des_cipher

static int ibmca_tdes_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
			     const unsigned char *in, size_t inlen)
{
	int mode = 0;
	int rv;
	unsigned int len;
#ifdef OLDER_OPENSSL
	ICA_DES_CTX *pCtx = ctx->cipher_data;
#else
	ICA_DES_CTX *pCtx = (ICA_DES_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);
#endif
	ica_des_vector_t pre_iv;

	if (inlen > UINT32_MAX) {
		IBMCAerr(IBMCA_F_IBMCA_TDES_CIPHER, IBMCA_R_OUTLEN_TO_LARGE);
		return 0;
	}
	len = inlen;

	if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_ECB_MODE) {
		mode = MODE_ECB;
	} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CBC_MODE) {
		mode = MODE_CBC;
	} else if ((EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_CFB_MODE) &&
		   (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE)) {
		IBMCAerr(IBMCA_F_IBMCA_TDES_CIPHER,
				IBMCA_R_CIPHER_MODE_NOT_SUPPORTED);
		return 0;
	}

#ifdef OLDER_OPENSSL
	if (ctx->encrypt) {
#else
	if (EVP_CIPHER_CTX_encrypting(ctx)) {
#endif
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			rv = p_ica_3des_cfb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					8, ICA_ENCRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_3des_ofb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_ENCRYPT);
		} else {
			rv = p_ica_3des_encrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_des_vector_t *) ctx->iv,
#else
				(ica_des_vector_t *) EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				(ica_des_key_triple_t *) pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_TDES_CIPHER,
					IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			       out + len - EVP_CIPHER_CTX_iv_length(ctx),
			       EVP_CIPHER_CTX_iv_length(ctx));
		}
	} else {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			/* Protect against decrypt in place */
			/* FIXME: Again, check if EVP_CIPHER_CTX_iv_length() should be used */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_3des_cfb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					8, ICA_DECRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_3des_ofb(in, out, len, pCtx->key,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_DECRYPT);
		} else {
			/* Protect against decrypt in place */
			/* FIXME: Again, check if EVP_CIPHER_CTX_iv_length() should be used */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_3des_decrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_des_vector_t *) ctx->iv,
#else
				(ica_des_vector_t *) EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				(ica_des_key_triple_t *) pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_TDES_CIPHER,
					IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				pre_iv, EVP_CIPHER_CTX_iv_length(ctx));
		}
	}

	return 1;
}				// end ibmca_tdes_cipher

/* FIXME: a lot of common code between ica_aes_[128|192|256]_cipher() fncs */
static int ibmca_aes_128_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
				const unsigned char *in, size_t inlen)
{
	int mode = 0;
	int rv;
	unsigned int len;
#ifdef OLDER_OPENSSL
	ICA_AES_128_CTX *pCtx = ctx->cipher_data;
#else
	ICA_AES_128_CTX *pCtx = (ICA_AES_128_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);
#endif
	ica_aes_vector_t pre_iv;

	if (inlen > UINT32_MAX) {
		IBMCAerr(IBMCA_F_IBMCA_AES_128_CIPHER, IBMCA_R_OUTLEN_TO_LARGE);
		return 0;
	}
	len = inlen;

	if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_ECB_MODE) {
		mode = MODE_ECB;
	} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CBC_MODE) {
		mode = MODE_CBC;
	} else if ((EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_CFB_MODE) &&
		   (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE)) {
		IBMCAerr(IBMCA_F_IBMCA_AES_128_CIPHER,
			 IBMCA_R_CIPHER_MODE_NOT_SUPPORTED);
		return 0;
	}

#ifdef OLDER_OPENSSL
	if (ctx->encrypt) {
#else
	if (EVP_CIPHER_CTX_encrypting(ctx)) {
#endif
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN128,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_ENCRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN128,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_ENCRYPT);
		} else {
			rv = p_ica_aes_encrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN128,
				(unsigned char *)pCtx->key,
				out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_128_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			       out + len - EVP_CIPHER_CTX_iv_length(ctx),
			       EVP_CIPHER_CTX_iv_length(ctx));
		}
	} else {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			/* Protect against decrypt in place */
			/* FIXME: Again, check if EVP_CIPHER_CTX_iv_length() should be used */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN128,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_DECRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN128,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_DECRYPT);
		} else {
			/* Protect against decrypt in place */
			/* FIXME: Again, check if EVP_CIPHER_CTX_iv_length() should be used */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_decrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN128,
				(unsigned char *)pCtx->key,
				out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_128_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				pre_iv, EVP_CIPHER_CTX_iv_length(ctx));
		}
	}

	return 1;
}

static int ibmca_aes_192_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
				const unsigned char *in, size_t inlen)
{
	int mode = 0;
	int rv;

	unsigned int len;
#ifdef OLDER_OPENSSL
	ICA_AES_192_CTX *pCtx = ctx->cipher_data;
#else
	ICA_AES_192_CTX *pCtx = (ICA_AES_192_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);
#endif
	ica_aes_vector_t pre_iv;

	if (inlen > UINT32_MAX) {
		IBMCAerr(IBMCA_F_IBMCA_AES_192_CIPHER, IBMCA_R_OUTLEN_TO_LARGE);
		return 0;
	}
	len = inlen;

	if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_ECB_MODE) {
		mode = MODE_ECB;
	} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CBC_MODE) {
		mode = MODE_CBC;
	} else if ((EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_CFB_MODE) &&
		   (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE)) {
		IBMCAerr(IBMCA_F_IBMCA_AES_192_CIPHER,
			 IBMCA_R_CIPHER_MODE_NOT_SUPPORTED);
		return 0;
	}

#ifdef OLDER_OPENSSL
	if (ctx->encrypt) {
#else
	if (EVP_CIPHER_CTX_encrypting(ctx)) {
#endif
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN192,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_ENCRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN192,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_ENCRYPT);
		} else {
			rv = p_ica_aes_encrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN192,
				(unsigned char *)pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_192_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			       out + len - EVP_CIPHER_CTX_iv_length(ctx),
			       EVP_CIPHER_CTX_iv_length(ctx));
		}
	} else {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			/* Protect against decrypt in place */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN192,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_DECRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN192,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_DECRYPT);
		} else {
			/* Protect against decrypt in place */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_decrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN192,
				(unsigned char *)pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_192_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			pre_iv, EVP_CIPHER_CTX_iv_length(ctx));
		}
	}

	return 1;
}

static int ibmca_aes_256_cipher(EVP_CIPHER_CTX * ctx, unsigned char *out,
				const unsigned char *in, size_t inlen)
{
	int mode = 0;
	int rv;
	unsigned int len;
#ifdef OLDER_OPENSSL
	ICA_AES_256_CTX *pCtx = ctx->cipher_data;
#else
	ICA_AES_256_CTX *pCtx = (ICA_AES_256_CTX *) EVP_CIPHER_CTX_get_cipher_data(ctx);
#endif
	ica_aes_vector_t pre_iv;

	if (inlen > UINT32_MAX) {
		IBMCAerr(IBMCA_F_IBMCA_AES_256_CIPHER, IBMCA_R_OUTLEN_TO_LARGE);
		return 0;
	}
	len = inlen;

	if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_ECB_MODE) {
		mode = MODE_ECB;
	} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CBC_MODE) {
		mode = MODE_CBC;
	} else if ((EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_CFB_MODE) &&
		   (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE)) {
		IBMCAerr(IBMCA_F_IBMCA_AES_256_CIPHER,
			 IBMCA_R_CIPHER_MODE_NOT_SUPPORTED);
		return 0;
	}

#ifdef OLDER_OPENSSL
	if (ctx->encrypt) {
#else
	if (EVP_CIPHER_CTX_encrypting(ctx)) {
#endif
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN256,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_ENCRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN256,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_ENCRYPT);
		} else {
			rv = p_ica_aes_encrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN256,
				(unsigned char *)pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_256_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
			       out + len - EVP_CIPHER_CTX_iv_length(ctx),
			       EVP_CIPHER_CTX_iv_length(ctx));
		}
	} else {
		if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE) {
			/* Protect against decrypt in place */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_cfb(in, out, len, pCtx->key,
					AES_KEY_LEN256,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					AES_BLOCK_SIZE, ICA_DECRYPT);
		} else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE) {
			rv = p_ica_aes_ofb(in, out, len, pCtx->key,
					AES_KEY_LEN256,
#ifdef OLDER_OPENSSL
					ctx->iv,
#else
					EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
					ICA_DECRYPT);
		} else {
			/* Protect against decrypt in place */
			memcpy(pre_iv, in + len - sizeof(pre_iv), sizeof(pre_iv));

			rv = p_ica_aes_decrypt(mode, len, (unsigned char *)in,
#ifdef OLDER_OPENSSL
				(ica_aes_vector_t *)ctx->iv,
#else
				(ica_aes_vector_t *)EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				AES_KEY_LEN256,
				(unsigned char *)pCtx->key, out);
		}

		if (rv) {
			IBMCAerr(IBMCA_F_IBMCA_AES_256_CIPHER,
				 IBMCA_R_REQUEST_FAILED);
			return 0;
		} else if (EVP_CIPHER_CTX_mode(ctx) != EVP_CIPH_OFB_MODE) {
#ifdef OLDER_OPENSSL
			memcpy(ctx->iv,
#else
			memcpy(EVP_CIPHER_CTX_iv_noconst(ctx),
#endif
				pre_iv, EVP_CIPHER_CTX_iv_length(ctx));
		}
	}

	return 1;
}

static int ibmca_cipher_cleanup(EVP_CIPHER_CTX * ctx)
{
	return 1;
}

#ifndef OPENSSL_NO_AES_GCM
static int ibmca_gcm_aad(ICA_AES_GCM_CTX *ctx, const unsigned char *aad,
			 size_t len, int enc, int keylen)
{
	uint64_t alen = ctx->aadlen;

	if (ctx->ptlen)
		return -2;

	alen += len;
	if (alen > (1ULL << 61) || (sizeof(len) == 8 && alen < len))
		return -1;

	ctx->aadlen = alen;

	/* ctx->taglen is not set at this time... and is not needed. The
	 * function only checks, if it's a valid gcm tag length. So we chose
	 * 16. */
	return !(p_ica_aes_gcm_intermediate(NULL, 0, NULL, ctx->ucb,
					    (unsigned char *)aad, len,
					    ctx->tag, 16, ctx->key, keylen,
					    ctx->subkey, enc));
}

static int ibmca_aes_gcm(ICA_AES_GCM_CTX *ctx, const unsigned char *in,
                         unsigned char *out, size_t len, int enc, int keylen)
{
	uint64_t mlen = ctx->ptlen;
	unsigned char *pt, *ct;
	int rv;

	mlen += len;
	if (mlen > ((1ULL << 36) - 32) || (sizeof(len) == 8 && mlen < len))
		return -1;

	ctx->ptlen = mlen;

	if (enc) {
		pt = (unsigned char *)in;
		ct = out;
	} else {
		ct = (unsigned char *)in;
		pt = out;
	}

	/* ctx->taglen is not set at this time... and is not needed. The
	 * function only checks, if it's a valid gcm tag length. So we chose
	 * 16. */
	rv = !(p_ica_aes_gcm_intermediate(pt, len, ct, ctx->ucb, NULL, 0,
					  ctx->tag, 16, ctx->key, keylen,
					  ctx->subkey, enc));
	return rv;
}

static int ibmca_aes_gcm_init_key(EVP_CIPHER_CTX *ctx,
                                  const unsigned char *key,
                                  const unsigned char *iv, int enc)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(ctx);
	const int gkeylen = EVP_CIPHER_CTX_key_length(ctx);

	if (!iv && !key)
	    return 1;

	if (key) {
		memcpy(gctx->key, key, gkeylen);

		if (iv == NULL && gctx->iv_set)
			iv = gctx->iv;

		if (iv) {
			memset(gctx->icb, 0, sizeof(gctx->icb));
			memset(gctx->tag, 0, sizeof(gctx->tag));
			gctx->aadlen = 0;
			gctx->ptlen = 0;
			if (p_ica_aes_gcm_initialize(iv, gctx->ivlen,
						     gctx->key, gkeylen,
						     gctx->icb, gctx->ucb,
						     gctx->subkey, enc))
				return 0;

			gctx->iv_set = 1;
		}
		gctx->key_set = 1;
	} else {
		if (gctx->key_set) {
			memset(gctx->icb, 0, sizeof(gctx->icb));
			memset(gctx->tag, 0, sizeof(gctx->tag));
			gctx->aadlen = 0;
			gctx->ptlen = 0;
			if (p_ica_aes_gcm_initialize(iv, gctx->ivlen,
						     gctx->key, gkeylen,
						     gctx->icb, gctx->ucb,
						     gctx->subkey, enc))
				return 0;
		} else {
			memcpy(gctx->iv, iv, gctx->ivlen);
		}
		gctx->iv_set = 1;
		gctx->iv_gen = 0;
	}
	return 1;
}

static int ibmca_aes_gcm_setiv(EVP_CIPHER_CTX *c)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(c);
	const int gkeylen = EVP_CIPHER_CTX_key_length(c);
	int enc = EVP_CIPHER_CTX_encrypting(c);

	if (gctx->key == NULL || !gctx->key_set)
		return 0;

	memset(gctx->icb, 0, sizeof(gctx->icb));
	memset(gctx->tag, 0, sizeof(gctx->tag));
	gctx->aadlen = 0;
	gctx->ptlen = 0;
	return !(p_ica_aes_gcm_initialize(gctx->iv, gctx->ivlen, gctx->key,
					  gkeylen, gctx->icb, gctx->ucb,
					  gctx->subkey, enc));
}

static int ibmca_aes_gcm_ctrl(EVP_CIPHER_CTX *c, int type, int arg,
			      void *ptr)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(c);
	unsigned char *iv_noconst = EVP_CIPHER_CTX_iv_noconst(c);
	unsigned char *buf_noconst = EVP_CIPHER_CTX_buf_noconst(c);
	int enc = EVP_CIPHER_CTX_encrypting(c);
	EVP_CIPHER_CTX *out;
	ICA_AES_GCM_CTX *gctx_out;
	unsigned char *iv_noconst_out;
	unsigned int len;

	switch (type) {
	case EVP_CTRL_INIT:
		gctx->key_set = 0;
		gctx->iv_set = 0;
		gctx->ivlen = EVP_CIPHER_CTX_iv_length(c);
		gctx->iv = iv_noconst;
		gctx->taglen = -1;
		gctx->iv_gen = 0;
		gctx->tls_aadlen = -1;
		return 1;

	case EVP_CTRL_GCM_SET_IVLEN:
		if (arg <= 0)
			return 0;
		if ((arg > EVP_MAX_IV_LENGTH) && (arg > gctx->ivlen)) {
			if (gctx->iv != iv_noconst)
				OPENSSL_free(gctx->iv);
			gctx->iv = OPENSSL_malloc(arg);
			if (gctx->iv == NULL)
				return 0;
		}
		gctx->ivlen = arg;
		return 1;

	case EVP_CTRL_GCM_SET_TAG:
		if (arg <= 0 || arg > 16 || enc)
			return 0;
		memcpy(buf_noconst, ptr, arg);
		gctx->taglen = arg;
		return 1;

	case EVP_CTRL_GCM_GET_TAG:
		if (arg <= 0 || arg > 16 || !enc || gctx->taglen < 0)
			return 0;
		memcpy(ptr, buf_noconst, arg);
		return 1;

	case EVP_CTRL_GCM_SET_IV_FIXED:
		if (arg == -1) {
			memcpy(gctx->iv, ptr, gctx->ivlen);
			gctx->iv_gen = 1;
			return 1;
		}
		if ((arg < 4) || (gctx->ivlen - arg) < 8)
			return 0;
		if (arg)
			memcpy(gctx->iv, ptr, arg);
		if (enc && RAND_bytes(gctx->iv + arg, gctx->ivlen - arg) <= 0)
			return 0;
		gctx->iv_gen = 1;
		return 1;

	case EVP_CTRL_GCM_IV_GEN:
		if (gctx->iv_gen == 0 || gctx->key_set == 0)
			return 0;
		if (!ibmca_aes_gcm_setiv(c))
			return 0;
		if (arg <= 0 || arg > gctx->ivlen)
			arg = gctx->ivlen;
		memcpy(ptr, gctx->iv + gctx->ivlen - arg, arg);
		++*(uint64_t *)(gctx->iv + gctx->ivlen - 8);
		gctx->iv_set = 1;
		return 1;

	case EVP_CTRL_GCM_SET_IV_INV:
		if (gctx->iv_gen == 0 || gctx->key_set == 0 || enc)
			return 0;
		memcpy(gctx->iv + gctx->ivlen - arg, ptr, arg);
		if (!ibmca_aes_gcm_setiv(c))
			return 0;
		gctx->iv_set = 1;
		return 1;

	case EVP_CTRL_AEAD_TLS1_AAD:
		if (arg != EVP_AEAD_TLS1_AAD_LEN)
			return 0;
		memcpy(buf_noconst, ptr, arg);
		gctx->tls_aadlen = arg;
		len = buf_noconst[arg - 2] << 8 | buf_noconst[arg - 1];
		if (len < EVP_GCM_TLS_EXPLICIT_IV_LEN)
			return 0;
		len -= EVP_GCM_TLS_EXPLICIT_IV_LEN;
		if (!enc) {
			if (len < EVP_GCM_TLS_TAG_LEN)
				return 0;
			len -= EVP_GCM_TLS_TAG_LEN;
		}
		buf_noconst[arg - 2] = len >> 8;
		buf_noconst[arg - 1] = len & 0xff;
		return EVP_GCM_TLS_TAG_LEN;

	case EVP_CTRL_COPY: {
		out = ptr;
		gctx_out = (ICA_AES_GCM_CTX *)
			   EVP_CIPHER_CTX_get_cipher_data(out);
		iv_noconst_out = EVP_CIPHER_CTX_iv_noconst(out);
		if (gctx->iv == iv_noconst) {
			gctx_out->iv = iv_noconst_out;
		} else {
			gctx_out->iv = OPENSSL_malloc(gctx->ivlen);
			if (gctx_out->iv == NULL)
				return 0;
			memcpy(gctx_out->iv, gctx->iv, gctx->ivlen);
		}
		return 1;
	}
	default:
		return -1;
    }
}

static int ibmca_gcm_tag(EVP_CIPHER_CTX *ctx, unsigned char *out,
			 const unsigned char *in, int taglen)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(ctx);
	int enc = EVP_CIPHER_CTX_encrypting(ctx);
	const int gkeylen = EVP_CIPHER_CTX_key_length(ctx);

	if (p_ica_aes_gcm_last(gctx->icb, gctx->aadlen, gctx->ptlen,
			       gctx->tag, (unsigned char *)in, taglen,
			       gctx->key, gkeylen, gctx->subkey, enc))
		return 0;

	if (out)
		memcpy(out, gctx->tag, taglen);

	return 1;
}

static int ibmca_aes_gcm_tls_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                    const unsigned char *in, size_t len)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(ctx);
	unsigned char *buf = EVP_CIPHER_CTX_buf_noconst(ctx);
	int enc = EVP_CIPHER_CTX_encrypting(ctx);
	const int keylen = EVP_CIPHER_CTX_key_length(ctx);
	int rv = -1;

	if (out != in
	    || len < (EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN))
		return -1;
	if (EVP_CIPHER_CTX_ctrl(ctx, enc ? EVP_CTRL_GCM_IV_GEN :
	    EVP_CTRL_GCM_SET_IV_INV, EVP_GCM_TLS_EXPLICIT_IV_LEN, out) <= 0)
		goto err;

	if (!ibmca_gcm_aad(gctx, buf, gctx->tls_aadlen, enc, keylen))
		goto err;

	in += EVP_GCM_TLS_EXPLICIT_IV_LEN;
	out += EVP_GCM_TLS_EXPLICIT_IV_LEN;
	len -= EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;

	if (!ibmca_aes_gcm(gctx, in, out, len, enc, keylen))
		goto err;

	if (enc) {
		out += len;
		if (!ibmca_gcm_tag(ctx, out, NULL, EVP_GCM_TLS_TAG_LEN)) {
			goto err;
		}
		rv = len + EVP_GCM_TLS_EXPLICIT_IV_LEN + EVP_GCM_TLS_TAG_LEN;
	} else {
		if (!ibmca_gcm_tag(ctx, buf, in + len, EVP_GCM_TLS_TAG_LEN)) {
			OPENSSL_cleanse(out, len);
			goto err;
		}
		rv = len;
	}
err:
	gctx->iv_set = 0;
	gctx->tls_aadlen = -1;
	return rv;
}

static int ibmca_aes_gcm_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                const unsigned char *in, size_t len)
{
	ICA_AES_GCM_CTX *gctx =
	    (ICA_AES_GCM_CTX *)EVP_CIPHER_CTX_get_cipher_data(ctx);
	unsigned char *buf = EVP_CIPHER_CTX_buf_noconst(ctx);
	int enc = EVP_CIPHER_CTX_encrypting(ctx);
	const int keylen = EVP_CIPHER_CTX_key_length(ctx);

	if (!gctx->key_set)
		return -1;

	if (gctx->tls_aadlen >= 0)
		return ibmca_aes_gcm_tls_cipher(ctx, out, in, len);

	if (!gctx->iv_set)
		return -1;

	if (in) {
		if (out == NULL) {
			if (!ibmca_gcm_aad(gctx, in, len, enc, keylen))
				return -1;
		} else {
			if (!ibmca_aes_gcm(gctx, in, out, len, enc, keylen))
				return -1;
		}
		return len;
	} else {
		if (enc) {
			gctx->taglen = 16;
			if (!ibmca_gcm_tag(ctx, buf, NULL, gctx->taglen))
				return -1;
		} else {
			if (gctx->taglen < 0)
				return -1;
			if (!ibmca_gcm_tag(ctx, NULL, buf, gctx->taglen))
				return -1;
		}
		gctx->iv_set = 0;
		return 0;
	}
}
#endif

static int ibmca_engine_digests(ENGINE * e, const EVP_MD ** digest,
				const int **nids, int nid)
{
	int i;
	if (!digest)
		return (ibmca_usable_digests(nids));

        *digest = NULL;
        for(i = 0; i < size_digest_list;i++)
                if(nid == ibmca_digest_lists.nids[i]){
                        *digest = (EVP_MD*) ibmca_digest_lists.crypto_meths[i];
                        break;
		}


	return (*digest != NULL);
}

static int ibmca_usable_digests(const int **nids)
{
        if(nids)
		*nids = ibmca_digest_lists.nids;
	return size_digest_list;
}

#ifndef OPENSSL_NO_SHA1
static int ibmca_sha1_init(EVP_MD_CTX * ctx)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA_CTX *ibmca_sha_ctx = ctx->md_data;
#else
	IBMCA_SHA_CTX *ibmca_sha_ctx = (IBMCA_SHA_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	memset((unsigned char *)ibmca_sha_ctx, 0, sizeof(*ibmca_sha_ctx));
	return 1;
}

static int ibmca_sha1_update(EVP_MD_CTX * ctx, const void *in_data,
			     unsigned long inlen)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA_CTX *ibmca_sha_ctx = ctx->md_data;
#else
	IBMCA_SHA_CTX *ibmca_sha_ctx = (IBMCA_SHA_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part=SHA_MSG_PART_MIDDLE,
		fill_size=0;
	unsigned long in_data_len=inlen;
	unsigned char tmp_hash[SHA_HASH_LENGTH];

	if (in_data_len == 0)
		return 1;

	if( ibmca_sha_ctx->c.runningLength == 0 && ibmca_sha_ctx->tail_len == 0) {
		message_part = SHA_MSG_PART_FIRST;

		ibmca_sha_ctx->tail_len = in_data_len & 0x3f;
		if(ibmca_sha_ctx->tail_len) {
			in_data_len &= ~0x3f;
			memcpy(ibmca_sha_ctx->tail, in_data + in_data_len, ibmca_sha_ctx->tail_len);
		}
	}
	else if( ibmca_sha_ctx->c.runningLength == 0 && ibmca_sha_ctx->tail_len > 0 ) {

		/* Here we need to fill out the temporary tail buffer until
		 * it has 64 bytes in it, then call ica_sha1 on that buffer.
		 * If there weren't enough bytes passed in to fill it out,
		 * just copy in what we can and return success without calling
		 * ica_sha1. - KEY
		 */

		fill_size = SHA_BLOCK_SIZE - ibmca_sha_ctx->tail_len;
		if(fill_size < in_data_len) {
			memcpy(ibmca_sha_ctx->tail + ibmca_sha_ctx->tail_len, in_data, fill_size);

			/* Submit the filled out tail buffer */
			if( p_ica_sha1( (unsigned int)SHA_MSG_PART_FIRST,
					(unsigned int)SHA_BLOCK_SIZE, ibmca_sha_ctx->tail,
					&ibmca_sha_ctx->c,
					tmp_hash)) {

				IBMCAerr(IBMCA_F_IBMCA_SHA1_UPDATE,
						IBMCA_R_REQUEST_FAILED);
				return 0;
			}
		} else {
			memcpy(ibmca_sha_ctx->tail + ibmca_sha_ctx->tail_len,
					in_data, in_data_len);
			ibmca_sha_ctx->tail_len += in_data_len;

			return 1;
		}

		/* We had to use 'fill_size' bytes from in_data to fill out the
		 * empty part of save data, so adjust in_data_len
		 */
		in_data_len -= fill_size;

		ibmca_sha_ctx->tail_len = in_data_len & 0x3f;
		if(ibmca_sha_ctx->tail_len) {
			in_data_len &= ~0x3f;
			memcpy(ibmca_sha_ctx->tail,
				in_data + fill_size + in_data_len,
				ibmca_sha_ctx->tail_len);
			// fill_size is added to in_data down below
		}
	}
	else if( ibmca_sha_ctx->c.runningLength > 0 ) {
		if(ibmca_sha_ctx->tail_len) {
			fill_size = SHA_BLOCK_SIZE - ibmca_sha_ctx->tail_len;
			if(fill_size < in_data_len) {
				memcpy(ibmca_sha_ctx->tail + ibmca_sha_ctx->tail_len,
						in_data, fill_size);

				/* Submit the filled out save buffer */
				if( p_ica_sha1( message_part,
						(unsigned int)SHA_BLOCK_SIZE, ibmca_sha_ctx->tail,
						&ibmca_sha_ctx->c,
						tmp_hash)) {

					IBMCAerr(IBMCA_F_IBMCA_SHA1_UPDATE,
							IBMCA_R_REQUEST_FAILED);
					return 0;
				}
			} else {
				memcpy(ibmca_sha_ctx->tail + ibmca_sha_ctx->tail_len,
						in_data, in_data_len);
				ibmca_sha_ctx->tail_len += in_data_len;

				return 1;
			}

			/*
			 * We had to use some of the data from in_data to
			 * fill out the empty part of save data, so adjust
			 * in_data_len
			 */
			in_data_len -= fill_size;

			ibmca_sha_ctx->tail_len = in_data_len & 0x3f;
			if(ibmca_sha_ctx->tail_len) {
				in_data_len &= ~0x3f;
				memcpy(ibmca_sha_ctx->tail,
					in_data + fill_size +in_data_len,
					ibmca_sha_ctx->tail_len);
			}
		} else {
			/* This is the odd case, where we need to go ahead and
			 * send the first X * 64 byte chunks in to be processed
			 * and copy the last <64 byte area into the tail. -KEY
			 */
			ibmca_sha_ctx->tail_len = in_data_len & 0x3f;
			if( ibmca_sha_ctx->tail_len) {
				in_data_len &= ~0x3f;
				memcpy(ibmca_sha_ctx->tail, in_data + in_data_len,
						ibmca_sha_ctx->tail_len);
			}
		}
	}

	/* If the data passed in was <64 bytes, in_data_len will be 0 */
        if( in_data_len &&
		p_ica_sha1(message_part,
			(unsigned int)in_data_len, (unsigned char *)(in_data + fill_size),
			&ibmca_sha_ctx->c,
			tmp_hash)) {

		IBMCAerr(IBMCA_F_IBMCA_SHA1_UPDATE, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}

static int ibmca_sha1_final(EVP_MD_CTX * ctx, unsigned char *md)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA_CTX *ibmca_sha_ctx = ctx->md_data;
#else
	IBMCA_SHA_CTX *ibmca_sha_ctx = (IBMCA_SHA_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part = 0;

	if (ibmca_sha_ctx->c.runningLength)
		message_part = SHA_MSG_PART_FINAL;
	else
		message_part = SHA_MSG_PART_ONLY;

	if( p_ica_sha1(message_part,
		       ibmca_sha_ctx->tail_len,
		       (unsigned char *)ibmca_sha_ctx->tail,
		       &ibmca_sha_ctx->c, md)) {

		IBMCAerr(IBMCA_F_IBMCA_SHA1_FINAL, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}				// end ibmca_sha1_final

static int ibmca_sha1_cleanup(EVP_MD_CTX * ctx)
{
	return 1;
}				// end ibmca_sha1_cleanup

#endif // OPENSSL_NO_SHA1

#ifndef OPENSSL_NO_SHA256
static int ibmca_sha256_init(EVP_MD_CTX *ctx)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = ctx->md_data;
#else
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = (IBMCA_SHA256_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	memset((unsigned char *)ibmca_sha256_ctx, 0, sizeof(*ibmca_sha256_ctx));
	return 1;
}				// end ibmca_sha256_init

static int
ibmca_sha256_update(EVP_MD_CTX *ctx, const void *in_data, unsigned long inlen)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = ctx->md_data;
#else
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = (IBMCA_SHA256_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part = SHA_MSG_PART_MIDDLE, fill_size = 0;
	unsigned long in_data_len = inlen;
	unsigned char tmp_hash[SHA256_HASH_LENGTH];

	if (in_data_len == 0)
		return 1;

	if (ibmca_sha256_ctx->c.runningLength == 0
	    && ibmca_sha256_ctx->tail_len == 0) {
		message_part = SHA_MSG_PART_FIRST;

		ibmca_sha256_ctx->tail_len = in_data_len & 0x3f;
		if(ibmca_sha256_ctx->tail_len) {
			in_data_len &= ~0x3f;
			memcpy(ibmca_sha256_ctx->tail, in_data + in_data_len,
			       ibmca_sha256_ctx->tail_len);
		}
	} else if (ibmca_sha256_ctx->c.runningLength == 0
		   && ibmca_sha256_ctx->tail_len > 0 ) {
		/* Here we need to fill out the temporary tail buffer
		 * until it has 64 bytes in it, then call ica_sha256 on
		 * that buffer.  If there weren't enough bytes passed
		 * in to fill it out, just copy in what we can and
		 * return success without calling ica_sha256. - KEY */

		fill_size = SHA256_BLOCK_SIZE - ibmca_sha256_ctx->tail_len;
		if (fill_size < in_data_len) {
			memcpy(ibmca_sha256_ctx->tail
			       + ibmca_sha256_ctx->tail_len, in_data,
			       fill_size);

			/* Submit the filled out tail buffer */
			if (p_ica_sha256((unsigned int)SHA_MSG_PART_FIRST,
					(unsigned int)SHA256_BLOCK_SIZE,
					ibmca_sha256_ctx->tail,
					&ibmca_sha256_ctx->c,
					tmp_hash)) {
				IBMCAerr(IBMCA_F_IBMCA_SHA256_UPDATE,
					 IBMCA_R_REQUEST_FAILED);
				return 0;
			}
		} else {
			memcpy(ibmca_sha256_ctx->tail
			       + ibmca_sha256_ctx->tail_len, in_data,
			       in_data_len);
			ibmca_sha256_ctx->tail_len += in_data_len;
			return 1;
		}

		/* We had to use 'fill_size' bytes from in_data to fill out the
		 * empty part of save data, so adjust in_data_len */
		in_data_len -= fill_size;

		ibmca_sha256_ctx->tail_len = in_data_len & 0x3f;
		if(ibmca_sha256_ctx->tail_len) {
			in_data_len &= ~0x3f;
			memcpy(ibmca_sha256_ctx->tail,
			       in_data + fill_size + in_data_len,
			       ibmca_sha256_ctx->tail_len);
			/* fill_size is added to in_data down below */
		}
	} else if (ibmca_sha256_ctx->c.runningLength > 0) {
		if (ibmca_sha256_ctx->tail_len) {
			fill_size = SHA256_BLOCK_SIZE - ibmca_sha256_ctx->tail_len;
			if (fill_size < in_data_len) {
				memcpy(ibmca_sha256_ctx->tail
				       + ibmca_sha256_ctx->tail_len, in_data,
				       fill_size);

				/* Submit the filled out save buffer */
				if (p_ica_sha256(message_part,
						(unsigned int)SHA256_BLOCK_SIZE,
						ibmca_sha256_ctx->tail,
						&ibmca_sha256_ctx->c,
						tmp_hash)) {
					IBMCAerr(IBMCA_F_IBMCA_SHA256_UPDATE,
						 IBMCA_R_REQUEST_FAILED);
					return 0;
				}
			} else {
				memcpy(ibmca_sha256_ctx->tail
				       + ibmca_sha256_ctx->tail_len, in_data,
				       in_data_len);
				ibmca_sha256_ctx->tail_len += in_data_len;
				return 1;
			}

			/*
			 * We had to use some of the data from in_data to
			 * fill out the empty part of save data, so adjust
			 * in_data_len
			 */
			in_data_len -= fill_size;

			ibmca_sha256_ctx->tail_len = in_data_len & 0x3f;
			if (ibmca_sha256_ctx->tail_len) {
				in_data_len &= ~0x3f;
				memcpy(ibmca_sha256_ctx->tail,
				       in_data + fill_size + in_data_len,
					ibmca_sha256_ctx->tail_len);
			}
		} else {
			/* This is the odd case, where we need to go
			 * ahead and send the first X * 64 byte chunks
			 * in to be processed and copy the last <64
			 * byte area into the tail. -KEY */
			ibmca_sha256_ctx->tail_len = in_data_len & 0x3f;
			if (ibmca_sha256_ctx->tail_len) {
				in_data_len &= ~0x3f;
				memcpy(ibmca_sha256_ctx->tail,
				       in_data + in_data_len,
				       ibmca_sha256_ctx->tail_len);
			}
		}
	}

	/* If the data passed in was <64 bytes, in_data_len will be 0 */
        if (in_data_len &&
	    p_ica_sha256(message_part,
			(unsigned int)in_data_len, (unsigned char *)(in_data + fill_size),
			&ibmca_sha256_ctx->c,
			tmp_hash)) {
		IBMCAerr(IBMCA_F_IBMCA_SHA256_UPDATE, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}				// end ibmca_sha256_update

static int ibmca_sha256_final(EVP_MD_CTX *ctx, unsigned char *md)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = ctx->md_data;
#else
	IBMCA_SHA256_CTX *ibmca_sha256_ctx = (IBMCA_SHA256_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part = 0;

	if (ibmca_sha256_ctx->c.runningLength)
		message_part = SHA_MSG_PART_FINAL;
	else
		message_part = SHA_MSG_PART_ONLY;

	if (p_ica_sha256(message_part,
			ibmca_sha256_ctx->tail_len,
			(unsigned char *)ibmca_sha256_ctx->tail,
			&ibmca_sha256_ctx->c,
			md)) {
		IBMCAerr(IBMCA_F_IBMCA_SHA256_FINAL, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}				// end ibmca_sha256_final

static int ibmca_sha256_cleanup(EVP_MD_CTX *ctx)
{
	return 1;
}				// end ibmca_sha256_cleanup
#endif // OPENSSL_NO_SHA256

#ifndef OPENSSL_NO_SHA512
static int ibmca_sha512_init(EVP_MD_CTX *ctx)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = ctx->md_data;
#else
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = (IBMCA_SHA512_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	memset((unsigned char *)ibmca_sha512_ctx, 0, sizeof(*ibmca_sha512_ctx));
	return 1;
}

static int
ibmca_sha512_update(EVP_MD_CTX *ctx, const void *in_data, unsigned long inlen)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = ctx->md_data;
#else
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = (IBMCA_SHA512_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part = SHA_MSG_PART_MIDDLE, fill_size = 0;
	unsigned long in_data_len = inlen;
	unsigned char tmp_hash[SHA512_HASH_LENGTH];

	if (in_data_len == 0)
		return 1;

	if (ibmca_sha512_ctx->c.runningLengthLow == 0
	    && ibmca_sha512_ctx->tail_len == 0) {
		message_part = SHA_MSG_PART_FIRST;

		ibmca_sha512_ctx->tail_len = in_data_len & 0x7f;
		if (ibmca_sha512_ctx->tail_len) {
			in_data_len &= ~0x7f;
			memcpy(ibmca_sha512_ctx->tail, in_data + in_data_len,
			       ibmca_sha512_ctx->tail_len);
		}
	} else if (ibmca_sha512_ctx->c.runningLengthLow == 0
		   && ibmca_sha512_ctx->tail_len > 0 ) {
		/* Here we need to fill out the temporary tail buffer
		 * until it has 128 bytes in it, then call ica_sha512 on
		 * that buffer.  If there weren't enough bytes passed
		 * in to fill it out, just copy in what we can and
		 * return success without calling ica_sha512.
		 */

		fill_size = SHA512_BLOCK_SIZE - ibmca_sha512_ctx->tail_len;
		if (fill_size < in_data_len) {
			memcpy(ibmca_sha512_ctx->tail
			       + ibmca_sha512_ctx->tail_len, in_data,
			       fill_size);

			/* Submit the filled out tail buffer */
			if (p_ica_sha512((unsigned int)SHA_MSG_PART_FIRST,
					 (unsigned int)SHA512_BLOCK_SIZE,
					 ibmca_sha512_ctx->tail,
					 &ibmca_sha512_ctx->c, tmp_hash)) {
				IBMCAerr(IBMCA_F_IBMCA_SHA512_UPDATE,
					 IBMCA_R_REQUEST_FAILED);
				return 0;
			}
		} else {
			memcpy(ibmca_sha512_ctx->tail
			       + ibmca_sha512_ctx->tail_len, in_data,
			       in_data_len);
			ibmca_sha512_ctx->tail_len += in_data_len;
			return 1;
		}

		/* We had to use 'fill_size' bytes from in_data to fill out the
		 * empty part of save data, so adjust in_data_len
		 */
		in_data_len -= fill_size;

		ibmca_sha512_ctx->tail_len = in_data_len & 0x7f;
		if (ibmca_sha512_ctx->tail_len) {
			in_data_len &= ~0x7f;
			memcpy(ibmca_sha512_ctx->tail,
			       in_data + fill_size + in_data_len,
			       ibmca_sha512_ctx->tail_len);
			/* fill_size is added to in_data down below */
		}
	} else if (ibmca_sha512_ctx->c.runningLengthLow > 0) {
		if (ibmca_sha512_ctx->tail_len) {
			fill_size = SHA512_BLOCK_SIZE - ibmca_sha512_ctx->tail_len;
			if (fill_size < in_data_len) {
				memcpy(ibmca_sha512_ctx->tail
				       + ibmca_sha512_ctx->tail_len, in_data,
					fill_size);

				/* Submit the filled out save buffer */
				if (p_ica_sha512(message_part,
						(unsigned int)SHA512_BLOCK_SIZE,
						ibmca_sha512_ctx->tail,
						&ibmca_sha512_ctx->c,
						tmp_hash)) {
					IBMCAerr(IBMCA_F_IBMCA_SHA512_UPDATE,
						 IBMCA_R_REQUEST_FAILED);
					return 0;
				}
			} else {
				memcpy(ibmca_sha512_ctx->tail
				       + ibmca_sha512_ctx->tail_len, in_data,
				       in_data_len);
				ibmca_sha512_ctx->tail_len += in_data_len;
				return 1;
			}

			/*
			 * We had to use some of the data from in_data to
			 * fill out the empty part of save data, so adjust
			 * in_data_len
			 */
			in_data_len -= fill_size;

			ibmca_sha512_ctx->tail_len = in_data_len & 0x7f;
			if (ibmca_sha512_ctx->tail_len) {
				in_data_len &= ~0x7f;
				memcpy(ibmca_sha512_ctx->tail,
				       in_data + fill_size + in_data_len,
				       ibmca_sha512_ctx->tail_len);
			}
		} else {
			/* This is the odd case, where we need to go
			 * ahead and send the first X * 128 byte chunks
			 * in to be processed and copy the last <128
			 * byte area into the tail.
			 */
			ibmca_sha512_ctx->tail_len = in_data_len & 0x7f;
			if (ibmca_sha512_ctx->tail_len) {
				in_data_len &= ~0x7f;
				memcpy(ibmca_sha512_ctx->tail,
				       in_data + in_data_len,
				       ibmca_sha512_ctx->tail_len);
			}
		}
	}

	/* If the data passed in was <128 bytes, in_data_len will be 0 */
	if (in_data_len &&
	    p_ica_sha512(message_part, (unsigned int)in_data_len,
			 (unsigned char *)(in_data + fill_size),
			 &ibmca_sha512_ctx->c, tmp_hash)) {
		IBMCAerr(IBMCA_F_IBMCA_SHA512_UPDATE, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}

static int ibmca_sha512_final(EVP_MD_CTX *ctx, unsigned char *md)
{
#ifdef OLDER_OPENSSL
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = ctx->md_data;
#else
	IBMCA_SHA512_CTX *ibmca_sha512_ctx = (IBMCA_SHA512_CTX *) EVP_MD_CTX_md_data(ctx);
#endif
	unsigned int message_part = 0;

	if (ibmca_sha512_ctx->c.runningLengthLow)
		message_part = SHA_MSG_PART_FINAL;
	else
		message_part = SHA_MSG_PART_ONLY;

	if (p_ica_sha512(message_part, ibmca_sha512_ctx->tail_len,
			 (unsigned char *)ibmca_sha512_ctx->tail,
			 &ibmca_sha512_ctx->c, md)) {
		IBMCAerr(IBMCA_F_IBMCA_SHA512_FINAL, IBMCA_R_REQUEST_FAILED);
		return 0;
	}

	return 1;
}

static int ibmca_sha512_cleanup(EVP_MD_CTX *ctx)
{
	return 1;
}
#endif // OPENSSL_NO_SHA512

static int ibmca_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
			 const BIGNUM *m, BN_CTX *ctx)
{
	/* r = (a^p) mod m
	                        r = output
	                        a = input
	                        p = exponent
	                        m = modulus
	*/

	unsigned char *input = NULL, *output =  NULL;
	ica_rsa_key_mod_expo_t *key = NULL;
	unsigned int rc;
	int plen, mlen, inputlen;

	if (!ibmca_dso) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_NOT_LOADED);
		goto err;
	}

	/*
	 make necessary memory allocations
	 FIXME: Would it be possible to minimize memory allocation overhead by either
                allocating it all at once or having a static storage?
	*/
	key = (ica_rsa_key_mod_expo_t *) calloc(1, sizeof(ica_rsa_key_mod_expo_t));
	if (key == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	key->key_length = mlen = BN_num_bytes(m);

	key->modulus = (unsigned char *) calloc(1, key->key_length);
	if (key->modulus == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	plen = BN_num_bytes(p);

	/* despite plen, key->exponent must be key->key_length in size */
	key->exponent = (unsigned char *) calloc(1, key->key_length);
	if (key->exponent == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	inputlen = BN_num_bytes(a);

	/* despite inputlen, input and output must be key->key_length in size */
	input = (unsigned char *) calloc(1, key->key_length);
	if (input == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	output = (unsigned char *) calloc(1, key->key_length);
	if (output == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	/* Now convert from BIGNUM representation.
	 * Everything must be right-justified
	 */
	BN_bn2bin(m, key->modulus);

	BN_bn2bin(p, key->exponent + key->key_length - plen);

	BN_bn2bin(a, input + key->key_length - inputlen);

	/* execute the ica mod_exp call */
	rc = p_ica_rsa_mod_expo(ibmca_handle, input, key, output);
	if (rc != 0) {
		goto err;
	}
	else {
		rc = 1;
	}

        /* Convert output to BIGNUM representation.
	 * right-justified output applies
	 */
	/* BN_bin2bn((unsigned char *) (output + key->key_length - inputlen), inputlen, r); */
	BN_bin2bn((unsigned char *) output, key->key_length, r);

	goto end;

err:
	rc = 0;    /* error condition */

end:
	free(key->exponent);
	free(key->modulus);
	free(key);
	free(input);
	free(output);

	return rc;
}

#ifndef OPENSSL_NO_RSA
static int ibmca_rsa_init(RSA *rsa)
{
	RSA_blinding_off(rsa);

	return 1;
}

#ifdef OLDER_OPENSSL
static int ibmca_rsa_mod_exp(BIGNUM * r0, const BIGNUM * I, RSA * rsa,
                             BN_CTX *ctx)
{
	int to_return = 0;

	if (!rsa->p || !rsa->q || !rsa->dmp1 || !rsa->dmq1 || !rsa->iqmp) {
		if (!rsa->d || !rsa->n) {
			IBMCAerr(IBMCA_F_IBMCA_RSA_MOD_EXP,
				 IBMCA_R_MISSING_KEY_COMPONENTS);
			goto err;
		}
		to_return = ibmca_mod_exp(r0, I, rsa->d, rsa->n, ctx);
	} else {
		to_return =
		    ibmca_mod_exp_crt(r0, I, rsa->p, rsa->q, rsa->dmp1,
				      rsa->dmq1, rsa->iqmp, ctx);
	}
err:
	return to_return;
}
#else
static int ibmca_rsa_mod_exp(BIGNUM * r0, const BIGNUM * I, RSA * rsa,
                             BN_CTX *ctx)
{
	int to_return = 0;
	const BIGNUM *d, *n, *p, *q, *dmp1, *dmq1, *iqmp;

	RSA_get0_key(rsa, &n, NULL, &d);
	RSA_get0_factors(rsa, &p, &q);
	RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);
	if (!p || !q || !dmp1 || !dmq1 || iqmp) {
		if (!d || !n) {
			IBMCAerr(IBMCA_F_IBMCA_RSA_MOD_EXP,
				 IBMCA_R_MISSING_KEY_COMPONENTS);
			goto err;
		}
		to_return = ibmca_mod_exp(r0, I, d, n, ctx);
	} else {
		to_return = ibmca_mod_exp_crt(r0, I, p, q, dmp1, dmq1, iqmp, ctx);
	}
err:
	return to_return;
}
#endif
#endif

/* Ein kleines chinesisches "Restessen"  */
static int ibmca_mod_exp_crt(BIGNUM * r, const BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * q,
			     const BIGNUM * dmp1, const BIGNUM * dmq1,
			     const BIGNUM * iqmp, BN_CTX * ctx)
{
	/*
	r = output
	a = input
	p and q are themselves
	dmp1, dmq1 are dp and dq respectively
	iqmp is qInverse
	*/

	ica_rsa_key_crt_t *key = NULL;
	unsigned char *output = NULL, *input = NULL;
	int rc;
	int plen, qlen, dplen, dqlen, qInvlen;
	int inputlen;

	/*
	 make necessary memory allocations
	 FIXME: Would it be possible to minimize memory allocation overhead by either
                allocating it all at once or having a static storage?
	*/
	key = (ica_rsa_key_crt_t *) calloc(1, sizeof(ica_rsa_key_crt_t));
	if (key == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	/* buffers pointed by p, q, dp, dq and qInverse in struct
	 * ica_rsa_key_crt_t must be of size key_legth/2 or larger.
	 * p, dp and qInverse have an additional 8-byte padding. */

	plen = BN_num_bytes(p);
	qlen = BN_num_bytes(q);
	key->key_length = 2 * (plen > qlen ? plen : qlen);

	key->p = (unsigned char *) calloc(1, (key->key_length/2) + 8);
	if (key->p == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	dplen = BN_num_bytes(dmp1);
	key->dp = (unsigned char *) calloc(1, (key->key_length/2) + 8);
	if (key->dp == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	key->q = (unsigned char *) calloc(1, key->key_length/2);
	if (key->q == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	dqlen = BN_num_bytes(dmq1);
	key->dq = (unsigned char *) calloc(1, key->key_length/2);
	if (key->dq == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	qInvlen = BN_num_bytes(iqmp);
	key->qInverse = (unsigned char *) calloc(1, (key->key_length/2) + 8);
	if (key->qInverse == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	inputlen = BN_num_bytes(a);
	if (inputlen > key->key_length) {     /* input can't be larger than key */
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	/* allocate input to the size of key_length in bytes, and
	 * pad front with zero if inputlen < key->key_length */
	input = (unsigned char *) calloc(1, key->key_length);
	if (input == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}

	/* output must also be key_length in size */
	output = (unsigned char *) calloc(1, key->key_length);
	if (output == NULL) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}


	/* Now convert from BIGNUM representation.
	 * p, dp and qInverse have an additional 8-byte padding,
	 * and everything must be right-justified */
	BN_bn2bin(p, key->p + 8 + (key->key_length/2) - plen);

	BN_bn2bin(dmp1, key->dp + 8 + (key->key_length/2) - dplen);

	BN_bn2bin(q, key->q + (key->key_length/2) - qlen);

	BN_bn2bin(dmq1, key->dq + (key->key_length/2) - dqlen);

	BN_bn2bin(iqmp, key->qInverse + 8 + (key->key_length/2) - qInvlen);

	BN_bn2bin(a, input + key->key_length - inputlen);

	/* execute the ica crt call */

	rc = p_ica_rsa_crt(ibmca_handle, input, key, output);
	if (rc != 0) {
		IBMCAerr(IBMCA_F_IBMCA_MOD_EXP, IBMCA_R_REQUEST_FAILED);
		goto err;
	}
	else {
		rc = 1;
	}

	/* Convert output to BIGNUM representation */
	/* BN_bin2bn((unsigned char *) (output + key->key_length - inputlen), inputlen, r); */
	BN_bin2bn((unsigned char *) output, key->key_length, r);


	goto end;

err:
	rc = 0;    /* error condition */

end:
	free(key->p);
	free(key->q);
	free(key->dp);
	free(key->dq);
	free(key->qInverse);
	free(key);
	free(input);
	free(output);

	return rc;
}

#ifndef OPENSSL_NO_DSA
/* This code was liberated and adapted from the commented-out code in
 * dsa_ossl.c. Because of the unoptimised form of the Ibmca acceleration
 * (it doesn't have a CRT form for RSA), this function means that an
 * Ibmca system running with a DSA server certificate can handshake
 * around 5 or 6 times faster/more than an equivalent system running with
 * RSA. Just check out the "signs" statistics from the RSA and DSA parts
 * of "openssl speed -engine ibmca dsa1024 rsa1024". */
#ifdef OLDER_OPENSSL
static int ibmca_dsa_mod_exp(DSA * dsa, BIGNUM * rr, BIGNUM * a1,
			     BIGNUM * p1, BIGNUM * a2, BIGNUM * p2,
			     BIGNUM * m, BN_CTX * ctx,
			     BN_MONT_CTX * in_mont)
#else
static int ibmca_dsa_mod_exp(DSA * dsa, BIGNUM * rr, const BIGNUM * a1,
			     const BIGNUM * p1, const BIGNUM * a2,
			     const BIGNUM * p2, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * in_mont)
#endif
{
	BIGNUM *t;
	int to_return = 0;

	t = BN_new();
	/* let rr = a1 ^ p1 mod m */
	if (!ibmca_mod_exp(rr, a1, p1, m, ctx))
		goto end;
	/* let t = a2 ^ p2 mod m */
	if (!ibmca_mod_exp(t, a2, p2, m, ctx))
		goto end;
	/* let rr = rr * t mod m */
	if (!BN_mod_mul(rr, rr, t, m, ctx))
		goto end;
	to_return = 1;
end:
	BN_free(t);
	return to_return;
}

#ifdef OLDER_OPENSSL
static int ibmca_mod_exp_dsa(DSA * dsa, BIGNUM * r, BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * m_ctx)
#else
static int ibmca_mod_exp_dsa(DSA * dsa, BIGNUM * r, const BIGNUM * a,
			     const BIGNUM * p, const BIGNUM * m,
			     BN_CTX * ctx, BN_MONT_CTX * m_ctx)
#endif
{
	return ibmca_mod_exp(r, a, p, m, ctx);
}
#endif

/* This function is aliased to mod_exp (with the mont stuff dropped). */
static int ibmca_mod_exp_mont(BIGNUM * r, const BIGNUM * a,
			      const BIGNUM * p, const BIGNUM * m,
			      BN_CTX * ctx, BN_MONT_CTX * m_ctx)
{
	return ibmca_mod_exp(r, a, p, m, ctx);
}

#ifndef OPENSSL_NO_DH
/* This function is aliased to mod_exp (with the dh and mont dropped). */
static int ibmca_mod_exp_dh(DH const *dh, BIGNUM * r,
			    const BIGNUM * a, const BIGNUM * p,
			    const BIGNUM * m, BN_CTX * ctx,
			    BN_MONT_CTX * m_ctx)
{
	return ibmca_mod_exp(r, a, p, m, ctx);
}
#endif

/* Random bytes are good */
static int ibmca_rand_bytes(unsigned char *buf, int num)
{
	unsigned int rc;

	rc = p_ica_random_number_generate(num, buf);
	if (rc < 0) {
		IBMCAerr(IBMCA_F_IBMCA_RAND_BYTES, IBMCA_R_REQUEST_FAILED);
		return 0;
	}
	return 1;
}

static int ibmca_rand_status(void)
{
	return 1;
}

/* This stuff is needed if this ENGINE is being compiled into a self-contained
 * shared-library. */
static int bind_fn(ENGINE * e, const char *id)
{
	if (id && (strcmp(id, engine_ibmca_id) != 0))	/* WJH XXX */
		return 0;
	if (!bind_helper(e))
		return 0;
	return 1;
}

IMPLEMENT_DYNAMIC_CHECK_FN()
IMPLEMENT_DYNAMIC_BIND_FN(bind_fn)

#endif				/* !OPENSSL_NO_HW_IBMCA */
#endif				/* !OPENSSL_NO_HW */
