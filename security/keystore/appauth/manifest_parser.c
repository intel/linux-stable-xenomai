// SPDX-License-Identifier: GPL-2.0
// Intel Keystore Linux driver, Copyright (c) 2018, Intel Corporation.
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <string.h>
#include <stdio.h>
#endif

#include "manifest_parser.h"
#include "app_auth.h"

#define MAX_CHUNKS 10

const uint8_t digest_len[] = {
	20, /* sha1   */
	28, /* sha224 */
	32, /* sha256 */
	48, /* sha384 */
	64  /* sha512 */
};

struct mf_envelope {
	uint8_t version;
	uint16_t len;
	uint8_t data[];
} __packed;

struct mf_chunk {
	uint8_t type;
	uint16_t len;
	uint8_t data[];
} __packed;

static const struct mf_chunk *mf_get_chunk(const uint8_t *mf,
		uint8_t chunk_type)
{
	const struct mf_envelope *env = (const struct mf_envelope *) mf;

	if (env) {
		uint16_t i, pos = 0;

		for (i = 0; pos < env->len && i < MAX_CHUNKS; i++) {
			const struct mf_chunk *chunk =
				(const struct mf_chunk *) (env->data + pos);
			if (chunk->type == chunk_type) {
				if (pos + chunk->len + 3 <= env->len + 1)
					return chunk;
				else
					return NULL;
			}
			pos += chunk->len + 3;
		}
	}
	return NULL;
}

static const uint8_t *mf_get_chunk_data(const uint8_t *mf,
		uint8_t chunk_type, size_t *len)
{
	const struct mf_chunk *chunk = mf_get_chunk(mf, chunk_type);

	if (len) {
		*len = 0;
		if (chunk) {
			*len = chunk->len;
			return chunk->data;
		}
	}
	return NULL;
}

uint8_t mf_get_version(const uint8_t *mf)
{
	const struct mf_envelope *env = (const struct mf_envelope *) mf;

	return env ? env->version : 0;
}

const char *mf_get_name(const uint8_t *mf)
{
	size_t len = 0;
	const uint8_t *data = mf_get_chunk_data(mf, TYPE_MANIFEST_NAME, &len);

	if (data && len < 256) {
		const char *name = (const char *) data;

		if (strlen(name) + 1 == len)
			return name;
	}
	return NULL;
}

const struct mf_app_data *mf_get_app_data(const uint8_t *mf)
{
	size_t len = 0;
	const struct mf_app_data *data = (const struct mf_app_data *)
			mf_get_chunk_data(mf, TYPE_MANIFEST_DATA, &len);
	if (data) {
		const char *name = (const char *) data->app_name;

		if (strlen(name) + 1 == data->app_name_len)
			return data;
	}
	return NULL;
}

int mf_init_file_list_ctx(const uint8_t *mf,
		struct mf_files_ctx *ctx)
{
	if (ctx) {
		size_t len = 0;
		const struct mf_app_data *data = (const struct mf_app_data *)
			mf_get_chunk_data(mf, TYPE_MANIFEST_DATA, &len);
		if (data) {
			ctx->num_files_left = data->num_files;
			ctx->bytes_left = len - 8 - data->app_name_len;
			ctx->next_file =
			(uint8_t *) (data->app_name + data->app_name_len);
			ks_debug("DEBUG_APPAUTH: num_files_left = %d\n",
				 ctx->num_files_left);
			ks_debug("DEBUG_APPAUTH: bytes_left = %lu\n",
				 ctx->bytes_left);
			return ctx->num_files_left;
		}
	}
	return 0;
}

const char *mf_get_next_file(const uint8_t *mf,
	struct mf_files_ctx *ctx, uint8_t *digest_algo_id, uint8_t **digest,
	uint32_t *size)
{
	if (ctx && ctx->num_files_left > 0 && ctx->bytes_left > 0 &&
			ctx->next_file && digest_algo_id && digest) {
		uint8_t filenamelen = *ctx->next_file;
		const char *filename = (const char *) (ctx->next_file + 1);

		ks_debug("DEBUG_APPAUTH: filenamelen  = %d\n", filenamelen);

		if (strlen(filename) + 1 == filenamelen) {
			size_t entry_size;
			*size =	*((uint32_t *)(ctx->next_file +
						filenamelen + 1));
			*digest_algo_id = *((uint8_t *)(ctx->next_file +
						filenamelen + 5));
			*digest = ctx->next_file + filenamelen + 6;
			if (*digest_algo_id >= sizeof(digest_len))
				return NULL;
			entry_size = 1 + filenamelen + 5 +
				digest_len[*digest_algo_id];
			ctx->num_files_left--;
			ctx->bytes_left -= entry_size;
			ctx->next_file += entry_size;
			return filename;
		} else
			return ERR_PTR(-EFAULT);
	}
	return NULL;
}

const uint8_t *mf_get_data(const uint8_t *mf, size_t *len)
{
	return mf_get_chunk_data(mf, TYPE_MANIFEST_DATA, len);
}

const uint8_t *mf_get_public_key(const uint8_t *mf, size_t *len)
{
	return mf_get_chunk_data(mf, TYPE_MANIFEST_PUBLIC_KEY, len);
}

const uint8_t *mf_get_certificate(const uint8_t *mf, size_t *len)
{
	return mf_get_chunk_data(mf, TYPE_MANIFEST_CERTIFICATE, len);
}

const uint8_t *mf_get_signature(const uint8_t *mf, size_t *len)
{
	return mf_get_chunk_data(mf, TYPE_MANIFEST_SIGNATURE, len);
}
