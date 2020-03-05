/*
 *
 * Intel Keystore Linux driver
 * Copyright (c) 2018, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/fdtable.h>
#include <linux/sched/mm.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/path.h>
#include <linux/dcache.h>

#include "keystore_client.h"
#include "keystore_mac.h"
#include "keystore_debug.h"

#ifdef CONFIG_APPLICATION_AUTH
#include "appauth/manifest_verify.h"
#endif
#define KERNEL_CLIENTS_ID			"+(!$(%@#%$$)*"

/**
 * Get the absolute path of current process in the filesystem. This is used
 * for client authentication purpose.
 *
 * Always use the return variable from this function, input variable 'buf'
 * may not contain the start of the path. In case if kernel was executing
 * a kernel thread then this function return NULL.
 *
 * @param input buf place holder for updating the path
 * @param input buflen avaialbe space
 *
 * @return path of current process, or NULL if it was kernel thread.
 */

static char *get_current_process_path(char *buf, int buflen)
{
	struct file *exe_file = NULL;
	char *result = NULL;
	struct mm_struct *mm = NULL;

	mm = get_task_mm(current);
	if (!mm) {
		ks_info(KBUILD_MODNAME ": %s error get_task_mm\n", __func__);
		goto out;
	}

	down_read(&mm->mmap_sem);
	exe_file = mm->exe_file;

	if (exe_file)
		path_get(&exe_file->f_path);

	up_read(&mm->mmap_sem);
	mmput(mm);
	if (exe_file) {
		result = d_path(&exe_file->f_path, buf, buflen);
		path_put(&exe_file->f_path);
	}
out:
	return result;
}

#ifdef CONFIG_APPLICATION_AUTH
int keystore_calc_clientid(u8 *client_id, const unsigned int client_id_size,
		int timeout, u16 caps)
#else
int keystore_calc_clientid(u8 *client_id, const unsigned int client_id_size)
#endif
{
	int res = 0;
	char *buf = NULL;
	char *f_path = NULL;
#ifdef CONFIG_APPLICATION_AUTH
	char *mbuf = NULL;
	int mrootlen = PATH_MAX + NAME_MAX +
		strlen(CONFIG_APPLICATION_AUTH_MANIFEST_ROOT) + 10;
#endif
	if (!client_id)
		return -EINVAL;

	/* alloc mem for pwd##app, use linux defined limits */
	buf = kmalloc(PATH_MAX + NAME_MAX, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
#ifdef CONFIG_APPLICATION_AUTH
	/* alloc mem for manifest path */
	mbuf = kmalloc(mrootlen, GFP_KERNEL);
	if (!mbuf)
		goto out_buf;
#endif

	/* clear the buf */
	memset(buf, 0, PATH_MAX + NAME_MAX);

	f_path = get_current_process_path(buf, (PATH_MAX + NAME_MAX));

	ks_debug(KBUILD_MODNAME ": %s KSM-Client ABS path: %s\n",
		 __func__, f_path);

	if (f_path && IS_ERR(f_path)) {
		/* error case, do not register */
		ks_err(KBUILD_MODNAME ": Cannot register with keystore - failed client auth\n");
		res = -EFAULT;
		goto out_buf;
	}

	/* f_path is NULL for PF_KTHREAD type */
	if (!f_path) {
		ks_info(KBUILD_MODNAME ": %s Kernel client - use default.\n",
			__func__);
		f_path = KERNEL_CLIENTS_ID;
	}

#ifdef CONFIG_APPLICATION_AUTH
	strcpy(mbuf, CONFIG_APPLICATION_AUTH_MANIFEST_ROOT);
	strcat(mbuf, "/");
	strcat(mbuf, f_path);
	strcat(mbuf, ".manifest");
	ks_info(KBUILD_MODNAME ": %s Verifying manifest: %s.\n",
		__func__, mbuf);

	res = verify_manifest_file(mbuf, timeout, caps);
	if (res) {
		/* error case, do not register */
		ks_err(KBUILD_MODNAME ": Cannot register with keystore - manifest verification failed (res=%d)\n",
				res);
		switch (-res) {
		case MALFORMED_MANIFEST:
			res = -EINVAL;
			ks_err(KBUILD_MODNAME ": -> Malformed manifest (check the compiler version)\n");
			break;
		case CERTIFICATE_FAILURE:
			res = -EKEYREJECTED;
			ks_err(KBUILD_MODNAME ": -> Invalid certificate in the manifest\n");
			break;
		case CERTIFICATE_EXPIRED:
			res = -EKEYEXPIRED;
			ks_err(KBUILD_MODNAME ": -> Certificate expired (check system date!)\n");
			break;
		case CAPS_FAILURE:
			res = -EKEYREJECTED;
			ks_err(KBUILD_MODNAME ": -> Capabilities do not match\n");
			break;
		case SIGNATURE_FAILURE:
			res = -EKEYREJECTED;
			ks_err(KBUILD_MODNAME ": -> Manifest signature verification failed\n");
			break;
		case EXE_NOT_FOUND:
			res = -ENOENT;
			ks_err(KBUILD_MODNAME ": -> The executable not listed in the manifest\n");
			break;
		case FILE_TOO_BIG:
			res = -EFBIG;
			ks_err(KBUILD_MODNAME ": -> File too big\n");
			break;
		case HASH_FAILURE:
			res = -EKEYREJECTED;
			ks_err(KBUILD_MODNAME ": -> Hash calculation failed (or file listed in the manifest is missing)\n");
			break;
		default:
			res = -EFAULT;
			break;
		}
		goto out_buf;
	}
#endif

	/* Clear the output buffer */
	memset(client_id, 0, sizeof(u8) * client_id_size);

	/* calculate sha2 on new cwd - this is new clientid! */
	keystore_sha256_block(f_path, strlen(f_path),
			      client_id,
			      client_id_size);

out_buf:
#ifdef CONFIG_APPLICATION_AUTH
	kfree(mbuf);
#endif
	kfree(buf);
	return res;
}
