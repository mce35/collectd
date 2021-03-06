/**
 * collectd - src/nfs.c
 * Copyright (C) 2005,2006  Jason Pepas
 * Copyright (C) 2012,2013  Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Jason Pepas <cell at ices.utexas.edu>
 *   Florian octo Forster <octo at collectd.org>
 *   Cosmin Ioiart <cioiart at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

/*
see /proc/net/rpc/nfs
see http://www.missioncriticallinux.com/orph/NFS-Statistics

net x x x x
rpc_stat.netcnt         Not used; always zero.
rpc_stat.netudpcnt      Not used; always zero.
rpc_stat.nettcpcnt      Not used; always zero.
rpc_stat.nettcpconn     Not used; always zero.

rpc x x x
rpc_stat.rpccnt             The number of RPC calls.
rpc_stat.rpcretrans         The number of retransmitted RPC calls.
rpc_stat.rpcauthrefresh     The number of credential refreshes.

proc2 x x x...
proc3 x x x...

Procedure   NFS Version NFS Version 3
Number      Procedures  Procedures

0           null        null
1           getattr     getattr
2           setattr     setattr
3           root        lookup
4           lookup      access
5           readlink    readlink
6           read        read
7           wrcache     write
8           write       create
9           create      mkdir
10          remove      symlink
11          rename      mknod
12          link        remove
13          symlink     rmdir
14          mkdir       rename
15          rmdir       link
16          readdir     readdir
17          fsstat      readdirplus
18                      fsstat
19                      fsinfo
20                      pathconf
21                      commit
*/

static const char *nfs2_procedures_names[] =
{
	"null",
	"getattr",
	"setattr",
	"root",
	"lookup",
	"readlink",
	"read",
	"wrcache",
	"write",
	"create",
	"remove",
	"rename",
	"link",
	"symlink",
	"mkdir",
	"rmdir",
	"readdir",
	"fsstat"
};
static size_t nfs2_procedures_names_num = STATIC_ARRAY_SIZE (nfs2_procedures_names);

static const char *nfs3_procedures_names[] =
{
	"null",
	"getattr",
	"setattr",
	"lookup",
	"access",
	"readlink",
	"read",
	"write",
	"create",
	"mkdir",
	"symlink",
	"mknod",
	"remove",
	"rmdir",
	"rename",
	"link",
	"readdir",
	"readdirplus",
	"fsstat",
	"fsinfo",
	"pathconf",
	"commit"
};
static size_t nfs3_procedures_names_num = STATIC_ARRAY_SIZE (nfs3_procedures_names);

#if HAVE_LIBKSTAT
static const char *nfs4_procedures_names[] =
{
	"null",
	"compound",
	"reserved",
	"access",
	"close",
	"commit",
	"create",
	"delegpurge",
	"delegreturn",
	"getattr",
	"getfh",
	"link",
	"lock",
	"lockt",
	"locku",
	"lookup",
	"lookupp",
	"nverify",
	"open",
	"openattr",
	"open_confirm",
	"open_downgrade",
	"putfh",
	"putpubfh",
	"putrootfh",
	"read",
	"readdir",
	"readlink",
	"remove",
	"rename",
	"renew",
	"restorefh",
	"savefh",
	"secinfo",
	"setattr",
	"setclientid",
	"setclientid_confirm",
	"verify",
	"write"
};
static size_t nfs4_procedures_names_num = STATIC_ARRAY_SIZE (nfs4_procedures_names);
#endif

#if KERNEL_LINUX
static const char *nfs4_server40_procedures_names[] =
{
	"null",
	"compound",
	"reserved",
	"access",
	"close",
	"commit",
	"create",
	"delegpurge",
	"delegreturn",
	"getattr",
	"getfh",
	"link",
	"lock",
	"lockt",
	"locku",
	"lookup",
	"lookupp",
	"nverify",
	"open",
	"openattr",
	"open_confirm",
	"open_downgrade",
	"putfh",
	"putpubfh",
	"putrootfh",
	"read",
	"readdir",
	"readlink",
	"remove",
	"rename",
	"renew",
	"restorefh",
	"savefh",
	"secinfo",
	"setattr",
	"setclientid",
	"setcltid_confirm",
	"verify",
	"write",
	"release_lockowner"
};

static size_t nfs4_server40_procedures_names_num = STATIC_ARRAY_SIZE (nfs4_server40_procedures_names);

static const char *nfs4_server41_procedures_names[] =
{
	"backchannel_ctl",
	"bind_conn_to_session",	
	"exchange_id",
	"create_session",
	"destroy_session",
	"free_stateid",
	"get_dir_delegation",
	"getdeviceinfo",
	"getdevicelist",
	"layoutcommit",
	"layoutget",
	"layoutreturn",
	"secinfo_no_name",
	"sequence",
	"set_ssv",
	"test_stateid",
	"want_delegation",
	"destroy_clientid",
	"reclaim_complete",
};

static size_t nfs4_server41_procedures_names_num = STATIC_ARRAY_SIZE (nfs4_server41_procedures_names);

#define NFS4_SERVER40_NUM_PROC ( \
	STATIC_ARRAY_SIZE (nfs4_server40_procedures_names) )

#define NFS4_SERVER41_NUM_PROC ( \
	STATIC_ARRAY_SIZE (nfs4_server40_procedures_names) + \
	STATIC_ARRAY_SIZE (nfs4_server41_procedures_names) )

#define NFS4_SERVER_MAX_PROC (NFS4_SERVER41_NUM_PROC)

static const char *nfs4_client40_procedures_names[] =
{
	"null",
	"read",
	"write",
	"commit",
	"open",
	"open_confirm",
	"open_noattr",
	"open_downgrade",
	"close",
	"setattr",
	"fsinfo",
	"renew",
	"setclientid",
	"setclientid_confirm",
	"lock",
	"lockt",
	"locku",
	"access",
	"getattr",
	"lookup",
	"lookupp",
	"remove",
	"rename",
	"link",
	"symlink",
	"create",
	"pathconf",
	"statfs",
	"readlink",
	"readdir",
	"server_caps",
	"delegreturn",
	"getacl",
	"setacl",
	"fs_locations",		/* |35| 2.6.18 */
	"release_lockowner",	/* |42| 2.6.36 */
	"secinfo",		/* |46| 2.6.39 */
	"fsid_present"		/* |54| 3.13 */
};

static const char *nfs4_client41_procedures_names[] =
{
	"exchange_id",		/* |40| 2.6.30 */
	"create_session",	/* |40| 2.6.30 */
	"destroy_session",	/* |40| 2.6.30 */
	"sequence",		/* |40| 2.6.30 */
	"get_lease_time",	/* |40| 2.6.30 */
	"reclaim_complete",	/* |41| 2.6.33 */
	"layoutget",		/* |44| 2.6.37 */
	"getdeviceinfo",	/* |44| 2.6.37 */
	"layoutcommit",		/* |46| 2.6.39 */
	"layoutreturn",		/* |47| 3.0 */
	"secinfo_no_name",	/* |51| 3.1 */
	"test_stateid",		/* |51| 3.1 */
	"free_stateid",		/* |51| 3.1 */
	"getdevicelist",	/* |51| 3.1 */
	"bind_conn_to_session",	/* |53| 3.5 */
	"destroy_clientid"	/* |53| 3.5 */
};

#define NFS4_CLIENT40_NUM_PROC ( \
	STATIC_ARRAY_SIZE (nfs4_client40_procedures_names) )

#define NFS4_CLIENT41_NUM_PROC ( \
	STATIC_ARRAY_SIZE (nfs4_client40_procedures_names) + \
	STATIC_ARRAY_SIZE (nfs4_client41_procedures_names) )

#define NFS4_CLIENT_MAX_PROC (NFS4_CLIENT41_NUM_PROC)

#endif

#if HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
static kstat_t *nfs2_ksp_client;
static kstat_t *nfs2_ksp_server;
static kstat_t *nfs3_ksp_client;
static kstat_t *nfs3_ksp_server;
static kstat_t *nfs4_ksp_client;
static kstat_t *nfs4_ksp_server;
#endif

#if KERNEL_LINUX
static int nfs_init (void)
{
	return (0);
}
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static int nfs_init (void)
{
	kstat_t *ksp_chain = NULL;

	nfs2_ksp_client = NULL;
	nfs2_ksp_server = NULL;
	nfs3_ksp_client = NULL;
	nfs3_ksp_server = NULL;
	nfs4_ksp_client = NULL;
	nfs4_ksp_server = NULL;

	if (kc == NULL)
		return (-1);

	for (ksp_chain = kc->kc_chain; ksp_chain != NULL;
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_module, "nfs", 3) != 0)
			continue;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v2", 13) == 0)
			nfs2_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v3", 13) == 0)
			nfs3_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v4", 13) == 0)
			nfs4_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v2", 12) == 0)
			nfs2_ksp_client = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v3", 12) == 0)
			nfs3_ksp_client = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v4", 12) == 0)
			nfs4_ksp_client = ksp_chain;
	}

	return (0);
} /* int nfs_init */
#endif

static void nfs_procedures_submit (const char *plugin_instance,
		const char **type_instances,
		value_t *values, size_t values_num)
{
	value_list_t vl = VALUE_LIST_INIT;
	size_t i;

	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "nfs", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "nfs_procedure", sizeof (vl.type));

	for (i = 0; i < values_num; i++)
	{
		vl.values = values + i;
		sstrncpy (vl.type_instance, type_instances[i],
				sizeof (vl.type_instance));
		plugin_dispatch_values (&vl);
	}
} /* void nfs_procedures_submit */

#if KERNEL_LINUX
static void nfs_submit_fields (int nfs_version, const char *instance, 
		char **fields, size_t fields_num, const char **proc_names)
{
	char plugin_instance[DATA_MAX_NAME_LEN];
	value_t values[fields_num];
	size_t i;

	ssnprintf (plugin_instance, sizeof (plugin_instance), "v%i%s",
			nfs_version, instance);

	for (i = 0; i < fields_num; i++)
		(void) parse_value (fields[i], &values[i], DS_TYPE_DERIVE);

	nfs_procedures_submit (plugin_instance, proc_names, values,
			fields_num);
}

static int nfs_submit_fields_safe (int nfs_version, const char *instance,
		char **fields, size_t fields_num,
		const char **proc_names, size_t proc_names_num)
{
	if (fields_num != proc_names_num)
	{
		WARNING ("nfs plugin: Wrong number of fields for "
				"NFSv%i %s statistics. Expected %zu, got %zu.",
				nfs_version, instance,
				proc_names_num, fields_num);
		return (EINVAL);
	}

	nfs_submit_fields (nfs_version, instance, fields, fields_num, 
			proc_names);

	return (0);
}

static int nfs_submit_nfs4_server (const char *instance, char **fields, 
		size_t fields_num)
{
	static int suppress_warning = 0;

	if (fields_num != NFS4_SERVER40_NUM_PROC &&
		fields_num != NFS4_SERVER41_NUM_PROC) 
	{
		if (!suppress_warning)
		{
			WARNING ("nfs plugin: Unexpected number of fields for "
					"NFSv4 %s statistics: %zu. ",
					instance, fields_num);
		}

		if (fields_num > NFS4_SERVER_MAX_PROC)
		{
			fields_num = NFS4_SERVER_MAX_PROC;
			suppress_warning = 1;
		}
		else
		{
			return (EINVAL);
		}
	}

        nfs_submit_fields (4, instance, fields, 
			nfs4_server40_procedures_names_num,
			nfs4_server40_procedures_names);

	if (fields_num >= NFS4_SERVER41_NUM_PROC)
	{
		fields += nfs4_server40_procedures_names_num;

	        nfs_submit_fields (4, instance, fields, 
				nfs4_server41_procedures_names_num, 
				nfs4_server41_procedures_names);
	}

	return (0);
}

static int nfs_submit_nfs4_client (const char *instance, char **fields, 
		size_t fields_num)
{
	size_t proc40_names_num, proc41_names_num;

	static int suppress_warning = 0;

	switch (fields_num)
	{
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
			/* 4.0-only configuration */
			proc40_names_num = fields_num;
			break;
		case 40:
		case 41:
			proc40_names_num = 35;
			break;
		case 42:
		case 44:
		case 46:
		case 47:
		case 51:
		case 53:
			proc40_names_num = 36;
			break;
		case 54:
			proc40_names_num = 37;
			break;
		default:
			if (!suppress_warning)
			{
				WARNING ("nfs plugin: Unexpected number of "
						"fields for NFSv4 %s "
						"statistics: %zu. ",
						instance, fields_num);
			}

			if (fields_num > 34)
			{
				/* safe fallback to basic nfs40 procedures */
				fields_num = 34;
				proc40_names_num = 34;

				suppress_warning = 1;
			}
			else
			{
				return (EINVAL);
			}
	}

	nfs_submit_fields (4, instance, fields, proc40_names_num,
			nfs4_client40_procedures_names);

	if (fields_num > proc40_names_num)
	{
		proc41_names_num = fields_num - proc40_names_num;
		fields += proc40_names_num;

		nfs_submit_fields (4, instance, fields,proc41_names_num,
				 nfs4_client41_procedures_names);
	}

	return (0);
}

static void nfs_read_linux (FILE *fh, char *inst)
{
	char buffer[1024];

	char *fields[64];
	int fields_num = 0;

	if (fh == NULL)
		return;

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		fields_num = strsplit (buffer,
				fields, STATIC_ARRAY_SIZE (fields));

		if (fields_num < 3)
			continue;

		if (strcmp (fields[0], "proc2") == 0)
		{
			nfs_submit_fields_safe (/* version = */ 2, inst,
					fields + 2, (size_t) (fields_num - 2),
					nfs2_procedures_names,
					nfs2_procedures_names_num);
		}
		else if (strncmp (fields[0], "proc3", 5) == 0)
		{
			nfs_submit_fields_safe (/* version = */ 3, inst,
					fields + 2, (size_t) (fields_num - 2),
					nfs3_procedures_names,
					nfs3_procedures_names_num);
		}
		else if (strcmp (fields[0], "proc4ops") == 0)
		{
			if (inst[0] == 's')
				nfs_submit_nfs4_server (inst, fields + 2, 
						(size_t) (fields_num - 2));
		}
		else if (strcmp (fields[0], "proc4") == 0)
		{
			if (inst[0] == 'c')
				nfs_submit_nfs4_client (inst, fields + 2,
						(size_t) (fields_num - 2));			
		}
	} /* while (fgets) */
} /* void nfs_read_linux */
#endif /* KERNEL_LINUX */

#if HAVE_LIBKSTAT
static int nfs_read_kstat (kstat_t *ksp, int nfs_version, char *inst,
		char const **proc_names, size_t proc_names_num)
{
	char plugin_instance[DATA_MAX_NAME_LEN];
	value_t values[proc_names_num];
	size_t i;

	if (ksp == NULL)
		return (EINVAL);

	ssnprintf (plugin_instance, sizeof (plugin_instance), "v%i%s",
			nfs_version, inst);

	kstat_read(kc, ksp, NULL);
	for (i = 0; i < proc_names_num; i++)
	{
		/* The name passed to kstat_data_lookup() doesn't have the
		 * "const" modifier, so we need to copy the name here. */
		char name[32];
		sstrncpy (name, proc_names[i], sizeof (name));

		values[i].counter = (derive_t) get_kstat_value (ksp, name);
	}

	nfs_procedures_submit (plugin_instance, proc_names, values,
			proc_names_num);
	return (0);
}
#endif

#if KERNEL_LINUX
static int nfs_read (void)
{
	FILE *fh;

	if ((fh = fopen ("/proc/net/rpc/nfs", "r")) != NULL)
	{
		nfs_read_linux (fh, "client");
		fclose (fh);
	}

	if ((fh = fopen ("/proc/net/rpc/nfsd", "r")) != NULL)
	{
		nfs_read_linux (fh, "server");
		fclose (fh);
	}

	return (0);
}
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static int nfs_read (void)
{
	nfs_read_kstat (nfs2_ksp_client, /* version = */ 2, "client",
			nfs2_procedures_names, nfs2_procedures_names_num);
	nfs_read_kstat (nfs2_ksp_server, /* version = */ 2, "server",
			nfs2_procedures_names, nfs2_procedures_names_num);
	nfs_read_kstat (nfs3_ksp_client, /* version = */ 3, "client",
			nfs3_procedures_names, nfs3_procedures_names_num);
	nfs_read_kstat (nfs3_ksp_server, /* version = */ 3, "server",
			nfs3_procedures_names, nfs3_procedures_names_num);
	nfs_read_kstat (nfs4_ksp_client, /* version = */ 4, "client",
			nfs4_procedures_names, nfs4_procedures_names_num);
	nfs_read_kstat (nfs4_ksp_server, /* version = */ 4, "server",
			nfs4_procedures_names, nfs4_procedures_names_num);

	return (0);
}
#endif /* HAVE_LIBKSTAT */

void module_register (void)
{
	plugin_register_init ("nfs", nfs_init);
	plugin_register_read ("nfs", nfs_read);
} /* void module_register */
