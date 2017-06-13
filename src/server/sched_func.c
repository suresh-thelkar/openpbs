/*
 * Copyright (C) 1994-2017 Altair Engineering, Inc.
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
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * The PBS Pro software is licensed under the terms of the GNU Affero General
 * Public License agreement ("AGPL"), except where a separate commercial license
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and distribute
 * them - whether embedded or bundled with other software - under a commercial
 * license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

/**
 * @file    sched_func.c
 *
 *@brief
 * 		sched_func.c - various functions dealing with schedulers
 *
 */
#include <errno.h>
#include <string.h>
#include "scheduler.h"
#include "log.h"
#include "pbs_ifl.h"
#include "pbs_db.h"
#include <memory.h>
#ifndef PBS_MOM
extern pbs_db_conn_t	*svr_db_conn;
#endif


/**
 * @brief
 * 		sched_alloc - allocate space for a pbs_sched structure and initialize
 *		attributes to "unset"
 *
 * @param[in]	sched_name	- scheduler  name
 *
 * @return	pbs_sched *
 * @retval	null	- space not available.
 */
pbs_sched *
sched_alloc(char *sched_name) {
	int i;
	pbs_sched *psched;

	psched = (pbs_sched *) malloc(sizeof(pbs_sched));

	if (psched == (pbs_sched *) 0) {
		log_err(errno, "sched_alloc", "no memory");
		return NULL;
	}
	(void) memset((char *) psched, (int) 0, (size_t) sizeof(pbs_sched));
	CLEAR_LINK(psched->sc_link);
	strncpy(psched->sc_name, sched_name, PBS_MAXSCHEDNAME);
	psched->sc_name[PBS_MAXSCHEDNAME] = '\0';

	append_link(&svr_allscheds, &psched->sc_link, psched);

	/* set the working attributes to "unspecified" */

	for (i = 0; i < (int) SCHED_ATR_LAST; i++) {
		clear_attr(&psched->sch_attr[i], &sched_attr_def[i]);
	}

	return (psched);
}

/**
 * @brief find a scheduler
 *
 * @param[in]	sched_name - scheduler name
 *
 * @return	pbs_sched *
 */

pbs_sched * find_scheduler(char *sched_name) {
	char *pc;
	pbs_sched *psched;

	if (!strcmp(sched_name, "1") || !strcmp(sched_name,"sched"))
		return dflt_scheduler;
	psched = (pbs_sched *) GET_NEXT(svr_allscheds);
	while (psched != (pbs_sched *) 0) {
		if (strcmp(sched_name, psched->sc_name) == 0)
			break;
		psched = (pbs_sched *) GET_NEXT(psched->sc_link);
	}
	return (psched);
}

/**
 * @brief free sched structure
 *
 * @param[in]	psched	- The pointer to the sched to free
 *
 */
void
sched_free(pbs_sched *psched)
{
	int		 i;
	attribute	*pattr;
	attribute_def	*pdef;

	/* remove any malloc working attribute space */

	for (i=0; i < (int)SCHED_ATR_LAST; i++) {
		pdef  = &sched_attr_def[i];
		pattr = &psched->sch_attr[i];

		pdef->at_free(pattr);
	}

	/* now free the main structure */
	delete_link(&psched->sc_link);
	(void)free((char*)psched);
}

/**
 * @brief - purge scheduler from system
 *
 * @param[in]	psched	- The pointer to the delete to delete
 *
 * @return	error code
 * @retval	0	- scheduler purged or scheduler not valid
 * @retval	PBSE_OBJBUSY	- scheduler deletion not allowed
 */
int
sched_delete(pbs_sched *psched)
{
	pbs_db_obj_info_t   obj;
	pbs_db_sched_info_t   dbsched;
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;

	if (psched == NULL)
		return (0);

	//TODO check for scheduler activity and return PBSE_OBJBUSY
	/* delete scheduler from database */
	strcpy(dbsched.sched_name, psched->sc_name);
	obj.pbs_db_obj_type = PBS_DB_SCHED;
	obj.pbs_db_un.pbs_db_sched = &dbsched;
	if (pbs_db_delete_obj(conn, &obj) != 0) {
		(void)sprintf(log_buffer,
			"delete of scheduler %s from datastore failed",
			psched->sc_name);
		log_err(errno, "sched_delete", log_buffer);
	}
	sched_free(psched);

	return (0);
}

