
#include <tee_client_api.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define FSCRYPT_MAX_KEY_SIZE 64
#define HOBOT_KEY_LENTH	(32u)
#define PBKDF_TEE_MAX_HASH_CMD_LEN 8
#define PBKDF_TEE_HASH_NAME_LEN    16
#define PBKDF_TEE_MAX_PASSWD_LEN   32
#define PBKDF_TEE_MAX_SALT_LEN     32
#define HB_KDF_ALGO_PBKDF2_FDE 0xA00000EE

#define HB_KM_PTA_UUID \
	{ 0x20734bc5, 0x50d2, 0x4725, \
		{ 0x9a, 0x02, 0xbd, 0x12, 0xd6, 0xbe, 0xe3, 0x1c } }

#define HB_FDE_PTA_UUID \
	{ 0x89b589e9, 0x5339, 0x41d1, \
		{ 0xa1, 0x38, 0xdc, 0xa3, 0x32, 0xcd, 0x0c, 0x88 } }

#define HB_KM_PTA_CMD_DERIVE_KEY             (0x09)
#define HB_PTA_GET_FDE_KEY                   (0x0)

static unsigned char fde_passwd_seed[] = {
	0xe4, 0x05, 0x8c, 0x56, 0x18, 0xdd, 0x2d, 0x48,
	0x1b, 0x06, 0x22, 0x91, 0x68, 0x93, 0xa9, 0x6a,
	0x2a, 0x01, 0xeb, 0xe6, 0x3c, 0x24, 0x47, 0x91,
	0xe1, 0xd6, 0xef, 0xd6, 0xa7, 0xc5, 0xf3, 0xbb,
};
static unsigned char fde_salt_value[] = {
	0xb7, 0x9c, 0x4b, 0x93, 0x14, 0x44, 0x13, 0x73,
	0x36, 0x86, 0x07, 0x9c, 0x53, 0x81, 0xaf, 0x9f,
	0xaf, 0x31, 0x67, 0x92, 0xc3, 0x6d, 0x3b, 0xb7,
	0x0e, 0x86, 0x1e, 0x2d, 0xb7, 0x84, 0x76, 0x16,
};

typedef struct
{
	uint8_t   passwd[PBKDF_TEE_MAX_PASSWD_LEN];  /**< passwd */
	uint8_t   salt[PBKDF_TEE_MAX_SALT_LEN];  /**< salt value */
	uint8_t   fde_data[HOBOT_KEY_LENTH];  /**< salt value */
	uint32_t  interation;  /**< interation:default:2 */
	uint32_t  key_len; /* default:HB_KDF_FDE_KEY_LEN 32bytes*/
} hb_pbkdf2_fde_scheme;

typedef struct
{
	//key_serial_t         target_key_id;;   /**< Input params for deriving a key;(The key material length to be derived must be >= 16 bytes and <= slot size)*/
	uint32_t             kdf_type;   /**< Input key derivation algorithm:HB_KDF_ALGO_PBKDF2HMAC*/
	//uint32_t             reserved;
	union
	{
		hb_pbkdf2_fde_scheme fde_schme;
	} Sch;
} hb_key_derive_req;

typedef struct {
	uint32_t magic; /**< magic naumber. HB_KM_HEAD_MAGIC. */
	uint32_t version; /**< NA */
	union
	{
		hb_key_derive_req       	   derive_req; /**< derive a key request. */
	} srv;
} hb_key_desc;

static void dump(uint8_t *p, int n)
{
	int x;

	for (x = 0; x < n; x++) {
		printf("%02x", ((char *)p)[x]);
	}
	printf("\n");
}

//coverity[misra_c_2012_rule_8_7_violation:SUPPRESS], ## violation reason SYSSW_V_8.7_02
static int hb_km_key_derive(void *payload, size_t plen, void *data_addr, size_t data_len)
{
	TEEC_UUID uuid = HB_KM_PTA_UUID;
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result res;
	TEEC_Context ctx = { 0 };    /**< TEE context */
	TEEC_Session sess = { 0 };   /**< TEE session */

	res = TEEC_InitializeContext(NULL, &ctx);
	//coverity[misra_c_2012_rule_10_4_violation:SUPPRESS], ## violation reason SYSSW_V_10.4_01
	if (res != TEEC_SUCCESS)
		goto exit;

	res = TEEC_OpenSession(&ctx, &sess, &uuid,
			TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	//coverity[misra_c_2012_rule_10_4_violation:SUPPRESS], ## violation reason SYSSW_V_10.4_01
	if (TEEC_SUCCESS != res)
		goto close_ctx;

	memset(&op, 0, sizeof(op));
	//coverity[misra_c_2012_rule_10_1_violation:SUPPRESS], ## violation reason SYSSW_V_10.1_01
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT,
					TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = payload;
	op.params[0].tmpref.size = plen;


	res = TEEC_InvokeCommand(&sess, HB_KM_PTA_CMD_DERIVE_KEY,
				&op, &origin);

	TEEC_CloseSession(&sess);
close_ctx:
	TEEC_FinalizeContext(&ctx);
exit:
	return res;
}

static int fde_get_key_from_km(unsigned int times)
{
	TEEC_Result teecErr;
	hb_key_desc desc;

	memset(&desc, 0, sizeof(hb_key_desc));
	memcpy(desc.srv.derive_req.Sch.fde_schme.passwd, fde_passwd_seed, PBKDF_TEE_MAX_PASSWD_LEN);
	memcpy(desc.srv.derive_req.Sch.fde_schme.salt, fde_salt_value, PBKDF_TEE_MAX_SALT_LEN);
	desc.srv.derive_req.kdf_type = HB_KDF_ALGO_PBKDF2_FDE;
	desc.srv.derive_req.Sch.fde_schme.interation = times;
	desc.srv.derive_req.Sch.fde_schme.key_len = HOBOT_KEY_LENTH;
	teecErr = hb_km_key_derive(&desc, sizeof(desc), NULL, 0);
	if (teecErr != 0)
		return teecErr;

	dump(desc.srv.derive_req.Sch.fde_schme.fde_data, HOBOT_KEY_LENTH);
	return teecErr;
}

static int hb_fde_key_derive(void *payload, size_t plen)
{
	TEEC_UUID uuid = HB_FDE_PTA_UUID;
	TEEC_Operation op;
	uint32_t origin;
	TEEC_Result res;
	TEEC_Context ctx = { 0 };    /**< TEE context */
	TEEC_Session sess = { 0 };   /**< TEE session */

	res = TEEC_InitializeContext(NULL, &ctx);
	//coverity[misra_c_2012_rule_10_4_violation:SUPPRESS], ## violation reason SYSSW_V_10.4_01
	if (res != TEEC_SUCCESS)
		goto exit;

	res = TEEC_OpenSession(&ctx, &sess, &uuid,
			TEEC_LOGIN_USER, NULL, NULL, &origin);
	//coverity[misra_c_2012_rule_10_4_violation:SUPPRESS], ## violation reason SYSSW_V_10.4_01
	if (TEEC_SUCCESS != res)
		goto close_ctx;

	memset(&op, 0, sizeof(op));
	//coverity[misra_c_2012_rule_10_1_violation:SUPPRESS], ## violation reason SYSSW_V_10.1_01
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT,
					TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = payload;
	op.params[0].tmpref.size = plen;

	res = TEEC_InvokeCommand(&sess, HB_PTA_GET_FDE_KEY,
				&op, &origin);

	TEEC_CloseSession(&sess);
close_ctx:
	TEEC_FinalizeContext(&ctx);
exit:
	return res;
}

static int fde_get_key_from_bin(char *key_path)
{
	FILE *fp = NULL;
	TEEC_Result teecErr;
	size_t len = 0;
	uint8_t enc_key[HOBOT_KEY_LENTH] = {0};

	if (key_path == NULL)
		return -1;

	fp = fopen(key_path, "rb");
	if (fp == NULL)
		return -1;

	len = fread(enc_key, 1, sizeof(enc_key), fp);
	if (len < HOBOT_KEY_LENTH) {
		printf("key file %s is too short, should be 32 bytes\n", key_path);
		fclose(fp);
		return -1;
	}

	fclose(fp);

	teecErr = hb_fde_key_derive(enc_key, sizeof(enc_key));
	if (teecErr != 0)
		return teecErr;

	dump(enc_key, HOBOT_KEY_LENTH);
	return teecErr;
}

void usage(char *argv[])
{
	printf("Usage:\n");
	printf("\t%s [--km]                get key from key manager\n", argv[0]);
	printf("\t%s [--bin key_path]      get key from key_path\n", argv[0]);
}

int main(int argc, char **argv)
{
	if ((argc == 2) && (!strncmp(argv[1], "--km", strlen(argv[1]))))
		return fde_get_key_from_km(1);
	else if ((argc == 3) && (!strncmp(argv[1], "--bin", strlen(argv[1])))) {
		return fde_get_key_from_bin(argv[2]);
	} else {
		usage(argv);
		return -1;
	}
}
