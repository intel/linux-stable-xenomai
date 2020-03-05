// SPDX-License-Identifier: GPL-2.0
// Intel Keystore Linux driver, Copyright (c) 2018, Intel Corporation.
#include "app_auth.h"
#include "manifest_parser.h"

/**
 * Allocates the crypto shash structure.
 *
 * @param algo         - hash algorithm.
 *
 * @return the pointer to the tfm.
 */
struct crypto_shash *appauth_alloc_tfm(enum hash_algo algo)
{
	struct crypto_shash *tfm = NULL;
	int rc;

	if (algo < 0 || algo >= HASH_ALGO__LAST)
		algo = manifest_default_hash_algo;

	tfm = crypto_alloc_shash(hash_algo_name[algo], 0, 0);
	if (IS_ERR(tfm)) {
		rc = PTR_ERR(tfm);
		ks_err("Can not allocate %s (reason: %d)\n",
				hash_algo_name[algo], rc);
	}

	return tfm;
}

/**
 * Frees the crypto shash structure.
 *
 * @param tfm         - address of crypto shash structure.
 *
 */
void appauth_free_tfm(struct crypto_shash *tfm)
{
	crypto_free_shash(tfm);
}

/**
 * Reads specified number of bytes from the file.
 *
 * @param file         - file structure.
 * @param offset       - offset in the file.
 * @param addr         - address of the buffer.
 * @param count        - size of the buffer .
 *
 * @return 0 if success or error code.
 */
int appauth_kernel_read(struct file *file, loff_t offset,
				char *addr, unsigned long count)
{
	char __user *buf = (char __user *)addr;
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;

	ret = kernel_read(file, buf, count, &offset);

	return ret;
}

/**
 * Calculate the hash of the file and store the result in 'hash'.
 *
 * @param file         - file structure.
 * @param hash         - pointer to appauth_digest.
 * @param tfm          - pointer to crypto shash structure.
 *
 * @return 0 if success or error code.
 */
static int appauth_calc_file_hash_tfm(struct file *file,
		struct appauth_digest *hash, struct crypto_shash *tfm)
{
	loff_t i_size, offset = 0;
	char *file_buf;
	SHASH_DESC_ON_STACK(shash, tfm);
	int rc, read = 0, count = 0;

	if (!file || !hash)
		return -EFAULT;

	shash->tfm = tfm;
	if (!shash->tfm)
		return -EFAULT;
	shash->flags = 0;

	hash->len = crypto_shash_digestsize(tfm);

	rc = crypto_shash_init(shash);
	if (rc != 0) {
		ks_err("DEBUG_APPAUTH: crypto_shash_init() failed\n");
		return -HASH_FAILURE;
	}

	i_size = i_size_read(file_inode(file));
	ks_debug("DEBUG_APPAUTH: file size = %lld\n", i_size);

	if (i_size == 0)
		goto out;

	file_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!file_buf)
		return -ENOMEM;

	if (!(file->f_mode & FMODE_READ)) {
		file->f_mode |= FMODE_READ;
		read = 1;
	}

	while (offset < i_size) {
		int file_buf_len;

		file_buf_len = appauth_kernel_read(file, offset,
						file_buf, PAGE_SIZE);
		if (file_buf_len < 0) {
			rc = file_buf_len;
			break;
		}
		if (file_buf_len == 0)
			break;
		offset += file_buf_len;
		count++;

		rc = crypto_shash_update(shash, file_buf, file_buf_len);
		if (rc)
			break;
	}
	ks_debug("DEBUG_APPAUTH: count = %d\n", count);
	if (read)
		file->f_mode &= ~FMODE_READ;
	kfree(file_buf);
out:
	if (!rc)
		rc = crypto_shash_final(shash, hash->digest);
	if (rc)
		return -HASH_FAILURE;
	return rc;
}

/**
 * Calculate the hash of the file and store the result in 'hash'.
 * Allocates and frees crypto tfm.
 *
 * @param file         - file structure.
 * @param hash         - pointer to appauth_digest.
 *
 * @return 0 if success or error code (see enum APP_AUTH_ERROR).
 */
static int appauth_calc_file_shash(struct file *file,
		struct appauth_digest *hash)
{
	struct crypto_shash *tfm;
	int ret = 0;

	if (!file || !hash)
		return -EFAULT;

	tfm = appauth_alloc_tfm(hash->algo);
	if (IS_ERR(tfm)) {
		ks_err("DEBUG_APPAUTH: appauth_alloc_tfm failed\n");
		return -HASH_FAILURE;
	}
	ks_debug("DEBUG_APPAUTH: appauth_alloc_tfm succeeded\n");
	ret = appauth_calc_file_hash_tfm(file, hash, tfm);

	appauth_free_tfm(tfm);

	return ret;
}

/**
 * Calculate the hash of the file and store the result in 'hash'.
 * Locks the file inode.
 *
 * @param file         - file structure.
 * @param hash         - pointer to appauth_digest.
 *
 * @return 0 if success or error code (see enum APP_AUTH_ERROR).
 */
static int process_file(struct file *file, struct appauth_digest *hash)
{
	int result = 0;

	ks_debug("DEBUG_APPAUTH: appauth_calc_file_shash() started\n");
	ks_debug("DEBUG_APPAUTH: calling mutex_lock\n");
	mutex_lock(&(file->f_pos_lock));
	result = appauth_calc_file_shash(file, hash);
	mutex_unlock(&(file->f_pos_lock));
	ks_debug("DEBUG_APPAUTH: appauth_calc_file_shash() finished\n");
	keystore_hexdump("", hash->digest, hash_digest_size[hash->algo]);
	return result;
}

/**
 * Converts the hash id to kernel crypto hash id's.
 *
 * @param digest_algo_id - hash id used in manifest file.
 *
 * @returns converted hash id, HASH_ALGO_SHA1 is the default.
 */
static int convert_hash_id(uint8_t digest_algo_id)
{
	if (DIGEST_ALGO_MD5)
		return HASH_ALGO_MD5;
	else if (DIGEST_ALGO_SHA1)
		return HASH_ALGO_SHA1;
	else if (DIGEST_ALGO_SHA224)
		return HASH_ALGO_SHA224;
	else if (DIGEST_ALGO_SHA384)
		return HASH_ALGO_SHA384;
	else if (DIGEST_ALGO_SHA512)
		return HASH_ALGO_SHA512;
	else
		return HASH_ALGO_SHA1;
}

/**
 * Computes the file hash and compares against the digest in the manifest.
 *
 * @param filename       - absolute path of the file.
 * @param digest         - file digest presnt in the manifest file.
 * @param digest_algo_id - hash id used in manifest file.
 *
 * @return 0,if success or error code.
 */
int compute_file_hash(const char *filename, uint8_t *digest,
					    uint8_t digest_algo_id)
{
	struct file *file = 0;
	struct appauth_digest hash;
	int ret = 0;

	file = filp_open(filename,  O_RDONLY, 0);

	if (IS_ERR(file)) {
		ks_err("DEBUG_APPAUTH: filp_open failed\n");
		return -EBADF;
	}
	ks_debug("DEBUG_APPAUTH: filp_open succeeded\n");
	hash.algo = convert_hash_id(digest_algo_id);
	ret = process_file(file, &hash);
	filp_close(file, NULL);
	if (ret < 0)
		return ret;
	ks_debug("DEBUG_APPAUTH: digest read from manifest:\n");
	keystore_hexdump("", digest, hash_digest_size[hash.algo]);
	if (!memcmp(hash.digest, digest, hash_digest_size[hash.algo]))
		return 0;

	return -HASH_FAILURE;
}
