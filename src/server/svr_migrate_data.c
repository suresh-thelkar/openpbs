/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

/**
 * @file    svr_migrate_data.c
 *
 * @brief
 * 		svr_migrate_data.c - Functions to migrate pbs server data from one
 * 		version of PBS to another, in cases where the server data structure
 * 		has undergone changes.
 *
 * Included public functions are:
 * 	svr_migrate_data()
 * 	svr_migrate_data_from_fs()
 * 	rm_migrated_files()
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <libutil.h>

#ifdef WIN32

#include <direct.h>
#include <windows.h>
#include <io.h>
#include "win.h"

#else	/* !WIN32 */
#include <dirent.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/time.h>

#endif 	/* WIN32 */

#include "libpbs.h"
#include "pbs_ifl.h"
#include "net_connect.h"
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "server.h"
#include "credential.h"
#include "ticket.h"
#include "batch_request.h"
#include "work_task.h"
#include "resv_node.h"
#include "job.h"
#include "queue.h"
#include "reservation.h"
#include "pbs_nodes.h"
#include "tracking.h"
#include "provision.h"
#include "svrfunc.h"
#include "acct.h"
#include "pbs_version.h"
#include "rpp.h"
#include "pbs_license.h"
#include "resource.h"
#include "hook.h"
#include "pbs_db.h"

#ifndef SIGKILL
/* there is some weid stuff in gcc include files signal.h & sys/params.h */
#include <signal.h>
#endif


/* global Data Items */
extern char *msg_startup3;
extern char *msg_daemonname;
extern char *msg_init_abt;
extern char *msg_init_queued;
extern char *msg_init_substate;
extern char *msg_err_noqueue;
extern char *msg_err_noqueue1;
extern char *msg_init_noqueues;
extern char *msg_init_noresvs;
extern char *msg_init_resvNOq;
extern char *msg_init_recovque;
extern char *msg_init_recovresv;
extern char *msg_init_expctq;
extern char *msg_init_nojobs;
extern char *msg_init_exptjobs;
extern char *msg_init_norerun;
extern char *msg_init_unkstate;
extern char *msg_init_baddb;
extern char *msg_init_chdir;
extern char *msg_init_badjob;
extern char *msg_script_open;
extern char *msg_unkresc;

extern char *path_svrdb;
extern char *path_priv;
extern char *path_jobs;
extern char *path_users;
extern char *path_rescdef;
extern char *path_spool;

extern char server_host[];
extern char server_name[];
extern struct server server;

char *path_queues;
char *path_nodes;
char *path_nodestate;
char *path_scheddb;
char *path_resvs;
char *path_svrdb_new;
char *path_scheddb_new;
/* private data */
static char *suffix_slash = "/";
static char *new_tag = ".new";
static void rm_migrated_files(char *dirname);

/* Private functions in this file */
extern int chk_save_file(char *filename);
extern void rm_files(char *dirname);

/* extern definitions to older FS based object recovery functions */
extern int sched_recov_fs(char *svrfile);
extern int svr_recov_fs(char *svrfile);
extern pbs_queue *que_recov_fs(char *filename);
extern job *job_recov_fs(char *filename);
extern void *job_or_resv_recov_fs(char *filename, int objtype);
extern char *build_path(char *parent, char *name, char *sufix);

#ifdef NAS /* localmod 005 */
extern int setup_nodes_fs(int preprocess);

extern int resv_save_db(resc_resv *presv, int updatetype);

extern int pbsd_init(int type);

int svr_migrate_data_from_fs();
#endif /* localmod 005 */
extern void init_server_attrs();

/**
 * @brief
 *		Top level function to migrate pbs server data from one
 * 		version of PBS to another, in cases where the server data structure
 * 		has undergone changes.
 * @par
 * 		When a database structure changes, there could be two types of change.
 *			a) Simple structure change, that can be effected by pbs_habitat
 *	   		before new server is started.
 *			b) More complex change, where pbs_habitat could make some changes,
 *	  		but pbs_server needs to load the data in old format and save in
 *	  		the new format.
 * @par
 * 		In general, as part of DB upgrade, our process should be as follows:
 * 			- Add schema changes to pbs_habitat file
 * 			- Start pbs_server with the updatedb switch
 * 			- pbs_server should check the existing schema version number
 * 			- Based on the existing version number, pbs_server will use older data-structures
 *   			to load data that has older semantics.
 * 			- pbs_server should then save the data using the "new" routines, after performing
 *   			any necessary semantical conversions. (sometimes a save with the new routines
 *   			would be enough).
 * 			- If the schema version is unknown, we cannot do an upgrade, return an error
 *
 * @return	Error code
 * @retval	0	: success
 * @retval	-1	: Failure
 *
 */
int
svr_migrate_data()
{
	int db_maj_ver, db_min_ver;
	int i;

	/* if serverdb file exists, do a FS->DB migration */
	if (chk_save_file(path_svrdb) == 0) {
		return (svr_migrate_data_from_fs());
	}

	/* if no fs serverdb exists then check db version */
	if (pbs_db_get_schema_version(svr_db_conn, &db_maj_ver, &db_min_ver) != 0) {
		log_err(-1, msg_daemonname, "Failed to get PBS datastore version");
		fprintf(stderr, "Failed to get the PBS datastore version\n");
		if (svr_db_conn->conn_db_err) {
			fprintf(stderr, "[%s]\n", (char *)svr_db_conn->conn_db_err);
			log_err(-1, msg_daemonname, svr_db_conn->conn_db_err);
		}
		return -1;
	}

	if (db_maj_ver == 1 && db_min_ver == 0) {
		/* upgrade to current version */
		/* read all data, including node data, and save all nodes again */
		if (pbsd_init(RECOV_WARM) != 0) {
			return -1;
		}

		/* loop through all the nodes and mark for update */
		for (i = 0; i < svr_totnodes; i++) {
			pbsndlist[i]->nd_modified = NODE_UPDATE_OTHERS;
		}

		if (save_nodes_db(0, NULL) != 0) {
			log_err(errno, "svr_migrate_data", "save_nodes_db failed!");
			return -1;
		}
		return 0;
	}
	if (db_maj_ver == 3 && db_min_ver == 0) {
		/* Do nothing, schema has already taken care by query */
		return 0;
	}

	/*
	 * if the code fall through here, we did not recognize the schema
	 * version, or we are not prepared to handle this version,
	 * upgrade from this version is not possible. Return an error with log
	 */
	snprintf(log_buffer, sizeof(log_buffer), "Cannot upgrade from PBS datastore version %d.%d",
		db_maj_ver, db_min_ver);
	log_err(-1, msg_daemonname, log_buffer);
	fprintf(stderr, "%s\n", log_buffer);

	return -1;
}

/**
 * @brief
 *		Function to migrate filesystem data to database.
 * 		Reads serverdb, scheddb, job files, node, nodestate, queue, resv information
 * 		from the filesystem and save them into the database. All the information is
 * 		recovered and saved into the database under a single database transaction,
 * 		so any failure rolls back all the updates to the database. If all the updates
 * 		to the database succeed, only then the respective files are deleted from the
 * 		filesystem, else no deletion takes place.
 *
 * @return	Error code
 * @retval	0	: success
 * @retval	-1	: Failure
 *
 */
int
svr_migrate_data_from_fs(void)
{
	int baselen;
	struct dirent *pdirent;
	DIR *dir;
	int had;
	char *job_suffix = JOB_FILE_SUFFIX;
	int job_suf_len = strlen(job_suffix);
	job *pjob = NULL;
	pbs_queue *pque;
	resc_resv *presv;
	char *psuffix;
	int rc;
	int recovered = 0;
	char    basen[MAXPATHLEN+1];
	char	scrfile[MAXPATHLEN+1];
	char	jobfile[MAXPATHLEN+1];
	char	origdir[MAXPATHLEN+1];
	int fd;
	struct stat stbuf;
	char *scrbuf = NULL;
	pbs_db_jobscr_info_t	jobscr;
	pbs_db_obj_info_t		obj;

	path_svrdb_new = build_path(path_priv, PBS_SERVERDB, new_tag);
	path_scheddb = build_path(path_priv, PBS_SCHEDDB, NULL);
	path_scheddb_new = build_path(path_priv, PBS_SCHEDDB, new_tag);
	path_queues = build_path(path_priv, PBS_QUEDIR, suffix_slash);
	path_resvs = build_path(path_priv, PBS_RESVDIR, suffix_slash);
	path_nodes = build_path(path_priv, NODE_DESCRIP, NULL);
	path_nodestate = build_path(path_priv, NODE_STATUS, NULL);

	/*    If not a "create" initialization, recover server db */
	/*    and sched db					  */
	if (chk_save_file(path_svrdb) != 0) {
		fprintf(stderr, "No serverdb found to update to datastore\n");
		return (0);
	}

	if (setup_resc(1) == -1) {
		fprintf(stderr, "%s\n", log_buffer);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	init_server_attrs();

	/* start a database transation for the whole recovery */
	if (pbs_db_begin_trx(svr_db_conn, 0, 0) != 0)
		return (-1);

	/* preprocess the nodes file to convert old properties to resources */
	if (setup_nodes_fs(1) == -1) {
		fprintf(stderr, "%s\n", log_buffer);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	/* Open the server database (save file) and read it in */
	if (svr_recov_fs(path_svrdb) == -1) {
		fprintf(stderr, "%s\n", msg_init_baddb);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	/* save server information to database now */
	if (svr_save_db(&server, SVR_SAVE_NEW) != 0) {
		fprintf(stderr, "Could not save server db\n");
		if (svr_db_conn->conn_db_err)
			fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	/* now do sched db */
	if (sched_recov_fs(path_scheddb) == -1) {
		fprintf(stderr, "Unable to recover scheddb\n");
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	if (sched_save_db(dflt_scheduler, SVR_SAVE_NEW) != 0) {
		fprintf(stderr, "Could not save scheduler db\n");
		if (svr_db_conn->conn_db_err)
			fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}
	set_sched_default(dflt_scheduler, 0);
	/* save current working dir before any chdirs */
	if (getcwd(origdir, MAXPATHLEN) == NULL) {
		fprintf(stderr, "getcwd failed\n");
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		return (-1);
	}

	if (chdir(path_queues) != 0) {
		fprintf(stderr, msg_init_chdir, path_queues);
		fprintf(stderr, "\n");
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}

	had = server.sv_qs.sv_numque;
	server.sv_qs.sv_numque = 0;
	dir = opendir(".");
	if (dir == NULL) {
		fprintf(stderr, "%s\n", msg_init_noqueues);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}
	while (errno = 0, (pdirent = readdir(dir)) != NULL) {
		if (chk_save_file(pdirent->d_name) == 0) {
			if ((pque = que_recov_fs(pdirent->d_name)) !=
				NULL) {
				/* que_recov increments sv_numque */
				fprintf(stderr, msg_init_recovque,
					pque->qu_qs.qu_name);
				fprintf(stderr, "\n");
				if (que_save_db(pque, QUE_SAVE_NEW) != 0) {
					fprintf(stderr,
						"Could not save queue info for queue %s\n",
						pque->qu_qs.qu_name);
					if (svr_db_conn->conn_db_err)
						fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
					(void) pbs_db_end_trx(svr_db_conn,
						PBS_DB_ROLLBACK);
					(void) closedir(dir);
					chdir(origdir);
					return (-1);
				}
			}
		}
	}
	if (errno != 0 && errno != ENOENT) {
		fprintf(stderr, "%s\n", msg_init_noqueues);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		(void) closedir(dir);
		chdir(origdir);
		return (-1);
	}
	(void) closedir(dir);
	if (had != server.sv_qs.sv_numque) {
		fprintf(stderr, msg_init_expctq, had, server.sv_qs.sv_numque);
		fprintf(stderr, "\n");
	}

	/* Open and read in node list if one exists */
	if (setup_nodes_fs(0) == -1) {
		fprintf(stderr, "%s\n", log_buffer);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}

	/*
	 * Recover reservations.
	 */
	if (chdir(path_resvs) != 0) {
		fprintf(stderr, msg_init_chdir, path_resvs);
		fprintf(stderr, "\n");
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}

	dir = opendir(".");
	if (dir == NULL) {
		fprintf(stderr, "%s\n", msg_init_noresvs);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}
	while (errno = 0, (pdirent = readdir(dir)) != NULL) {
		if (chk_save_file(pdirent->d_name) == 0) {
			presv = (resc_resv *)
				job_or_resv_recov_fs(pdirent->d_name,
				RESC_RESV_OBJECT);
			if (presv != NULL) {
				if (resv_save_db(presv, SAVERESV_NEW) != 0) {
					fprintf(stderr,
						"Could not save resv info for resv %s\n",
						presv->ri_qs.ri_resvID);
					if (svr_db_conn->conn_db_err)
						fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
					(void) pbs_db_end_trx(svr_db_conn,
						PBS_DB_ROLLBACK);
					(void) closedir(dir);
					chdir(origdir);
					return (-1);
				}
			}
		}
	}
	if (errno != 0 && errno != ENOENT) {
		fprintf(stderr, "%s\n", msg_init_noresvs);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		(void) closedir(dir);
		chdir(origdir);
		return (-1);
	}
	(void) closedir(dir);

	/*
	 *   Recover jobs
	 */
	if (chdir(path_jobs) != 0) {
		fprintf(stderr, msg_init_chdir, path_jobs);
		fprintf(stderr, "\n");
		chdir(origdir);
		return (-1);
	}

	server.sv_qs.sv_numjobs = 0;
	recovered = 0;
	dir = opendir(".");
	if (dir == NULL) {
		fprintf(stderr, "%s\n", msg_init_nojobs);
	} else {
		/* Now, for each job found ... */
		while (errno = 0,
			(pdirent = readdir(dir)) != NULL) {
			if (chk_save_file(pdirent->d_name) != 0)
				continue;

			/* recover the job */
			baselen = strlen(pdirent->d_name) - job_suf_len;
			psuffix = pdirent->d_name + baselen;
			if (strcmp(psuffix, job_suffix))
				continue;

			if ((pjob = job_recov_fs(pdirent->d_name)) == NULL) {
				(void)strcpy(basen, pdirent->d_name);
				psuffix = basen + baselen;
				(void)strcpy(psuffix, JOB_BAD_SUFFIX);
				(void)snprintf(log_buffer, sizeof(log_buffer), "moved bad file to %s",
					basen);
				log_event(PBSEVENT_SYSTEM,
					PBS_EVENTCLASS_SERVER, LOG_NOTICE,
					msg_daemonname, log_buffer);
				continue;
			}

			if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_SCRIPT) {
				/* load the job script file */
				strcpy(scrfile, path_jobs);
#ifndef WIN32
				/* under WIN32, there's already a prefixed '/' */
				(void) strcat(scrfile, "/");
#endif
				strcat(scrfile, pdirent->d_name);
				baselen = strlen(scrfile) - strlen(JOB_FILE_SUFFIX);
				scrfile[baselen] = 0; /* put null char */
				strcat(scrfile, JOB_SCRIPT_SUFFIX);
				rc = 1;
#ifdef WIN32
				if ((fd = open(scrfile, O_BINARY | O_RDONLY)) != -1)
#else
				if ((fd = open(scrfile, O_RDONLY)) != -1)
#endif
				{
					/* load the script */
					if (fstat(fd, &stbuf) == 0) {
						if ((scrbuf = malloc(stbuf.st_size + 1))) {
							if (read(fd, scrbuf, stbuf.st_size) == stbuf.st_size) {
								scrbuf[stbuf.st_size] = '\0'; /* null character */
								rc = 0; /* success loading */
							}
						}
					}
					close(fd);
				}

				if (rc != 0) {
					fprintf(stderr, "Could not recover script file for job %s\n", pjob->ji_qs.ji_jobid);
					(void) strcpy(basen, scrfile);
					psuffix = basen + strlen(scrfile) - strlen(JOB_SCRIPT_SUFFIX);
					(void) strcpy(psuffix, JOB_BAD_SUFFIX);

					(void) strcpy(jobfile, scrfile);
					psuffix = jobfile + strlen(jobfile) - strlen(JOB_SCRIPT_SUFFIX);
					(void) strcpy(psuffix, JOB_FILE_SUFFIX);
#ifdef WIN32
					if (MoveFileEx(jobfile, basen,
						MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
						errno = GetLastError();
						snprintf(log_buffer, sizeof(log_buffer), "MoveFileEx(%s, %s) failed!",
							jobfile, basen);
						log_err(errno, "script", log_buffer);

					}
					secure_file(basen, "Administrators",
						READS_MASK | WRITES_MASK | STANDARD_RIGHTS_REQUIRED);
#else
					if (rename(jobfile, basen) == -1) {
						snprintf(log_buffer, sizeof(log_buffer), "error renaming job file %s",
							jobfile);
						log_err(errno, "job_recov", log_buffer);
					}
#endif
					(void) snprintf(log_buffer, sizeof(log_buffer), "moved bad file to %s",
						basen);
					log_event(PBSEVENT_SYSTEM,
						PBS_EVENTCLASS_SERVER, LOG_NOTICE,
						msg_daemonname, log_buffer);
					free(scrbuf);
					scrbuf = NULL;
					continue;
				}
			}

			/* now save job first */
			if (job_save_db(pjob, SAVEJOB_NEW) != 0) {
				fprintf(stderr, "Could not save job info for jobid %s\n",
					pjob->ji_qs.ji_jobid);
				if (svr_db_conn->conn_db_err)
					fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
				(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
				(void) closedir(dir);
				chdir(origdir);
				free(scrbuf);
				return (-1);
			}

			if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_SCRIPT) {
				/* save job script */
				strcpy(jobscr.ji_jobid, pjob->ji_qs.ji_jobid);
				jobscr.script = scrbuf;
				obj.pbs_db_obj_type = PBS_DB_JOBSCR;
				obj.pbs_db_un.pbs_db_jobscr = &jobscr;
				if (pbs_db_save_obj(svr_db_conn, &obj, PBS_INSERT_DB) != 0) {
					fprintf(stderr, "Could not save job script for jobid %s\n",
						pjob->ji_qs.ji_jobid);
					if (svr_db_conn->conn_db_err)
						fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
					(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
					free(scrbuf);
					(void) closedir(dir);
					chdir(origdir);
					return (-1);
				}
				free(scrbuf);
				scrbuf = NULL;
			}

			recovered++;
		}
		if (errno != 0 && errno != ENOENT) {
			if (pjob)
				fprintf(stderr, "readdir error for jobid %s\n", pjob->ji_qs.ji_jobid);
			else
				fprintf(stderr, "readdir error\n");
			(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
			free(scrbuf);
			(void) closedir(dir);
			chdir(origdir);
			return (-1);
		}
		(void) closedir(dir);
		fprintf(stderr, msg_init_exptjobs, recovered);
		fprintf(stderr, "\n");
	}

	if (save_nodes_db(0, NULL) != 0) {
		fprintf(stderr, "Could not save nodes\n");
		if (svr_db_conn->conn_db_err)
			fprintf(stderr, "[%s]\n", (char*)svr_db_conn->conn_db_err);
		(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
		chdir(origdir);
		return (-1);
	}

	if (pbs_db_end_trx(svr_db_conn, PBS_DB_COMMIT) == 0) {
		rm_migrated_files(path_priv);
		chdir(origdir);
		return (0);
	}
	chdir(origdir);
	return -1;
}

/**
 * @brief
 *		Remove all the migrated files from the filesystem.
 *
 * @param[in]	dirname	-	remove the directory contents recursively.
 *
 * @par MT-safe:	No.
 */
static void
rm_migrated_files(char *dirname)
{
	DIR *dir;
	int i;
	struct stat stb;
	struct dirent *pdirt;
	char path[MAXPATHLEN + 1];
	char pathbd[MAXPATHLEN + 1];
	char *psuffix;
	int baselen;

	/* list of directories in which files are removed */
	static char *byebye[] = {
		"acl_groups",
		"acl_hosts",
		"acl_svr",
		"acl_users",
		"resvs",
		"queues",
		"svrdb",
		"scheddb",
		"jobs",
		NULL /* keep as last entry */
	};

	dir = opendir(dirname);
	if (dir == NULL)
		return;
	while (errno = 0, (pdirt = readdir(dir)) != NULL) {
		(void) strcpy(path, dirname);

#ifndef WIN32
		/* under WIN32, there's already a prefixed '/' */
		(void) strcat(path, "/");
#endif
		(void) strcat(path, pdirt->d_name);
		if (stat(path, &stb) != 0)
			continue;
		if (S_ISDIR(stb.st_mode)) {
			/* recurse through directories */
			for (i = 0; byebye[i]; ++i) {
				if (strcmp(pdirt->d_name, byebye[i]) == 0) {
					rm_migrated_files(path);
					/* remove the top level directory */
					if (strcmp(pdirt->d_name, "jobs")!= 0) {
#ifdef WIN32
						if (_rmdir(path) == -1)
#else
						if (rmdir(path) == -1)
#endif
						{
							strcpy(log_buffer,     "cannot rm dir ");
							strcat(log_buffer, path);
							log_err(errno, "rm_migrated_files", log_buffer);
							fprintf(stderr, "%s\n", log_buffer);
						}
					}
				}
			}
		} else {
			/* plain files */
			if (strcmp(pdirt->d_name, "license_file") == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, PBS_RESCDEF) == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, "server.lock") == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, "tracking") == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, "prov_tracking") == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, PBS_SVRLIVE) == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, "db_password") == 0) {
				continue;
			} else if (strcmp(pdirt->d_name, "db_user") == 0) {
				continue;
			} else {
				baselen = strlen(pdirt->d_name) - strlen(JOB_CRED_SUFFIX);
				psuffix = pdirt->d_name + baselen;
				if (strcmp(psuffix, JOB_CRED_SUFFIX) == 0)
					continue; /* dont del credential files */

				baselen = strlen(pdirt->d_name) - strlen(JOB_BAD_SUFFIX);
				psuffix = pdirt->d_name + baselen;
				if (strcmp(psuffix, JOB_BAD_SUFFIX) == 0)
					continue; /* dont del bad job files */

				baselen = strlen(pdirt->d_name) - strlen(JOB_SCRIPT_SUFFIX);
				psuffix = pdirt->d_name + baselen;
				if (strcmp(psuffix, JOB_SCRIPT_SUFFIX) == 0) {
					size_t pathbd_size = sizeof(pathbd);
					/* this is a script file, check if .bd file exists */
					baselen = strlen(path) - strlen(JOB_SCRIPT_SUFFIX);
					strncpy(pathbd, path, pathbd_size - 1);
					if (baselen < (pathbd_size - 1))
						/* put null char */
						pathbd[baselen] = '\0';
					strncat(pathbd, JOB_BAD_SUFFIX,
						pathbd_size - strlen(pathbd));
					if (access(pathbd, F_OK) == 0) {
						/* corresponding .bd file exists, so dont delete */
						continue;
					}
				}
			}

			if (unlink(path) == -1) {
				strcpy(log_buffer, "cannot unlink");
				strcat(log_buffer, path);
				log_err(errno, "rm_migrated_files", log_buffer);
				fprintf(stderr, "%s\n", log_buffer);
			}
		}
	}
	if (errno != 0 && errno != ENOENT) {
		strcpy(log_buffer,     "cannot read dir ");
		strcat(log_buffer, path);
		log_err(errno, "rm_migrated_files", log_buffer);
		fprintf(stderr, "%s\n", log_buffer);
		closedir(dir);
		return;
	}
	closedir(dir);
}
