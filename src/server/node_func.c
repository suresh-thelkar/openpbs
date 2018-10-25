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
 * @file	node_func.c
 * @brief
 *		node_func.c - various functions dealing with nodes, properties and
 *		 the following global variables:
 *	pbsnlist     -	the server's global node list
 *	svr_totnodes -	total number of pbshost entries
 *	initialize_pbsnode - Initialize a new pbs node structure
 *
 * Included functions are:
 *	find_nodebyname()   	-     given a node host name, search pbsndlist
 * 	save_characteristic() 	- save the the characteristics of the node along with
 *							the address of the node
 * 	chk_characteristic() 	-  check for changes to the node's set of
 *							characteristics and set appropriate flag bits in the "need_todo"
 *							location depending on which characteristics changed
 * 	status_nodeattrib() 	-    add status of each requested (or all) node-attribute
 *								to the status reply
 * 	initialize_pbsnode() 	-   performs node initialization on a new node
 * 	effective_node_delete() -  effectively deletes a node from the server's node
 *								list by setting the node's "deleted" bit
 * 	setup_notification() 	-   sets mechanism for notifying other hosts about a new
 *								host
 * 	process_host_name_part()- processes hostname part of a batch request into a
 *								prop structure, host's IP addresses into an array, and node
 *								node type (cluster/time-shared) into an int variable
 * 	save_nodes_db() 		-    		used to update the nodes file when certain changes
 *								occur to the server's internal nodes list
 *	free_prop_list()		-	For each element of a null terminated prop list call free
 *								to clean up any string buffer that hangs from the element.
 *	subnode_delete()		-	delete the specified subnode by marking it deleted
 *	remove_mom_from_vnodes()-	remove this Mom from the list of Moms for any vnode
 *	save_nodes_db_inner()	-	Static function to update all the nodes in the db
 *	init_prop()				-	allocate and initialize a prop struct
 *	create_subnode()		-	create a subnode entry and link to parent node
 *	setup_nodes_fs()		-	Read the file, "nodes", containing the list of properties for each node.
 *	setup_nodes()			-	Read the, "nodes" information from database
 *								containing the list of properties for each node.
 *	delete_a_subnode()		-	mark a (last) single subnode entry as deleted
 *	mod_node_ncpus()		-	when resources_available.ncpus changes, need to update the number of subnodes,
 *								 creating or deleting as required
 *	set_clear_target()		-	set or clear ATR_VFLAG_TARGET flag in a target resource "index"
 *								is the index into the node's attribute array.
 *	indirect_target_check()	-	called via a work task to (re)set ATR_VFLAG_TARGET
 *									in any resource which is the target of another indirect resource.
 *	fix_indirectness()		-	check if a member of a node's resource_available is becoming indirect or vice-versa.
 *	node_np_action()		-	action routine for a node's resources_available attribute
 *	node_pcpu_action()		-	action routine for node's pcpus (physical) resource
 *	mark_which_queues_have_nodes()	-	Mark the queue header for queues that have nodes associated with them.
 *	node_queue_action()		-	action routine for nodes when "queue" attribute set
 *	set_node_mom_port()		-	set an alternative port for the Mom on a node
 *	is_vnode_up()			-	check if vnode is up
 *	decode_Mom_list()		-	decode a comma string which specifies a list of Mom/host
 *								names into an attr of type ATR_TYPE_ARST
 *	record_node_topology()	-	remember the node topology information reported by a node's MoM
 *	remove_node_topology()	-	remove the node topology information for the given node name
 *	set_node_topology()		-	set the node topology attribute
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "pbs_db.h"
#ifndef WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <assert.h>
#include "pbs_ifl.h"
#include "libpbs.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "credential.h"
#include "server_limits.h"
#include "batch_request.h"
#include "server.h"
#include "resv_node.h"
#ifdef WIN32
#include "win.h"
#endif
#include "job.h"
#include "queue.h"
#include "reservation.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "pbs_error.h"
#include "log.h"
#include "rpp.h"
#include "work_task.h"
#include "net_connect.h"
#include "cmds.h"
#include "pbs_license.h"
#include "avltree.h"
#if !defined(H_ERRNO_DECLARED) && !defined(WIN32)
extern int h_errno;
#endif


/* Global Data */

extern int	 svr_quehasnodes;
extern int	 svr_totnodes;
extern char	*path_nodes_new;
extern char	*path_nodes;
extern char	*path_nodestate;
extern pbs_list_head svr_queues;
extern unsigned int pbs_mom_port;
extern unsigned int pbs_rm_port;
extern mominfo_time_t  mominfo_time;
extern char	*resc_in_err;
extern char	server_host[];
extern AVL_IX_DESC *node_tree;
extern int write_single_node_mom_attr(struct pbsnode *np);
static AVL_IX_DESC *momfqdn_tree = NULL;

extern struct python_interpreter_data  svr_interp_data;

#ifdef NAS /* localmod 005 */
/* External Functions Called */
extern int node_recov_db_raw(void *nd, pbs_list_head *phead);
extern int node_delete_db(struct pbsnode *pnode);
extern int write_single_node_state(struct pbsnode *np);
#endif /* localmod 005 */


static void	remove_node_topology(char *);

/**
 * @brief
 * 		find_nodebyname() - find a node host by its name
 * @param[in]	nodename	- node being searched
 *
 * @return	pbsnode
 * @retval	NULL	- failure
 */

struct pbsnode	  *find_nodebyname(nodename)
char *nodename;
{
	char		*pslash;

	if (nodename == NULL)
		return NULL;
	if (*nodename == '(')
		nodename++;	/* skip over leading paren */
	if ((pslash = strchr(nodename, (int)'/')) != NULL)
		*pslash = '\0';
	if (node_tree == NULL)
		return NULL;

	return ((struct pbsnode *) find_tree(node_tree, nodename));
}


/**
 * @brief
 * 		find_nodebyaddr() - find a node host by its addr
 * @param[in]	addr	- addr being searched
 *
 * @return	pbsnode
 * @retval	NULL	- failure
 */

struct pbsnode	  *find_nodebyaddr(addr)
pbs_net_t addr;
{
	int i, j;
	mom_svrinfo_t *psvrmom;

	for (i=0; i<svr_totnodes; i++) {
		psvrmom = (mom_svrinfo_t *)pbsndlist[i]->nd_moms[0]->mi_data;
		for (j = 0; psvrmom->msr_addrs[j]; j++) {
			if (addr == psvrmom->msr_addrs[j]) {
				return (pbsndlist[i]);
			}
		}
	}
	return NULL;
}


static struct pbsnode	*old_address = 0;			/*node in question */
static unsigned long	old_state = 0;				/*node's   state   */


/**
 * @brief
 * 		save_characteristic() -  save the characteristic values of the node along
 *			    with the address of the node
 * @param[in]	pnode	- the node to check
 *
 * @return	void
 */

void
save_characteristic(struct pbsnode *pnode)
{
	if (pnode == NULL)
		return;

	old_address = pnode;
	old_state =   pnode->nd_state;

}

/**
 * @brief
 * 		Check the value of the characteristics against
 *		that which was saved earlier.
 *
 * @param[in]	pnode	- the node to check
 * @param[out]	pneed_todo - 	gets appropriate bit(s) set depending on the
 * 				results of the check.
 *
 * @return int
 * @retval	-1  if parent address doesn't match saved parent address
 * @retval	0   if successful check.
 */

int
chk_characteristic(struct pbsnode *pnode, int *pneed_todo)
{
	unsigned long	tmp;
	int		i;
	int		deleted=0;

	if (pnode != old_address || pnode == NULL) {
		/*
		 **	didn't do save_characteristic() before
		 **	issuing chk_characteristic()
		 */

		old_address = NULL;
		return (-1);
	}
	pnode->nd_modified = 0; /* reset */

	tmp = pnode->nd_state;
	if (tmp != old_state) {
		if (tmp & INUSE_DELETED && !(old_state & INUSE_DELETED)) {
			*pneed_todo |= WRITE_NEW_NODESFILE; /*node being deleted*/
			pnode->nd_modified |= NODE_UPDATE_OTHERS;
			deleted = 1; /* no need to update other attributes */
		} else {
			if (tmp & INUSE_OFFLINE && !(old_state & INUSE_OFFLINE)) {
				*pneed_todo |= WRITENODE_STATE; /*marked offline */
				pnode->nd_modified |= NODE_UPDATE_STATE;
			}

			if (!(tmp & INUSE_OFFLINE) && old_state & INUSE_OFFLINE) {
				*pneed_todo |= WRITENODE_STATE; /*removed offline*/
				pnode->nd_modified |= NODE_UPDATE_STATE;
			}

			if (tmp & INUSE_OFFLINE_BY_MOM && !(old_state & INUSE_OFFLINE_BY_MOM)) {
				*pneed_todo |= WRITENODE_STATE; /*marked offline */
				pnode->nd_modified |= NODE_UPDATE_STATE;
			}

			if (!(tmp & INUSE_OFFLINE_BY_MOM) && old_state & INUSE_OFFLINE_BY_MOM) {
				*pneed_todo |= WRITENODE_STATE; /*removed offline*/
				pnode->nd_modified |= NODE_UPDATE_STATE;
			}
		}
	}

	if (!deleted) {
		if (pnode->nd_attr[ND_ATR_Comment].at_flags & ATR_VFLAG_MODIFY) {
			*pneed_todo |= WRITENODE_STATE;
			pnode->nd_modified |= NODE_UPDATE_COMMENT;
		}

		for (i = 0; i < ND_ATR_LAST; i++) {
			if ((i != ND_ATR_Comment && i != ND_ATR_state) &&
				(pnode->nd_attr[i].at_flags & ATR_VFLAG_MODIFY)) {
				*pneed_todo |= WRITE_NEW_NODESFILE;
				pnode->nd_modified |= NODE_UPDATE_OTHERS;
				break;
			}
		}
	}
	old_address = NULL;
	return  0;
}



/**
 * @brief
 * 	 	status_nodeattrib() - add status of each requested (or all) node-attribute to
 *			 the status reply.
 *		if a node-attribute is incorrectly specified, *bad is set to the node-attribute's ordinal position.
 * @see
 * 		status_node
 * @param[in,out]	pal	- the node to check
 * @param[in]	padef 	- 	the defined node attributes
 * @param[out]	pnode 	- 	no longer an attribute ptr
 * @param[in]	limit 	- 	number of array elements in padef
 * @param[in]	priv 	- 	requester's privilege
 * @param[in]	phead 	- 	heads list of svrattrl structs that hang
 * 							off the brp_attr member of the status sub
 * 							structure in the request's "reply area"
 * @param[in]	bad 	- 	if node-attribute error record it's list position here
 *
 * @return	int
 * @retval	0		- success
 * @retval	!= 0	- error
 */

int
status_nodeattrib(svrattrl *pal, attribute_def *padef, struct pbsnode *pnode, int limit, int priv, pbs_list_head *phead, int *bad)
{
	int   rc = 0;		/*return code, 0 == success*/
	int   index;
	int   nth;		/*tracks list position (ordinal tacker)   */




	priv &= ATR_DFLAG_RDACC;  		/* user-client privilege      */

	if (pal) {   /*caller has requested status on specific node-attributes*/
		nth = 0;
		while (pal) {
			++nth;
			index = find_attr(padef, pal->al_name, limit);
			if (index < 0) {
				*bad = nth;	/* name in this position not found */
				rc = PBSE_UNKNODEATR;
				break;
			}
			if ((padef+index)->at_flags & priv) {
				rc = (padef+index)->at_encode(&pnode->nd_attr[index],
					phead,
					(padef+index)->at_name, NULL,
					ATR_ENCODE_CLIENT, NULL);
				if (rc < 0) {
					rc = -rc;
					break;
				}
				rc = 0;
			}
			pal = (svrattrl *)GET_NEXT(pal->al_link);
		}

	} else {
		/*
		 **	non-specific request,
		 **	return all readable attributes
		 */
		for (index = 0; index < limit; index++) {
			if ((padef+index)->at_flags & priv) {
				rc = (padef+index)->at_encode(
					&pnode->nd_attr[index],
					phead, (padef+index)->at_name,
					NULL, ATR_ENCODE_CLIENT, NULL);
				if (rc < 0) {
					rc = -rc;
					break;
				}
				rc = 0;
			}
		}
	}

	return (rc);
}


/**
 * @brief
 * 		free_prop_list
 * 		For each element of a null terminated prop list call free
 * 		to clean up any string buffer that hangs from the element.
 *		After this, call free to remove the struct prop.
 *
 * @param[in,out]	prop 	- 	prop list
 *
 * @return	void
 */

void
free_prop_list(struct prop *prop)
{
	struct prop	*pp;

	while (prop) {
		pp = prop->next;
		free(prop->name);
		prop->name = NULL;
		free(prop);
		prop = pp;
	}
}

/**
 * @brief
 *		initialize_pbsnode - carries out initialization on a new
 *		pbs node.  The assumption is that all the parameters are valid.
 * @see
 * 		node_recov_db and create_pbs_node2
 * @param[out]	pnode 	- 	new pbs node
 * @param[in]	pname	- 	node name
 * @param[in]	ntype 	- 	time-shared or cluster
 *
 * @return	int
 * @retval	PBSE_NONE	- success
 */

int
initialize_pbsnode(struct pbsnode *pnode, char *pname, int ntype)
{
	int	      i;
	attribute    *pat1;
	attribute    *pat2;
	resource_def *prd;
	resource     *presc;

	pnode->nd_name    = pname;
	pnode->nd_ntype   = ntype;
	pnode->nd_nsn     = 0;
	pnode->nd_nsnfree = 0;
	pnode->nd_written = 0;
	pnode->nd_ncpus	  = 1;
	pnode->nd_psn     = NULL;
	pnode->nd_hostname= NULL;
	pnode->nd_state = INUSE_UNKNOWN | INUSE_DOWN;
	pnode->nd_resvp   = NULL;
	pnode->nd_pque	  = NULL;
	pnode->nd_nummoms = 0;
	pnode->nd_modified = 0;
	pnode->nd_moms    = (struct mominfo **)calloc(1, sizeof(struct mominfo *));
	if (pnode->nd_moms == NULL)
		return (PBSE_SYSTEM);
	pnode->nd_nummslots = 1;

	/* first, clear the attributes */

	for (i=0; i<(int)ND_ATR_LAST; i++)
		clear_attr(&pnode->nd_attr[i], &node_attr_def[i]);

	/* then, setup certain attributes */

	pnode->nd_attr[(int)ND_ATR_state].at_val.at_long = pnode->nd_state;
	pnode->nd_attr[(int)ND_ATR_state].at_flags = ATR_VFLAG_SET;

	pnode->nd_attr[(int)ND_ATR_ntype].at_val.at_short = pnode->nd_ntype;
	pnode->nd_attr[(int)ND_ATR_ntype].at_flags = ATR_VFLAG_SET;

	pnode->nd_attr[(int)ND_ATR_jobs].at_val.at_jinfo = pnode;
	pnode->nd_attr[(int)ND_ATR_jobs].at_flags = ATR_VFLAG_SET;

	pnode->nd_attr[(int)ND_ATR_resvs].at_val.at_jinfo = pnode;
	pnode->nd_attr[(int)ND_ATR_resvs].at_flags = ATR_VFLAG_SET;

	pnode->nd_attr[(int)ND_ATR_ResvEnable].at_val.at_long = 1;
	pnode->nd_attr[(int)ND_ATR_ResvEnable].at_flags =
		ATR_VFLAG_SET|ATR_VFLAG_DEFLT;

	pnode->nd_attr[(int)ND_ATR_version].at_val.at_str = strdup("unavailable");
	pnode->nd_attr[(int)ND_ATR_version].at_flags =
		ATR_VFLAG_SET|ATR_VFLAG_DEFLT;

	pnode->nd_attr[(int)ND_ATR_Sharing].at_val.at_long = (long)VNS_DFLT_SHARED;
	pnode->nd_attr[(int)ND_ATR_Sharing].at_flags =
		ATR_VFLAG_SET|ATR_VFLAG_DEFLT;

	pat1 = &pnode->nd_attr[(int)ND_ATR_ResourceAvail];
	pat2 = &pnode->nd_attr[(int)ND_ATR_ResourceAssn];

	prd  = find_resc_def(svr_resc_def, "arch", svr_resc_size);
	assert(prd != NULL);
	(void)add_resource_entry(pat1, prd);

	prd  = find_resc_def(svr_resc_def, "mem", svr_resc_size);
	assert(prd != NULL);
	(void)add_resource_entry(pat1, prd);

	prd  = find_resc_def(svr_resc_def, "ncpus", svr_resc_size);
	assert(prd != NULL);
	(void)add_resource_entry(pat1, prd);

	/* add to resources_assigned any resource with ATR_DFLAG_FNASSN */
	/* or  ATR_DFLAG_ANASSN set in the resource definition          */

	for (prd = svr_resc_def; prd; prd = prd->rs_next) {
		if (prd->rs_flags & (ATR_DFLAG_FNASSN |  ATR_DFLAG_ANASSN)) {
			presc = add_resource_entry(pat2, prd);
			presc->rs_value.at_flags = ATR_VFLAG_SET |
				ATR_VFLAG_MODCACHE;
		}
	}

	/* clear the modify flags */

	for (i=0; i<(int)ND_ATR_LAST; i++)
		pnode->nd_attr[i].at_flags &= ~ATR_VFLAG_MODIFY;
	return (PBSE_NONE);
}

/**
 * @brief
 * 		subnode_delete - delete the specified subnode
 *		by marking it deleted
 *
 * @see
 * 		effective_node_delete and delete_a_subnode
 *
 * @param[in]	psubn	-	ncpus on a vnode
 *
 * @return	void
 */

static void
subnode_delete(struct pbssubn *psubn)
{
	struct jobinfo	*jip, *jipt;

	for (jip = psubn->jobs; jip; jip = jipt) {
		jipt = jip->next;
		free(jip);
	}
	psubn->jobs  = NULL;
	psubn->next  = NULL;
	psubn->inuse = INUSE_DELETED;
	free(psubn);
}
/**
 * @brief
 * 		Remove the vnode from the list of vnodes of a mom.
 * @see
 * 		effective_node_delete and effective_node_delete
 *
 * @param[in]	pnode	- Vnode structure
 *
 * @return	void
 */
static void
remove_vnode_from_moms(struct pbsnode *pnode)
{
	int imom;
	int ivnd;

	mom_svrinfo_t *psvrm;

	for (imom = 0; imom < pnode->nd_nummoms; ++imom) {
		psvrm = pnode->nd_moms[imom]->mi_data;
		for (ivnd = 0; ivnd < psvrm->msr_numvnds; ++ivnd) {
			if (psvrm->msr_children[ivnd] == pnode) {
				/* move list down to remove this entry */
				while (ivnd <  psvrm->msr_numvnds - 1) {
					psvrm->msr_children[ivnd] =
						psvrm->msr_children[ivnd+1];
					++ivnd;
				}
				psvrm->msr_children[ivnd] = NULL;
				--psvrm->msr_numvnds;
				break;	/* done with this Mom */
			}
		}
	}
}

/**
 * @brief
 * 		remove_mom_from_vnodes - remove this Mom from the list of Moms for any
 *		vnode (after the natural vnode) and removed from the Mom attribute
 * @see
 * 		effective_node_delete
 * @param[in]	pmom	-	Mom which needs to be removed
 *
 * @return	void
 */
static void
remove_mom_from_vnodes(mominfo_t *pmom)
{
	int imom;
	int ivnd;
	struct pbsnode *pnode;
	mom_svrinfo_t  *psvrmom;
	attribute       tmomattr;

	psvrmom = pmom->mi_data;
	if (psvrmom->msr_numvnds == 1)
		return;

	/* setup temp "Mom" attribute with the host name to remove */
	clear_attr(&tmomattr, &node_attr_def[(int)ND_ATR_Mom]);
	(void)node_attr_def[(int)ND_ATR_Mom].at_decode(&tmomattr,
		ATTR_NODE_Mom,
		NULL,
		pmom->mi_host);

	/* start index "invd" at 1 to skip natural vnode for this Mom */
	for (ivnd=1; ivnd<psvrmom->msr_numvnds; ++ivnd) {
		pnode = psvrmom->msr_children[ivnd];
		for (imom = 0; imom < pnode->nd_nummoms; ++imom) {
			if (pnode->nd_moms[imom] == pmom) {
				/* move list down to remove this mom */
				while (imom < pnode->nd_nummoms - 1) {
					pnode->nd_moms[imom] =
						pnode->nd_moms[imom+1];
					++imom;
				}
				pnode->nd_moms[imom] = NULL;
				--pnode->nd_nummoms;
				pnode->nd_modified = NODE_UPDATE_OTHERS; /* since we modified nd_nummoms, flag for save */
				/* remove (decr) Mom host from Mom attrbute */
				(void)node_attr_def[(int)ND_ATR_Mom].at_set(
					&pnode->nd_attr[(int)ND_ATR_Mom],
					&tmomattr, DECR);

				break;
			}
		}

	}
	node_attr_def[(int)ND_ATR_Mom].at_free(&tmomattr);
}

/**
 * @brief Free a pbsnode structure
 *
 * @param[in] pnode - ptr to pnode to delete
 *
 * @par MT-safe: No
 **/
void
free_pnode(struct pbsnode *pnode)
{
	if (pnode) {
		(void)free(pnode->nd_name);
		(void)free(pnode->nd_hostname);
		(void)free(pnode->nd_moms);
		(void)free(pnode); /* delete the pnode from memory */
	}
}

/**
 * @brief
 * 		effective_node_delete - physically delete a vnode, including its
 *		pbsnode structure, associated attribute, etc and free the licenses.
 *		This should not be called if the vnode has jobs running on it.
 *
 * @param[in,out]	pnode	-	vnode structure
 *
 * @return	void
 */
void
effective_node_delete(struct pbsnode *pnode)
{
	int		 i, j;
	struct pbssubn  *psubn;
	struct pbssubn  *pnxt;
	mom_svrinfo_t	*psvrmom;
	int		 iht;
	int		 socket_released = 0;

	psubn = pnode->nd_psn;
	while (psubn) {
		pnxt = psubn->next;
		subnode_delete(psubn);
		psubn = pnxt;
	}

	/* release license if node is locked */
	if (pnode->nd_attr[(int)ND_ATR_License].at_val.at_char == ND_LIC_TYPE_locked &&
			(strcmp(pnode->nd_name, pnode->nd_hostname) == 0)) {
		sockets_release(
			pnode->nd_attr[(int)ND_ATR_LicenseInfo].at_val.at_long);
		socket_released = 1;
	}

        /* free attributes */

	for (i=0; i<ND_ATR_LAST; i++) {
		node_attr_def[i].at_free(&pnode->nd_attr[i]);
	}

	if (pnode->nd_nummoms > 1) {
		/* unlink from mominfo for all parent Moms */
		remove_vnode_from_moms(pnode);
	} else if (pnode->nd_nummoms == 1) {
		psvrmom = (mom_svrinfo_t *)(pnode->nd_moms[0]->mi_data);
		if (psvrmom->msr_children[0] == pnode) {
			/*
			 * This is the "natural" vnode for a Mom
			 * must mean for the Mom to go away also
			 * first remove from any vnode pool
			 */
			remove_mom_from_pool(pnode->nd_moms[0]);

			/* Then remove this MoM from any other vnode she manages */
			remove_mom_from_vnodes(pnode->nd_moms[0]);

			/* then delete the Mom */
			for (j=0; psvrmom->msr_addrs[j]; j++) {
				u_long	ipaddr = psvrmom->msr_addrs[j];
				if (ipaddr)
					delete_iplist_element(pbs_iplist, ipaddr);
			}
			delete_svrmom_entry(pnode->nd_moms[0]);
			pnode->nd_moms[0] = NULL; /* since we deleted the mom */
		} else {
			/* unlink from mominfo of parent Moms */
			remove_vnode_from_moms(pnode);
		}
	}

	/* set the nd_moms to NULL before calling save */
	if (pnode->nd_moms)
		free(pnode->nd_moms);
	pnode->nd_moms = NULL;

	DBPRT(("Deleting node %s from database\n", pnode->nd_name))
	node_delete_db(pnode);

	remove_node_topology(pnode->nd_name);

	/* delete the node from the node tree as well as the node array */
	if (node_tree != NULL) {
		tree_add_del(node_tree, pnode->nd_name, NULL, TREE_OP_DEL);
	}

	for (iht=pnode->nd_arr_index + 1; iht < svr_totnodes; iht++) {
		pbsndlist[iht - 1] = pbsndlist[iht];
		/* adjust the arr_index since we are coalescing elements */
		pbsndlist[iht - 1]->nd_arr_index--;
	}
	svr_totnodes--;
	free_pnode(pnode);
	if (socket_released)
		license_more_nodes();
}

/**
 * @brief
 *	setup_notification -  Sets up the  mechanism for notifying
 *	other members of the server's node pool that a new node was added
 *	manually via qmgr.  Actual notification occurs some time later through
 *	the ping_nodes mechanism.
 *	The IS_CLUSTER_ADDRS2 message is only sent to the existing Moms.
 * @see
 * 		mgr_node_create
 *
 * @return	void
 */
void
setup_notification()
{
	int	i;
	int	nmom;

	for (i=0; i<svr_totnodes; i++) {
		if (pbsndlist[i]->nd_state & INUSE_DELETED)
			continue;

		set_vnode_state(pbsndlist[i], INUSE_DOWN, Nd_State_Or);
		pbsndlist[i]->nd_attr[(int)ND_ATR_state].at_flags |= ATR_VFLAG_MODCACHE;
		for (nmom = 0; nmom < pbsndlist[i]->nd_nummoms; ++nmom) {
			((mom_svrinfo_t *)(pbsndlist[i]->nd_moms[nmom]->mi_data))->msr_state |= INUSE_NEED_ADDRS;
			((mom_svrinfo_t *)(pbsndlist[i]->nd_moms[nmom]->mi_data))->msr_timepinged = 0;
		}
	}
}


/**
 * @brief
 * 		process_host_name_part - actually processes the node name part of the form
 *		node[:ts|:gl]
 *		checks the node type and rechecks agaist the ntype attribute which
 *		may be in the attribute list given by plist
 * @see
 * 		create_pbs_node2
 *
 * @param[in]		objname	-	node to be's name
 * @param[out]		plist	-	THINGS RETURNED
 * @param[out]		pname	-	node name w/o any :ts
 * @param[out]		ntype	-	node type, time-shared, not
 *
 * @return	int
 * @retval	0	- success
 */
int
process_host_name_part(char *objname, svrattrl *plist, char **pname, int *ntype)
{
	attribute	 lattr;
	char		*pnodename;     /*caller supplied node name */
	int		len;


	len = strlen(objname);
	if (len == 0)
		return  (PBSE_UNKNODE);

	pnodename = strdup(objname);

	if (pnodename == NULL)
		return  (PBSE_SYSTEM);

	*ntype = NTYPE_PBS;
	if (len >= 3) {
		if  (!strcmp(&pnodename[len-3], ":ts")) {
			pnodename[len-3] = '\0';
		}
	}
	*pname = pnodename;			/* return node name	  */


	if ((*ntype == NTYPE_PBS) && (plist != NULL)) {
		/* double check type */
		while (plist) {
			if (!strcasecmp(plist->al_name, ATTR_NODE_ntype))
				break;
			plist = (svrattrl *)GET_NEXT(plist->al_link);
		}
		if (plist) {
			clear_attr(&lattr, &node_attr_def[ND_ATR_ntype]);
			(void)decode_ntype(&lattr, plist->al_name, 0, plist->al_value);
			*ntype = (int)lattr.at_val.at_short;
		}
	}

	return  (0);				/* function successful    */
}

static char *nodeerrtxt = "Node description file update failed";

/**
 * @brief
 *		Static function to update the specified mom in the db. If the
 *		NODE_UPDATE_OTHERS flag is set: for each node, it also calls
 *		the "write_single_node_state" function to update the state and
 *		comment of the node.  If the NODE_UPDATE_MOM flag is set, it
 *		calls write_single_node_mom_attr to update the attribute of
 *		the node.
 *
 *		We don't need to write the nodes in any particular order anymore. The
 *		nodes (while reading) will be read sorted on the nd_index column, which
 *		is the value of the nd_nummoms (number of moms the node is part of).
 *		This ensures the nodes which belong only one mom are loaded first, and
 *		the nodes with multi moms are loaded later.
 *
 * @see
 * 		save_nodes_db, save_nodes_db_inner
 *
 * @return	error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
static int
save_nodes_db_mom(mominfo_t *pmom)
{
	struct pbsnode *np;
	pbs_list_head wrtattr;
	mom_svrinfo_t *psvrm;
	int	isoff;
	int	hascomment;
	int	nchild;

	CLEAR_HEAD(wrtattr);

	if (pmom == NULL)
		return -1;

	psvrm = (mom_svrinfo_t *) pmom->mi_data;
	for (nchild = 0; nchild < psvrm->msr_numvnds; ++nchild) {
		np = psvrm->msr_children[nchild];
		if (np == NULL)
			continue;

		if (np->nd_state & INUSE_DELETED) {
			/* this shouldn't happen, if it does, ignore it */
			continue;
		}

		if (np->nd_modified & NODE_UPDATE_OTHERS) {
			DBPRT(("Saving node %s into the database\n", np->nd_name))
			if (node_save_db(np) != 0) {
				log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
					LOG_WARNING, "nodes", nodeerrtxt);
				return (-1);
			}
			/*
			 * node record were deleted
			 * so add state and comments only if set
			 */
			isoff = np->nd_state &
				(INUSE_OFFLINE | INUSE_OFFLINE_BY_MOM | INUSE_SLEEP);

			hascomment = (np->nd_attr[(int) ND_ATR_Comment].at_flags &
				(ATR_VFLAG_SET | ATR_VFLAG_DEFLT)) == ATR_VFLAG_SET;

			if (isoff)
				np->nd_modified |= NODE_UPDATE_STATE;

			if (hascomment)
				np->nd_modified |= NODE_UPDATE_COMMENT;

			write_single_node_state(np);
		} else if (np->nd_modified & NODE_UPDATE_MOM) {
			write_single_node_mom_attr(np);
		}
	}

	return 0;
}

/**
 * @brief
 *		Static function to update all the nodes in the db
 *
 * @see
 * 		save_nodes_db_mom
 *
 * @return	error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
static int
save_nodes_db_inner(void)
{
	int i;
	pbs_list_head wrtattr;
	mominfo_t *pmom;

	/* for each Mom ... */
	CLEAR_HEAD(wrtattr);

	for (i = 0; i < mominfo_array_size; ++i) {
		pmom = mominfo_array[i];
		if (pmom == NULL)
			continue;

		if(save_nodes_db_mom(pmom) == -1)
			return -1;
	}
	return 0;
}

/**
 * @brief
 *		When called, this function will update
 *		all the nodes in the db. It will update the mominfo_time to the db
 *		and save all the nodes which has the NODE_UPDATE_OTHERS flag set. It
 *		saves the nodes by calling a helper function save_nodes_db_inner.
 *
 *  	The updates are done under a single transaction.
 *  	Upon successful conclusion the transaction is commited.
 *
 * @param[in]	changemodtime - flag to change the mom modification time or not
 * @param[in]	p - when p is set, save the specific mom to the db
 *		    when p is unset, save all the nodes to the db
 *
 * @return	error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
save_nodes_db(int changemodtime, void *p)
{
	struct pbsnode  *np;
	pbs_db_mominfo_time_t mom_tm;
	pbs_db_obj_info_t obj;
	int           num;
	attribute    *pattr;
	resource     *resc;
	char         *rname;
	resource_def *rscdef;
	int	i;
	mominfo_t    *pmom = (mominfo_t *) p;

	DBPRT(("%s: entered\n", __func__))

	if (changemodtime) {	/* update generation on host-vnode map */
		if (mominfo_time.mit_time == time(0))
			mominfo_time.mit_gen++;
		else {
			mominfo_time.mit_time = time(0);
			mominfo_time.mit_gen  = 1;
		}
	}

	if (svr_totnodes == 0 || mominfo_array_size == 0) {
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
			LOG_ALERT, "nodes",
			"Server has empty nodes list");
		return (-1);
	}

	/* begin transaction */
	if (pbs_db_begin_trx(svr_db_conn, 0, 0) !=0)
		goto db_err;

	/* insert/update the mominfo_time to db */
	mom_tm.mit_time = mominfo_time.mit_time;
	mom_tm.mit_gen = mominfo_time.mit_gen;
	obj.pbs_db_obj_type = PBS_DB_MOMINFO_TIME;
	obj.pbs_db_un.pbs_db_mominfo_tm = &mom_tm;

	if (pbs_db_save_obj(svr_db_conn, &obj, PBS_UPDATE_DB_FULL) == 1) {/* no row updated */
		if (pbs_db_save_obj(svr_db_conn, &obj, PBS_INSERT_DB) != 0) /* insert also failed */
			goto db_err;
	}

	if (pmom) {
		if (save_nodes_db_mom(pmom) == -1)
			goto db_err;
	} else {
		if (save_nodes_db_inner() == -1)
			goto db_err;
	}

	if (pbs_db_end_trx(svr_db_conn, PBS_DB_COMMIT) != 0)
		goto db_err;

	/*
	 * Clear the ATR_VFLAG_MODIFY bit on each node attribute
	 * and on the node_group_key resource, for those nodes
	 * that possess a node_group_key resource
	 */

	if (server.sv_attr[SRV_ATR_NodeGroupKey].at_flags & ATR_VFLAG_SET  &&
		server.sv_attr[SRV_ATR_NodeGroupKey].at_val.at_str)
		rname = server.sv_attr[SRV_ATR_NodeGroupKey].at_val.at_str;
	else
		rname = NULL;

	if (rname)
		rscdef = find_resc_def(svr_resc_def, rname, svr_resc_size);
	else
		rscdef = NULL;

	for (i=0; i<svr_totnodes; i++) {
		np = pbsndlist[i];
		if (np->nd_state & INUSE_DELETED)
			continue;

		/* reset only after transaction is committed */
		np->nd_modified &= ~(NODE_UPDATE_OTHERS | NODE_UPDATE_STATE | NODE_UPDATE_COMMENT);

		for (num=0; num<ND_ATR_LAST; num++) {

			np->nd_attr[num].at_flags &= ~ATR_VFLAG_MODIFY;

			if (num == ND_ATR_ResourceAvail)
				if (rname != NULL && rscdef != NULL) {
					pattr = &np->nd_attr[ND_ATR_ResourceAvail];
					if ((resc = find_resc_entry(pattr, rscdef)))
						resc->rs_value.at_flags &= ~ATR_VFLAG_MODIFY;
				}

		}
	}
	return (0);

db_err:
	strcpy(log_buffer, "Unable to save node data base ");
	if (svr_db_conn->conn_db_err != NULL)
		strncat(log_buffer, svr_db_conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);
	(void) pbs_db_end_trx(svr_db_conn, PBS_DB_ROLLBACK);
	panic_stop_db(log_buffer);
	return (-1);
}



/**
 * @brief
 * 		init_prop - allocate and initialize a prop struct
 *
 * @param[in]	pname	-	points to the property string
 *
 * @return	struct prop *
 */

struct prop *init_prop(char *pname)
{
	struct prop *pp;

	if ((pp = (struct prop *)malloc(sizeof(struct prop))) != NULL) {
		pp->name    = pname;
		pp->mark    = 0;
		pp->next    = 0;
	}

	return (pp);
}

/**
 * @brief
 * 		create_subnode - create a subnode entry and link to parent node
 * @see
 * 		mod_node_ncpus, set_nodes, create_pbs_node2
 * @param[in] - pnode -	parent node.
 * @param[in] - lstsn - Points to the last subnode in the parent node list. This
 *						eliminates the need to find the last node in parent node list.
 *
 * @return	struct pbssubn *
 */
struct pbssubn *create_subnode(struct pbsnode *pnode, struct pbssubn *lstsn)
{
	struct pbssubn  *psubn;
	struct pbssubn **nxtsn;

	psubn = (struct pbssubn *)malloc(sizeof(struct pbssubn));
	if (psubn == NULL) {
		return NULL;
	}

	/* initialize the subnode and link into the parent node */

	psubn->next  = NULL;
	psubn->jobs  = NULL;
	psubn->inuse = 0;
	psubn->index = pnode->nd_nsn++;
	pnode->nd_nsnfree++;
	if ((pnode->nd_state & INUSE_JOB) != 0) {
		/* set_vnode_state(pnode, INUSE_FREE, Nd_State_Set); */
		/* removed as part of OS prov fix- this was causing a provisioning
		 * node to lose its INUSE_PROV flag. Prb occurred when OS with low
		 * ncpus booted into OS with higher ncpus.
		 */
		set_vnode_state(pnode, ~INUSE_JOB, Nd_State_And);
	}

	if(lstsn) /* If not null, then append new subnode directly to the last node */
		lstsn->next = psubn;
	else{
		nxtsn = &pnode->nd_psn;	   /* link subnode onto parent node's list */
		while (*nxtsn)
			nxtsn = &((*nxtsn)->next);
		*nxtsn = psubn;
	}
	return (psubn);
}

/**
 * @brief
 *		Read the file, "nodes", containing the list of properties for each node.
 *		The list of nodes is formed with pbsndlist as the head.
 *		Return -1 on error, 0 otherwise.
 *
 *		Read the node state file, "node_state", for any "offline"
 *		conditions which should be set in the nodes.
 *
 *		If routine returns -1, then "log_buffer" contains a message to
 *		be logged.
 * @see
 * 		svr_migrate_data_from_fs
 *
 * @param[in]	preprocess	- arg set for first call to just scan for old style properties
 * 					and create matching boolean resources. On second call,
 * 					"preprocess" is zero, makes the node entries.
 * @return	void
 * @retval	-1	- error
 * @retval	0	- success
 *
 * @par MT-safe: No
 */

#define MAXNLINE 2048
#define FILE_VERSION "node_file_ver "
int
setup_nodes_fs(int preprocess)
{
	FILE	 *nin;
	char	  line[MAXNLINE];
	char	  comm[MAXNLINE];
	char	 *nodename;
	char	 *token;
	int	  bad, i, num, linenum;
	int	  err;
	time_t	  mom_modtime;
	struct pbsnode *np;
	char	 *rsc;
	char     *val;
	char      xchar;
	svrattrl *pal;
	int	  perm = ATR_DFLAG_ACCESS | ATR_PERM_ALLOW_INDIRECT;
	int	  file_version = 0;
	pbs_list_head atrlist;
	long	  sharing_val;
	char	 *sharing_str;
	static char *ravail = ATTR_rescavail;
	static char file_ver_str[] =  FILE_VERSION;
	static char file_ver_err[] = "Invalid \"nodes\" version";
	static char cr_attr_err[]  = "cannot create node attribute";
	static char br_resc_err[]  = "old style property %s already defined as non-boolean resource and/or not node level resource, cannot convert it";
	static char timestamp[] = "$modtime=";
	int	    resc_added = 0;


	DBPRT(("%s: entered\n", __func__))
	CLEAR_HEAD(atrlist);

#ifdef WIN32
	fix_perms(path_nodes);
#endif
	if ((nin = fopen(path_nodes, "r")) == NULL) {
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
			LOG_ALERT, "nodes",
			"Server has empty nodes list");
		return (0);
	}


	if (!preprocess) {
		tfree2(&streams);
		tfree2(&ipaddrs);

		svr_totnodes = 0;
	}

	mominfo_time.mit_gen = 1;	/* set to be newer than Mom's default */
	for (linenum=1; fgets(line, MAXNLINE, nin); linenum++) {

		line[MAXNLINE-1]     = '\0';
		line[strlen(line)-1] = '\0';
		sharing_val = VNS_UNSET;

		if (line[0] == '#')	/* comment */
			continue;
		if (line[0] == '$') {
			if (strncmp(line, timestamp, strlen(timestamp)) == 0) {
				sscanf(line+strlen(timestamp), "%lu.%d",
					&mominfo_time.mit_time,
					&mominfo_time.mit_gen);
			}
			continue;
		}
		mom_modtime = 0;
		if (strncmp(line, file_ver_str, strlen(file_ver_str)) == 0) {
			if (file_version != 0) {
				strcpy(log_buffer, file_ver_err);
				return -1;
			}
			sscanf(line+strlen(file_ver_str), "%d", &file_version);
			if (file_version != 2) {
				strcpy(log_buffer, file_ver_err);
				return -1;
			}
		}

		/* first token is the node name 	*/
		/* version 1: may have ":ts" appended	*/

		token = parse_node_token(line, 1, &err, &xchar);
		if (token == NULL)
			continue;	/* blank line */
		if (err) {
			sprintf(log_buffer,
				"invalid character in token \"%s\" on line %d",
				token, linenum);
			goto errtoken2;
		}
		if (!isalnum((int)*token)) {
			sprintf(log_buffer, "token \"%s\" doesn't start with alphanumeric on line %d", token, linenum);
			goto errtoken2;
		}
		nodename = token;

		/* now process remaining tokens (if any), they may be either */
		/* attributes (keyword=value) or old style properties        */

		while (1) {

			rsc = NULL;
			token = parse_node_token(NULL, 0, &err, &xchar);
			if (err)
				goto errtoken1;
			if (token == NULL)
				break;

			/* check for special "keywords" such as [ts] */
			if (strcasecmp(token, "[ts]") == 0) {
				val = parse_node_token(NULL, 0, &err, &xchar);
				if ((val==NULL) || err)
					goto errtoken1;
				mom_modtime = (time_t)atol(val);
				continue;	/* will use it latter */
			} else if (strcmp(token, "[sharing]") == 0) {
				val = parse_node_token(NULL, 0, &err, &xchar);
				if ((val==NULL) || err)
					goto errtoken1;
				/* old style(<=10.4) enum int value writen into the nodes file
				 * We need to convert from old enum values to new enum also
				 */
				if (isdigit(val[0])) {
					static char *share_words[] = {
						ND_Default_Shared,
						ND_Default_Excl,
						ND_Ignore_Excl,
						ND_Force_Excl
					};
					i = atol(val);
					if (i >= 0 && i < sizeof(share_words)/sizeof(share_words[0]))
						sharing_str = share_words[i];
					else
						sharing_str = "";
				}
				else
					sharing_str = val;

				sharing_val = str_to_vnode_sharing(sharing_str);
				if (sharing_val == VNS_UNSET) {
					sprintf(log_buffer, "Unknown sharing value: %s on line %d of file nodes", val, linenum);
					goto errtoken2;
				}
				continue;
			}

			if (xchar == '.') {

				/* have resource */

				rsc = parse_node_token(NULL, 0, &err, &xchar);
				if ((rsc==NULL) || err || (xchar!='='))
					goto errtoken1;
			}

			if (xchar == '=') {

				/* have normal attribute, keyword=value */

				if (strcasecmp(token, ATTR_NODE_Host) == 0) {
					token = ATTR_NODE_Mom;
				}
				val = parse_node_token(NULL, 2, &err, &xchar);
				/* NOTE - here "val" if non null is on the heap */
				if ((val==NULL) || err || (xchar=='=')) {
					free(val);
					val = NULL;
					goto errtoken1;
				}

				if (!preprocess) {
					if ((rsc == NULL) && ((strcmp(token, "np")==0) ||
						(strcmp(token, "ncpus")==0))) {
						/* translate np=# to resources_avail.ncpus=# */
						rsc = "ncpus";
						token = ravail;
					}

					pal = attrlist_create(token, rsc, strlen(val)+1);
					if (pal == NULL) {
						strcpy(log_buffer, cr_attr_err);
						free(val);
						val = NULL;
						goto errtoken2;
					}
					(void)strcpy(pal->al_value, val);
					pal->al_flags = SET;
					append_link(&atrlist, &pal->al_link, pal);
				}
				free(val);
				val = NULL;

			} else {

				/* old style properity */

				if (preprocess) {

					if ((err = add_resource_def(token, ATR_TYPE_BOOL, READ_WRITE | ATR_DFLAG_CVTSLT)) == -1) {
						strcpy(log_buffer, cr_attr_err);
						goto errtoken2;
					} else if (err == -2) {
						sprintf(log_buffer, br_resc_err, token);
						goto errtoken2;
					}
					resc_added++;

				} else {

					pal = attrlist_create(ravail, token, strlen(ATR_TRUE)+1);
					if (pal == NULL) {
						strcpy(log_buffer, cr_attr_err);
						goto errtoken2;
					}
					(void)strcpy(pal->al_value, ATR_TRUE);
					pal->al_flags = SET;
					append_link(&atrlist, &pal->al_link, pal);
				}
			}

		}

		if (resc_added > 0) {
			log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_HOOK,
				LOG_INFO, "setup_nodes_fs",
			"Restarting Python interpreter as resourcedef file has changed.");
			pbs_python_ext_shutdown_interpreter(&svr_interp_data);
			pbs_python_ext_start_interpreter(&svr_interp_data);

			send_rescdef(1);
		}

		if (!preprocess) {
			/* now create node and subnodes */

			pal = GET_NEXT(atrlist);
			err = create_pbs_node2(nodename, pal, perm, &bad, &np, FALSE, TRUE); /* allow unknown resources */
			if (err == PBSE_NODEEXIST) {
				sprintf(log_buffer, "duplicate node \"%s\"on line %d",
					nodename, linenum);
				goto errtoken2;
			} else if (err) {
				sprintf(log_buffer,
					"could not create node \"%s\", error = %d",
					nodename, err);
				goto errtoken2;
			}
			free_attrlist(&atrlist);
			if (mom_modtime) {
				/* a vnode pointer will be returned */
				if (np)
					np->nd_moms[0]->mi_modtime = mom_modtime;
			}
			if (sharing_val != VNS_UNSET) {
				np->nd_attr[ND_ATR_Sharing].at_val.at_long =sharing_val;
				np->nd_attr[ND_ATR_Sharing].at_flags = ATR_VFLAG_SET | ATR_VFLAG_DEFLT;
			}

		}
	}

	fclose(nin);

	if (!preprocess) {
		nin = fopen(path_nodestate, "r");
		if (nin != NULL) {
			while (fscanf(nin, "%s %d", line, &num) == 2) {
				fgets(comm, MAXNLINE-1, nin);
				for (i=0; i<svr_totnodes; i++) {
					np = pbsndlist[i];
					if (np->nd_state & INUSE_DELETED)
						continue;

					if (strcmp(np->nd_name, line) == 0) {
						set_vnode_state(np, num & (INUSE_OFFLINE|INUSE_OFFLINE_BY_MOM), Nd_State_Or);
						if (comm[0] != '\n') {
							comm[strlen(comm) - 1] = '\0';
							node_attr_def[(int)ND_ATR_Comment].at_free(
								&np->nd_attr[(int)ND_ATR_Comment]);
							node_attr_def[(int)ND_ATR_Comment].at_decode(
								&np->nd_attr[(int)ND_ATR_Comment],
								ATTR_comment,
								NULL,
								comm);
						}
						break;
					}
				}
			}
			(void)fclose(nin);
		}

		/* clear MODIFY bit on attributes */
		for (i=0; i<svr_totnodes; i++) {
			np = pbsndlist[i];
			for (num=0; num<ND_ATR_LAST; num++) {
				np->nd_attr[num].at_flags &= ~ATR_VFLAG_MODIFY;
			}
		}
	}
	svr_chngNodesfile = 0;	/* clear in case set while creating node */

	return (0);

errtoken1:
	sprintf(log_buffer, "token \"%s\" in error on line %d of file nodes",
		token, linenum);
errtoken2:
	free_attrlist(&atrlist);
	fclose(nin);
	return -1;
}

/**
 * @brief
 *		Read the, "nodes" information from database
 *		containing the list of properties for each node.
 *		The list of nodes is formed with pbsndlist as the head.
 * @see
 * 		pbsd_init
 *
 * @return	error code
 * @retval	-1	- Failure
 * @retval	0	- Success
 *
 */
int
setup_nodes()
{
	int	  err;
	int       perm = ATR_DFLAG_ACCESS | ATR_PERM_ALLOW_INDIRECT;
	pbs_db_obj_info_t   obj;
	pbs_db_node_info_t dbnode;
	pbs_db_mominfo_time_t mom_tm;
	void *state;
	int rc;
	time_t	  mom_modtime = 0;
	struct pbsnode *np;
	pbs_list_head atrlist;
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	svrattrl *pal;
	int bad;
	int i, num;

	DBPRT(("%s: entered\n", __func__))
	CLEAR_HEAD(atrlist);

	tfree2(&streams);
	tfree2(&ipaddrs);

	svr_totnodes = 0;

	/* start a transaction */
	if (pbs_db_begin_trx(conn, 0, 0) != 0)
		return (-1);

	/* Load  the mominfo_time from the db */
	obj.pbs_db_obj_type = PBS_DB_MOMINFO_TIME;
	obj.pbs_db_un.pbs_db_mominfo_tm = &mom_tm;
	if (pbs_db_load_obj(svr_db_conn, &obj) == -1) {
		sprintf(log_buffer, "Could not load momtime info");
		goto db_err;
	}
	mominfo_time.mit_time = mom_tm.mit_time;
	mominfo_time.mit_gen = mom_tm.mit_gen;

	obj.pbs_db_obj_type = PBS_DB_NODE;
	obj.pbs_db_un.pbs_db_node = &dbnode;
	state = pbs_db_cursor_init(conn, &obj, NULL);
	if (state == NULL) {
		sprintf(log_buffer, "%s", (char *) conn->conn_db_err);
		goto db_err;
	}

	while ((rc = pbs_db_cursor_next(conn, state, &obj)) == 0) {
		/* recover node without triggering action routines */
		if (node_recov_db_raw(&dbnode, &atrlist) != 0) {
			sprintf(log_buffer,
				"Could not load node info for %s",
				dbnode.nd_name);
			pbs_db_cursor_close(conn, state);
			goto db_err;
		}
		mom_modtime = dbnode.mom_modtime;

		/* now create node and subnodes */
		pal = GET_NEXT(atrlist);
		err = create_pbs_node2(dbnode.nd_name, pal, perm, &bad, &np, FALSE, TRUE);	/* allow unknown resources */
		free_attrlist(&atrlist);
		if (err) {
			if (err == PBSE_NODEEXIST) {
				sprintf(log_buffer, "duplicate node \"%s\"",
					dbnode.nd_name);
			} else {
				sprintf(log_buffer,
					"could not create node \"%s\", "
					"error = %d",
					dbnode.nd_name, err);
			}
			log_err(-1, "setup_nodes", log_buffer);
			continue; /* continue recovering other nodes */
		}
		if (mom_modtime) {
			/* a vnode pointer will be returned */
			if (np)
				np->nd_moms[0]->mi_modtime = mom_modtime;
		}
		if (np) {
			if ((np->nd_attr[(int)ND_ATR_vnode_pool].at_flags & ATR_VFLAG_SET) &&
			    (np->nd_attr[(int)ND_ATR_vnode_pool].at_val.at_long > 0)) {
				mominfo_t *pmom = np->nd_moms[0];
				if (pmom &&
				    (np == ((mom_svrinfo_t *)(pmom->mi_data))->msr_children[0])) {
					/* natural vnode being recovered, add to pool */
					(void)add_mom_to_pool(np->nd_moms[0]);
				}
			}
		}
		pbs_db_reset_obj(&obj);
	}

	pbs_db_cursor_close(conn, state);
	if (pbs_db_end_trx(conn, PBS_DB_COMMIT) !=0)
		goto db_err;

	/* clear MODIFY bit on attributes */
	for (i=0; i<svr_totnodes; i++) {
		np = pbsndlist[i];
		for (num=0; num<ND_ATR_LAST; num++) {
			np->nd_attr[num].at_flags &= ~ATR_VFLAG_MODIFY;
		}
		np->nd_modified = 0; /* clear nd_modified on node since create_pbsnode set it*/
	}
	svr_chngNodesfile = 0;	/* clear in case set while creating node */

	return (0);
db_err:
	log_err(-1, "setup_nodes", log_buffer);
	(void) pbs_db_end_trx(conn, PBS_DB_ROLLBACK);
	return (-1);
}


/**
 * @brief
 * 		delete_a_subnode - mark a (last) single subnode entry as deleted
 * @see
 * 		mod_node_ncpus
 *
 * @param[in,out]	pnode	- parent node list
 *
 * @return	void
 */
static void
delete_a_subnode(struct pbsnode *pnode)
{
	struct pbssubn *psubn;
	struct pbssubn *pprior = 0;

	psubn = pnode->nd_psn;

	while (psubn->next) {
		pprior = psubn;
		psubn = psubn->next;
	}

	/*
	 * found last subnode in list for given node, mark it deleted
	 * note, have to update nd_nsnfree using pnode
	 * because it point to the real node rather than the the copy (pnode)
	 * and the real node is overwritten by the copy
	 */

	if ((psubn->inuse & INUSE_JOB) == 0)
		pnode->nd_nsnfree--;

	subnode_delete(psubn);
	if (pprior)
		pprior->next = NULL;
}

/**
 * @brief
 * 		mod_node_ncpus - when resources_available.ncpus changes, need to
 *		update the number of subnodes, creating or deleting as required
 *
 * @param[in,out]	pnode	- parent node list
 * @param[in]		ncpus	- resources_available.ncpus
 * @param[in]		actmode	- value for the "actmode" parameter
 *
 * @return	int
 * @return	0	- success
 */

int
mod_node_ncpus(struct pbsnode *pnode, long ncpus, int actmode)
{
	long		old_np;
	struct pbssubn *lst_sn;
	if ((actmode == ATR_ACTION_NEW) || (actmode == ATR_ACTION_ALTER)) {

		if (ncpus < 0)
			return PBSE_BADATVAL;
		else if (ncpus == 0)
			ncpus = 1;		/* insure at least 1 subnode */

		old_np = pnode->nd_nsn;
		if (old_np != ncpus)
			svr_chngNodesfile = 1;	/* force update on shutdown */
		lst_sn = NULL;
		while (ncpus != old_np) {

			if (ncpus < old_np) {
				delete_a_subnode(pnode);
				old_np--;
			} else {
				/* Store the last subnode of parent node list.
				 * This removes the need to find the last node of
				 * parent node's list, in create_subnode().
				 */
				lst_sn = create_subnode(pnode, lst_sn);
				old_np++;
			}
		}
		pnode->nd_nsn = old_np;
	}
	return 0;
}

/**
 * @brief
 * 		set_clear_target - set or clear ATR_VFLAG_TARGET flag in a target resource
 *		"index" is the index into the node's attribute array (which attr).
 *		either ND_ATR__ResourceAvail or ND_ATR_ResourceAssn
 *
 * @param[out]	psourcend	- Vnode structure
 * @param[in]	psourcerc	- target resource
 * @param[in]	index		- index into the node's attribute array
 * @param[in]	set			- decides set or unset.
 *
 * @return	int
 * @retval	-1	- error
 * @retval	-	- success
 */
int
set_clear_target(struct pbsnode *psourcend, resource *psourcerc, int index, int set)
{
	char		*nname;
	char		*pn;
	struct pbsnode 	*pnode;
	resource	*ptargetrc;

	if (psourcend)
		nname = psourcend->nd_name;
	else
		nname = " ";

	pn = psourcerc->rs_value.at_val.at_str;
	if ((pn == NULL) ||
		(*pn != '@') ||
		((pnode = find_nodebyname(pn+1)) == NULL)) {
		sprintf(log_buffer,
			"resource %s on vnode points to invalid vnode %s",
			psourcerc->rs_defin->rs_name, pn);
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_NODE, LOG_CRIT,
			nname, log_buffer);
		return -1;
	}

	ptargetrc = find_resc_entry(&pnode->nd_attr[index], psourcerc->rs_defin);
	if (ptargetrc == NULL) {
		sprintf(log_buffer, "resource %s on vnode points to missing resource on vnode %s", psourcerc->rs_defin->rs_name, pn+1);
		log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_NODE, LOG_CRIT,
			nname, log_buffer);
		return -1;
	} else {
		if (set)
			ptargetrc->rs_value.at_flags |= ATR_VFLAG_TARGET;
		else
			ptargetrc->rs_value.at_flags &= ~ATR_VFLAG_TARGET;
	}
	return 0;
}

/**
 * @brief
 * 		indirect_target_check - called via a work task to (re)set ATR_VFLAG_TARGET
 *		in any resource which is the target of another indirect resource
 *
 *		This covers the cases where a target node might not have been setup
 *		on Server recovery/startup.
 * @see
 * 		fix_indirectness and mgr_unset_attr
 *
 * @param[in]	ptask	-	work task structure
 *
 * @return	void
 */

void
indirect_target_check(struct work_task *ptask)
{
	int		 i;
	attribute	*pattr;
	struct pbsnode	*pnode;
	resource	*presc;

	for (i=0; i<svr_totnodes; i++) {
		pnode = pbsndlist[i];
		if (pnode->nd_state & INUSE_DELETED ||
			pnode->nd_state & INUSE_STALE)
			continue;
		pattr = &pnode->nd_attr[(int)ND_ATR_ResourceAvail];
		if (pattr->at_flags & ATR_VFLAG_SET) {
			for (presc = (resource *)GET_NEXT(pattr->at_val.at_list);
				presc;
				presc = (resource *)GET_NEXT(presc->rs_link)) {

				if (presc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {
					set_clear_target(pnode, presc, (int)ND_ATR_ResourceAvail, 1);
				}
			}
		}
	}
}

/**
 * @brief
 * 		fix_indirectness - check if a member of a node's resource_available is
 *		becoming indirect (points to another node) or was indirect and is
 *		becoming direct.
 *
 *		If becoming indirect, check that the target node is known (unless just
 *		recovering) and that the target resource itself is not indirect.
 *
 *		If "doit" is true, then and only then make the needed changes in
 *		resources_available and resources_assigned.
 * @see
 * 		node_np_action and update2_to_vnode
 *
 * @param[in]		presc	-	pointer to resource structure
 * @param[in,out]	pnode	-	the node for checking.
 * @param[in]		doit	-	If "doit" is true, then and only then make the needed changes in
 * 								recovering) and that the target resource itself is not indirect.
 *
 * @return	int
 * @return	0	- success
 * @retval	nonzero	- failure
 *
 */
int
fix_indirectness(resource *presc, struct pbsnode *pnode, int doit)
{
	int		     consumable;
	resource            *porignalrc;	/* original before change */
	struct pbssubn	    *psn;
	struct pbsnode      *ptargetnd;		/* target node		  */
	resource            *ptargetrc;		/* target resource avail  */
	resource            *ptargetan;		/* target resource assigned  */
	struct resource_def *prdef;
	int		     recover_ok;
	int		     run_safety_check = 0;

	prdef = presc->rs_defin;

	recover_ok = (server.sv_attr[(int)SRV_ATR_State].at_val.at_long == SV_STATE_INIT);	/* if true, then recoverying and targets may not yet be there */
	consumable = prdef->rs_flags & (ATR_DFLAG_ANASSN | ATR_DFLAG_FNASSN);
	porignalrc = find_resc_entry(&pnode->nd_attr[(int)ND_ATR_ResourceAvail], prdef);

	if (doit == 0) {	/* check for validity only this pass */

		if (presc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {

			/* disallow change if vnode has running jobs */
			for (psn = pnode->nd_psn; psn; psn = psn->next) {
				if (psn->jobs != NULL)
					return PBSE_OBJBUSY;
			}

			/* setting this resource to be indirect, make serveral checks */

			/* this vnode may not be a target of another indirect */
			if (porignalrc) {
				if (porignalrc->rs_value.at_flags & ATR_VFLAG_TARGET) {
					if ((resc_in_err = strdup(porignalrc->rs_defin->rs_name)) == NULL)
						return PBSE_SYSTEM;
					return PBSE_INDIRECTHOP;
				}
			}

			/* target vnode must be known unless the Server is recovering */
			/* the value (at_str) is "@vnodename", so skip over the '@'   */
			ptargetnd = find_nodebyname(presc->rs_value.at_val.at_str+1);
			if (ptargetnd == NULL) {
				if (! recover_ok)
					return (PBSE_UNKNODE);
			} else {

				/* target resource must exist */
				ptargetrc = find_resc_entry(&ptargetnd->nd_attr[(int)ND_ATR_ResourceAvail], prdef);
				if (pnode == ptargetnd) {
					/* target node may not be itself  */
					if ((resc_in_err = strdup(prdef->rs_name)) == NULL)
						return PBSE_SYSTEM;
					return PBSE_INDIRECTHOP;
				} else if (ptargetrc == NULL) {
					if ((resc_in_err = strdup(prdef->rs_name)) == NULL)
						return PBSE_SYSTEM;
					return (PBSE_INDIRECTBT);
				} else {
					if (ptargetrc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {
						/* target cannot be indirect itself */
						if ((resc_in_err = strdup(ptargetrc->rs_defin->rs_name)) == NULL)
							return PBSE_SYSTEM;
						return PBSE_INDIRECTHOP;
					}
				}
				/* if consumable, insure resource exists in this node's */
				/* resources_assigned */

				if (consumable) {
					ptargetrc = add_resource_entry(&pnode->nd_attr[(int)ND_ATR_ResourceAssn], prdef);
					if (ptargetrc == NULL)
						return PBSE_SYSTEM;
				}
			}

		} else {

			/* new is not indirect, was the original? */

			if (porignalrc) {
				if (porignalrc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {
					/* disallow change if vnode has running jobs */
					for (psn = pnode->nd_psn; psn; psn = psn->next) {
						if (psn->jobs != NULL)
							return PBSE_OBJBUSY;
					}
				}
			}
		}
		return PBSE_NONE;

	} else {

		/*
		 * In this pass,  actually do the required changes:
		 * If setting...
		 *	- set ATR_VFLAG_TARGET on the target resource entry
		 *  - change the paired Resource_Assigned entry to also be indirect
		 * If unsetting...
		 *	- clear ATR_VFLAG_TARGET on the old target resource
		 *  - change the paired Resource_Assigned entry to be direct
		 */

		if (presc->rs_value.at_flags & ATR_VFLAG_INDIRECT) {

			/* setting to be indirect */

			if (set_clear_target(pnode, presc, (int)ND_ATR_ResourceAvail, 1) == -1)
				run_safety_check = 1;  /* need to set after nodes done */

			/* resource_assigned for resource being changed ... */
			ptargetan = find_resc_entry(&pnode->nd_attr[(int)ND_ATR_ResourceAssn], prdef);
			if (consumable && (ptargetan != NULL)) {
				prdef->rs_free(&ptargetan->rs_value);	/* free first */
				(void)decode_str(&ptargetan->rs_value, NULL, NULL,
					presc->rs_value.at_val.at_str);
				ptargetan->rs_value.at_flags |= ATR_VFLAG_INDIRECT;
			}

		} else if ((porignalrc != NULL) && (porignalrc->rs_value.at_flags & ATR_VFLAG_INDIRECT)) {
			/* unsetting an old indirect reference */
			/* Clear ATR_VFLAG_TARGET on target vnode */
			(void)set_clear_target(pnode, porignalrc, (int)ND_ATR_ResourceAvail, 0);

			/* clear ATR_VFLAG_INDIRECT on the resc being changed */
			free_str(&porignalrc->rs_value);
			porignalrc->rs_value.at_flags &= ~ATR_VFLAG_INDIRECT;
			ptargetan = find_resc_entry(&pnode->nd_attr[(int)ND_ATR_ResourceAssn], prdef);		/* resource_assigned for resource being changed ... */
			if (consumable && (ptargetan != NULL)) {
				free_str(&ptargetan->rs_value);
				ptargetan->rs_value.at_flags &= ~ATR_VFLAG_INDIRECT;
			}

			run_safety_check = 1;
		}
		if (run_safety_check) 	/* double check TARGET bit on targets */
			(void)set_task(WORK_Immed, 0, indirect_target_check, NULL);

	}
	return 0;
}

/**
 * @brief
 * 		node_np_action - action routine for a node's resources_available attribute
 *		Does several things:
 *		1. prohibits resources_available.hosts from being changed;
 *		2. when resources_available.ncpus (np in nodes file) changes,
 *	   	update the subnode structures;
 *		3. For any modified resource, check if it is changing "indirectness"
 *
 * @param[in]	new	-	newly changed resources_available
 * @param[in]	pobj	-	pointer to a pbsnode struct
 * @param[in]	actmode	-	action mode: "NEW" or "ALTER"
 *
 * @return	int
 * @retval	0	- success
 * @retval	nonzero	- error
 */

int
node_np_action(attribute *new, void *pobj, int actmode)
{
	int		err;
	struct pbsnode *pnode = (struct pbsnode *)pobj;
	resource_def   *prdef;
	resource       *presc;
	long		new_np;

	if (actmode == ATR_ACTION_FREE)	/* cannot unset resources_available */
		return (PBSE_IVALREQ);

	/* 1. prevent change of "host" or "vnode" */
	prdef = find_resc_def(svr_resc_def, "host", svr_resc_size);
	presc = find_resc_entry(new, prdef);
	if ((presc != NULL) &&
		(presc->rs_value.at_flags & ATR_VFLAG_MODIFY)) {
		if (actmode != ATR_ACTION_NEW)
			return (PBSE_ATTRRO);
	}
	prdef = find_resc_def(svr_resc_def, "vnode", svr_resc_size);
	presc = find_resc_entry(new, prdef);
	if ((presc != NULL) &&
		(presc->rs_value.at_flags & ATR_VFLAG_MODIFY)) {
		if (actmode != ATR_ACTION_NEW)
			return (PBSE_ATTRRO);
	}
	/* prevent change of "aoe" */
	prdef = find_resc_def(svr_resc_def, "aoe", svr_resc_size);
	presc = find_resc_entry(new, prdef);
	if ((presc != NULL) &&
		(presc->rs_value.at_flags & ATR_VFLAG_MODIFY)) {
		if (pnode->nd_state & (INUSE_PROV | INUSE_WAIT_PROV))
			return (PBSE_NODEPROV_NOACTION);
		if ((pnode->nd_attr[(int) ND_ATR_Mom].at_flags & ATR_VFLAG_SET)
			&& (!compare_short_hostname(
			pnode->nd_attr[(int) ND_ATR_Mom].at_val.at_arst->as_string[0],
			server_host)))
			return (PBSE_PROV_HEADERROR);
	}

	/* 2. If changing ncpus, fix subnodes */
	prdef = find_resc_def(svr_resc_def, "ncpus", svr_resc_size);
	presc = find_resc_entry(new, prdef);

	if (presc == NULL)
		return PBSE_SYSTEM;
	if (presc->rs_value.at_flags & ATR_VFLAG_MODIFY) {
		new_np = presc->rs_value.at_val.at_long;
		presc->rs_value.at_flags &= ~ATR_VFLAG_DEFLT;
		if ((err = mod_node_ncpus(pnode, new_np, actmode)) != 0)
			return (err);
	}

	/* 3. check each entry that is modified to see if it is now   */
	/*    becoming an indirect reference or was one and now isn't */
	/*    This first pass just validates the changes...	      */

	for (presc = (resource *)GET_NEXT(new->at_val.at_list);
		presc != NULL;
		presc = (resource *)GET_NEXT(presc->rs_link)) {

		if (presc->rs_value.at_flags & ATR_VFLAG_MODIFY)
			if ((err = fix_indirectness(presc, pnode, 0)) != 0)
				return (err);
	}

	/* Now do it again and actual make the needed changes since  */
	/* there are no errors to worry about			     */
	for (presc = (resource *)GET_NEXT(new->at_val.at_list);
		presc != NULL;
		presc = (resource *)GET_NEXT(presc->rs_link)) {
		if (presc->rs_value.at_flags & ATR_VFLAG_MODIFY)
			(void)fix_indirectness(presc, pnode, 1);
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		node_pcpu_action - action routine for node's pcpus (physical) resource
 *
 * @param[in]	new	-		derive props into this attribute
 * @param[in]	pobj	-	pointer to a pbsnode struct
 * @param[in]	actmode	-	action mode: "NEW" or "ALTER"
 *
 * @return	int
 * @retval	0	- success
 * @retval	nonzero	- error
 */

int
node_pcpu_action(attribute *new, void *pobj, int actmode)
{

	struct pbsnode *pnode = (struct pbsnode *)pobj;
	resource_def   *prd;
	resource       *prc;
	long		new_np;

	/* get new value of pcpus */
	new_np = new->at_val.at_long;
	pnode->nd_ncpus = new_np;

	/* now get ncpus */
	prd = find_resc_def(svr_resc_def, "ncpus", svr_resc_size);
	if (prd == 0)
		return PBSE_SYSTEM;
	prc = find_resc_entry(&pnode->nd_attr[(int)ND_ATR_ResourceAvail], prd);
	if (prc == 0) {
		return (0); /* if this error happens - ignore it */
	}
	if (((prc->rs_value.at_flags & ATR_VFLAG_SET) == 0) ||
		((prc->rs_value.at_flags & ATR_VFLAG_DEFLT) != 0)) {
		if (prc->rs_value.at_val.at_long != new_np) {
			prc->rs_value.at_val.at_long = new_np;
			prc->rs_value.at_flags |= ATR_VFLAG_SET|ATR_VFLAG_MODCACHE|ATR_VFLAG_DEFLT;
			return (mod_node_ncpus(pnode, new_np, actmode));
		}
	}
	return (0);

}

/**
 * @brief
 * 		mark_which_queues_have_nodes()
 *
 *		Mark the queue header for queues that have nodes associated with
 *		them.  This is used when looking for nodes for jobs that are in
 *		such a queue.
 *
 * @see
 * 		node_queue_action, pbsd_init and mgr_node_unset.
 *
 * @return	void
 */

void
mark_which_queues_have_nodes()
{
	int		 i;
	pbs_queue       *pque;

	/* clear "has node" flag in all queues */

	svr_quehasnodes = 0;

	pque = (pbs_queue *)GET_NEXT(svr_queues);
	while (pque != NULL) {
		pque->qu_attr[(int)QE_ATR_HasNodes].at_val.at_long = 0;
		pque->qu_attr[(int)QE_ATR_HasNodes].at_flags &= ~ATR_VFLAG_SET;
		pque->qu_attr[(int)QE_ATR_HasNodes].at_flags |= ATR_VFLAG_MODCACHE;
		pque = (pbs_queue *)GET_NEXT(pque->qu_link);
	}

	/* now (re)set flag for those queues that do have nodes */

	for (i=0; i<svr_totnodes; i++) {
		if (pbsndlist[i]->nd_pque) {
			pbsndlist[i]->nd_pque->qu_attr[(int)QE_ATR_HasNodes].at_val.at_long = 1;
			pbsndlist[i]->nd_pque->qu_attr[(int)QE_ATR_HasNodes].at_flags = ATR_VFLAG_SET | ATR_VFLAG_MODCACHE;
			svr_quehasnodes = 1;
		}
	}
}

/**
 * @brief
 * 		node_queue_action - action routine for nodes when "queue" attribute set
 *
 * @param[in]	pattr	-	attribute
 * @param[in]	pobj	-	pointer to a pbsnode struct
 * @param[in]	actmode	-	action mode: "NEW" or "ALTER"
 *
 * @return	int
 * @retval	0	- success
 * @retval	nonzero	- error
 */


int
node_queue_action(attribute *pattr, void *pobj, int actmode)
{
	struct pbsnode	*pnode;
	pbs_queue 	*pq;

	pnode = (struct pbsnode *)pobj;

	if (pattr->at_flags & ATR_VFLAG_SET) {

		pq = find_queuebyname(pattr->at_val.at_str);
		if (pq == 0) {
			return (PBSE_UNKQUE);
		} else if (pq->qu_qs.qu_type != QTYPE_Execution) {
			return (PBSE_ATTRTYPE);
		} else if ((pq->qu_attr[QA_ATR_partition].at_flags & ATR_VFLAG_SET) &&
			(pnode->nd_attr[ND_ATR_partition].at_flags & ATR_VFLAG_SET) &&
			strcmp(pq->qu_attr[QA_ATR_partition].at_val.at_str, pnode->nd_attr[ND_ATR_partition].at_val.at_str)) {
			return PBSE_PARTITION_NOT_IN_QUE;
		}
		else {
			pnode->nd_pque = pq;
		}
	} else {
		pnode->nd_pque = NULL;
	}
	mark_which_queues_have_nodes();
	return 0;
}
/**
 * @brief
 * 		set_node_host_name returns 0 if actmode is 1, otherwise PBSE_ATTRRO
 */
int
set_node_host_name(attribute *pattr, void *pobj, int actmode)
{
	if (actmode == 1)
		return 0;
	else
		return PBSE_ATTRRO;
}
/**
 * @brief
 * 		set_node_host_name returns 0 if actmode is 1, otherwise PBSE_ATTRRO
 */
int
set_node_mom_port(attribute *pattr, void *pobj, int actmode)
{
	if (actmode == 1)
		return 0;
	else
		return PBSE_ATTRRO;
}

/**
 * @brief
 * 		Returns true (1) if none of the following bits are set:
 *			OFFLINE, OFFLINE_BY_MOM, DOWN, DELETED, STALE
 *		otherwise return false (0) for the node being "down"
 *
 a @param[in]	nodename - name of the node to check
 *
 * @return int
 * @retval 0	- means vnode is not up
 * @retval 1	- means vnode is up
 */

int
is_vnode_up(char *nodename)
{
	struct pbsnode	*np;

	np = find_nodebyname(nodename);
	if ((np == NULL) ||
		((np->nd_state & (INUSE_OFFLINE | INUSE_OFFLINE_BY_MOM | INUSE_DOWN | INUSE_DELETED | INUSE_STALE)) != 0))
		return 0;	/* vnode is not up */
	else
		return 1;	/* vnode is up */
}

/**
 * @brief
 * 		decode_Mom_list - decode a comma string which specifies a list of Mom/host
 *		names into an attr of type ATR_TYPE_ARST
 *		Each host name is fully qualified before being added into the array
 *
 * @param[in,out]	patr	-	attribute
 * @param[in]		name	-	attribute name
 * @param[in]	rescn	-	resource name, unused here.
 * @param[in]	val	-	comma separated string of substrings
 *
 *	Returns: 0 if ok,
 *		>0 error number if an error occured,
 *		*patr members set
 */

int
decode_Mom_list(struct attribute *patr, char *name, char *rescn, char *val)
{
	int			  rc;
	int			  ns;
	int			  i = 0;
	char			 *p;
	char			  buf[PBS_MAXHOSTNAME+1];
	static char		**str_arr = NULL;
	static long int		  str_arr_len = 0;
	attribute		  new;
	char			 *tmp_fqdn;

	if ((val == NULL) || (strlen(val) == 0) || count_substrings(val, &ns)) {
		node_attr_def[(int)ND_ATR_Mom].at_free(patr);
		clear_attr(patr, &node_attr_def[(int)ND_ATR_Mom]);
		/* ATTR_VFLAG_SET is cleared now */
		patr->at_flags &= ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
		return (0);
	}

	if (patr->at_flags & ATR_VFLAG_SET) {
		node_attr_def[(int)ND_ATR_Mom].at_free(patr);
		clear_attr(patr, &node_attr_def[(int)ND_ATR_Mom]);
	}

	if (str_arr_len == 0) {
		str_arr = malloc(((2 * ns) + 1) * sizeof(char *));
		str_arr_len = (2 * ns) + 1;
	} else if (str_arr_len < ns) {
		char **new_str_arr;
		new_str_arr = realloc(str_arr, ((2 * ns) + 1) * sizeof(char *));
		/* str_arr will be untouched if realloc failed */
		if (!new_str_arr)
			return PBSE_SYSTEM;
		str_arr = new_str_arr;
		str_arr_len = (2 * ns) + 1;
	}
	/* Filling node list to a array, this has been done outside the
	 * second for loop since, parse_comma_string() is being again called internally by
	 * decode_arst() that alters the static variable in parse_comma_string().
	 */
	str_arr[0]=NULL;
	p = parse_comma_string(val);
	for (i = 0; (str_arr[i] = p) != NULL; i++)
		p = parse_comma_string(NULL);

	for (i = 0; (p = str_arr[i]) != NULL; i++) {
		clear_attr(&new, &node_attr_def[(int)ND_ATR_Mom]);
		/* try the cache first */
		tmp_fqdn = NULL;
		if ((momfqdn_tree == NULL) || ((tmp_fqdn = (char *) find_tree(momfqdn_tree, p)) == NULL)) {
			/* cache miss, do name resolution */
			if (get_fullhostname(p, buf, (sizeof(buf) - 1)) != 0)
				strncpy(buf, p, (sizeof(buf) - 1));
		} else {
			/* found it in the cache */
			strncpy(buf, tmp_fqdn, (sizeof(buf) - 1));
		}
		rc = decode_arst(&new, ATTR_NODE_Mom, NULL, buf);
		if (rc != 0)
			continue;
		set_arst(patr, &new, INCR);
		free_arst(&new);
		if (tmp_fqdn == NULL) {
			/* there was a cache miss, add resolved name to cache.
			 * Ignore all errors while adding to the cache.
			 */
			if (momfqdn_tree == NULL)
				momfqdn_tree = create_tree(AVL_NO_DUP_KEYS, 0);

			if (momfqdn_tree != NULL) {
				if ((tmp_fqdn = strdup(buf)) != NULL)
					tree_add_del(momfqdn_tree, p,
						tmp_fqdn, TREE_OP_ADD);
			}
		}
	}

	return (0);
}

/**
 * @brief
 * 		remember the node topology information reported by a node's MoM
 *
 * @param[in]	node_name	-	the name of the node
 * @param[in]	topology	-	topology information from node's MoM
 *
 * @return	void
 *
 * @par MT-Safe:	no
 *
 * @par Note:
 *		Information is recorded in the $PBS_HOME/server_priv/node_topology/
 *		directory, one file per node.  The information in these files may be
 *		consumed by the hwloc lstopo command using
 *		lstopo -i <node topology file path>
 *
 * @see	http://www.open-mpi.org/projects/hwloc/doc/v1.3/tools.php, lstopo(1)
 */
static void
record_node_topology(char *node_name, char *topology)
{
	char		path[MAXPATHLEN + 1];
	int		fd;
	int		topology_len;
	static char	topology_dir[] = "topology";
	static char	msg_topologydiroverflow[] = "unexpected overflow "
		"creating node topology "
	"directory";
	static char	msg_mkdirfail[] = "failed to create topology directory";
	static char	msg_topologypathoverflow[] = "unexpected overflow "
		"creating node topology "
	"file %s";
	static char	msg_createpathfail[] = "failed to open path to node "
		"topology file for node %s";
	static char	msg_writepathfail[] = "failed to write node topology "
		"for node %s";
	static char	msg_notdir[] = "topology directory path exists but is "
		"not a directory";
	struct stat	sb;

	if (snprintf(path, sizeof(path), "%s/server_priv/%s",
		pbs_conf.pbs_home_path,
		topology_dir) >= sizeof(path)) {
		sprintf(log_buffer, "%s", msg_topologydiroverflow);
		log_event(PBSEVENT_DEBUG3,
			PBS_EVENTCLASS_SERVER,
			LOG_DEBUG, msg_daemonname,
			log_buffer);
		return;
	}
	if (stat(path, &sb) == -1) {
		/* can't stat path - assume it does not exist */
		if (mkdir(path, S_IRWXU) == -1) {
			sprintf(log_buffer, "%s", msg_mkdirfail);
			log_err(errno, __func__, log_buffer);
			return;
		}
#ifdef	WIN32
		secure_file(path, NULL, 0);
#endif
	} else if (!S_ISDIR(sb.st_mode)) {
		/* path exists but is not a directory */
		sprintf(log_buffer, "%s", msg_notdir);
		log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
			msg_daemonname, log_buffer);
		return;
	}

	/* path exists and is a directory */
	if (snprintf(path, sizeof(path), "%s/server_priv/%s/%s",
		pbs_conf.pbs_home_path,
		topology_dir, node_name) >= sizeof(path)) {
		sprintf(log_buffer, msg_topologypathoverflow, node_name);
		log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
			msg_daemonname, log_buffer);
		return;
	}
	if ((fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR)) == -1) {
		sprintf(log_buffer, msg_createpathfail,
			node_name);
		log_err(errno, __func__, log_buffer);
		return;
	}
#ifdef	WIN32
	secure_file(path, "Administrators",
		READS_MASK|WRITES_MASK|STANDARD_RIGHTS_REQUIRED);
#endif
	topology_len = strlen(topology);
	if (write(fd, topology, topology_len) != topology_len) {
		sprintf(log_buffer, msg_writepathfail, node_name);
		log_err(errno, __func__, log_buffer);
	}
	(void) close(fd);
}

/**
 * @brief
 * 		remove the node topology information for the given node name
 * @see
 * 		effective_node_delete
 *
 * @param[in]	node_name	-	the name of the node
 *
 * @return	void
 *
 * @par MT-Safe:	no
 *
 */
static void
remove_node_topology(char *node_name)
{
	char		path[MAXPATHLEN + 1];
	static char	topology_dir[] = "topology";
	static char	msg_topologyfileoverflow[] = "unexpected overflow "
		"removing topology "
	"file for node %s";
	static char	msg_unlinkfail[] = "unlink of topology file for "
		"node %s failed";

	if (snprintf(path, sizeof(path), "%s/server_priv/%s/%s",
		pbs_conf.pbs_home_path,
		topology_dir, node_name) >= sizeof(path)) {
		sprintf(log_buffer, msg_topologyfileoverflow,
			node_name);
		log_event(PBSEVENT_DEBUG3,
			PBS_EVENTCLASS_SERVER,
			LOG_DEBUG, msg_daemonname,
			log_buffer);
	} else if (unlink(path) == -1) {
		sprintf(log_buffer, msg_unlinkfail, node_name);
		log_err(errno, __func__, log_buffer);
	}
}

/**
 * @brief
 * 		set the node topology attribute
 *
 * @param[in]	new		-	pointer to new attribute
 * @param[in]	pobj	-	pointer to parent object of the attribute (in this case,
 *							a pbsnode)
 * @param[in]	op		-	the attribute operation being performed
 *
 * @return	int
 * @retval	PBSE_NONE	- success
 * @retval	nonzero		- PBSE_* error
 *
 * @par MT-Safe:	no
 *
 * @par	Note
 * 		This attribute is versioned (by an arbitrary string terminating
 *		in a ':' character).  In the case of the NODE_TOPOLOGY_TYPE_HWLOC
 *		version, the value following the version string is the topology
 *		information captured by the MoM via hwloc_topology_load() and it
 *		is saved in $PBS_HOME/server_priv/ by record_node_topology().
 *
 * @see
 * 		record_node_topology()
 *
 * @par Side Effects:
 *		None
 */
int
set_node_topology(attribute *new, void *pobj, int op)
{
#ifdef NAS /* localmod 035 */
	return (PBSE_NONE);
#else

	int		rc = PBSE_NONE;
	struct pbsnode	*pnode = ((pbsnode *) pobj);
	attribute_def	*pnadl = &node_attr_def[(int) ND_ATR_License];
	attribute_def	*pnadli = &node_attr_def[(int) ND_ATR_LicenseInfo];
	attribute	*ppnl = &pnode->nd_attr[(int) ND_ATR_License];
	attribute	*ppnli = &pnode->nd_attr[(int) ND_ATR_LicenseInfo];
	char		*valstr;
	int		node_nsockets;
	ntt_t		ntt;
	enum		licensing_backend lb = LIC_SOCKETS;
	char		license_type[8];
	char		msg_unknown_topology_type[] = "unknown topology type in "
					"topology attribute for node %s";
	char		msg_sockets_initialized[] = "node %s already assigned "
				"license for %d %s%s, nd_attr[ND_ATR_License] set to %c";
	char		msg_licensed_with_sockets[] = "node %s assigned %d "
					"%s license%s, nd_attr[ND_ATR_License] set to %c";
	char		msg_sockets_mismatch[] = "node %s:  node reporting %d "
						"%s licenses, has licenses for %d";
	strcpy(license_type, "node");

	switch (op) {

		case ATR_ACTION_NOOP:
			break;

		case ATR_ACTION_NEW:
		case ATR_ACTION_ALTER:

			valstr = new->at_val.at_str;

			/*
			 *	Currently two topology types are known;  if it's one
			 *	we expect, step over it to the actual value we care
			 *	about.
			 */
			if (strstr(valstr, NODE_TOPOLOGY_TYPE_HWLOC) == valstr) {
				valstr += strlen(NODE_TOPOLOGY_TYPE_HWLOC);
				ntt = tt_hwloc;
			} else if (strstr(valstr, NODE_TOPOLOGY_TYPE_CRAY) == valstr) {
				valstr += strlen(NODE_TOPOLOGY_TYPE_CRAY);
				ntt = tt_Cray;
			} else if (strstr(valstr, NODE_TOPOLOGY_TYPE_WIN) == valstr) {
				valstr += strlen(NODE_TOPOLOGY_TYPE_WIN);
				ntt = tt_Win;
			} else {
				sprintf(log_buffer, msg_unknown_topology_type,
					pnode->nd_name);
				log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER,
					LOG_DEBUG, __func__, log_buffer);
				return (PBSE_INTERNAL);
			}

			record_node_topology(pnode->nd_name, valstr);
			node_nsockets = nsockets_from_topology(valstr, ntt);
			lb = LIC_NODES;

			if (licstate_is_up(LIC_SOCKETS)) {
				lb = LIC_SOCKETS;
				strcpy(license_type, "socket");
			}
			
			if ((ppnl->at_flags & ATR_VFLAG_SET) && (ppnl->at_val.at_char == ND_LIC_TYPE_locked)) {
				/* node socket licenses previously initialized */
				if (ppnli->at_val.at_long != node_nsockets) {
					/*
					 *	This node's socket licensing information
					 *	has already been initialized;  the node
					 *	is now reporting different socket count than have
					 *	previously been licensed.  try to license the node
					 *	if enough licenses are available else we reset the 
					 *	node's License and LicenseInfo attributes.
					 */
					clear_attr(ppnl, pnadl);
					pnode->nd_modified |= NODE_UPDATE_OTHERS;
					sockets_release(ppnli->at_val.at_long);
					if (sockets_consume(node_nsockets) == 0) {
						ppnl->at_val.at_char = ND_LIC_TYPE_locked;
						ppnl->at_flags  |= ATR_VFLAG_SET |
							ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
					}
					sprintf(log_buffer, msg_sockets_mismatch,
						pnode->nd_name, node_nsockets, license_type,
						ppnli->at_val.at_long);
					log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER,
						LOG_DEBUG, __func__, log_buffer);
					ppnli->at_val.at_long = node_nsockets;
					ppnli->at_flags  |= ATR_VFLAG_SET |
					ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
				} else if (licstate_is_up(lb)) {
					/*
					 *	A valid socket license file exists and
					 *	the node claims no more sockets than
					 *	licensed before - mark it as licensed.
					 *
					 *	Note that in contrast to the "else"
					 *	case below, no socket licenses need be
					 *	consumed here - since the node was
					 *	already licensed, that accounting was
					 *	done in pbsd_init(), q.v.
					 */
					/* mark node as using node-locked licenses */
					clear_attr(ppnl, pnadl);
					ppnl->at_val.at_char = ND_LIC_TYPE_locked;
					ppnl->at_flags  |= ATR_VFLAG_SET |
						ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
					pnode->nd_modified |= NODE_UPDATE_OTHERS;
					sprintf(log_buffer, msg_sockets_initialized,
						pnode->nd_name, node_nsockets, license_type,
						node_nsockets == 1 ? "" : "s", ND_LIC_TYPE_locked);
					log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER,
						LOG_DEBUG, __func__, log_buffer);
				}
			} else if ((node_nsockets > 0) && licstate_is_up(lb) &&
					(sockets_consume(node_nsockets) == 0)) {
				/*
				 *	Not previously initialized and sufficient
				 *	licenses - mark node as using node-locked
				 *	licenses ...
				 */
				clear_attr(ppnl, pnadl);
				ppnl->at_val.at_char = ND_LIC_TYPE_locked;
				ppnl->at_flags  |= ATR_VFLAG_SET |
					ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
				/*
				 *	... and remember the number of socket
				 *	licenses consumed.
				 */
				clear_attr(ppnli, pnadli);
				ppnli->at_val.at_long = node_nsockets;
				ppnli->at_flags  |= ATR_VFLAG_SET |
					ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
				sprintf(log_buffer, msg_licensed_with_sockets,
					pnode->nd_name, node_nsockets, license_type,
					node_nsockets == 1 ? "" : "s", ND_LIC_TYPE_locked);
				log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SERVER,
					LOG_DEBUG, __func__, log_buffer);

				pnode->nd_modified |= NODE_UPDATE_OTHERS;
			} else {
				/*
				 * In this case, we don't have enough license for this node.
				 * However, the nsockets is saved for future reference.
				 * We don't have to re-evaluate topology if license file
				 * is updated using qmgr
				 */
				clear_attr(ppnl, pnadl);
				clear_attr(ppnli, pnadli);
				ppnli->at_val.at_long = node_nsockets;
				ppnli->at_flags  |= ATR_VFLAG_SET |
					ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
			}
			break;

		case ATR_ACTION_RECOV:
		case ATR_ACTION_FREE:
		default:
			rc = PBSE_INTERNAL;
	}

	if (rc == PBSE_NONE) {
		new->at_flags |=
			ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	}
	return rc;
#endif /* localmod 035 */
}

/**
 * @brief chk_vnode_pool - action routine for a node's vnode_pool attribute
 *      Does several things:
 *      1. Verifies that there is only one Mom being pointed to
 *      2. Verifies in the Mom structure that this is the zero-th node
 *
 * @param[in] new - ptr to attribute being modified with new value
 * @param[in] pobj - ptr to parent object (pbs_node)
 * @param[in] actmode - type of action: recovery, new node, or altering
 *
 * @return error code
 * @retval PBSE_NONE  (zero) - on success
 * @retval PBSE_*  (non zero) - on error
 */
int
chk_vnode_pool (attribute *new, void *pobj, int actmode)
{
	static char     id[] = "chk_vnode_pool";
	int		pool = -1;

	switch (actmode) {
		case ATR_ACTION_NEW:
		case ATR_ACTION_RECOV:

			pool = new->at_val.at_long;
			sprintf(log_buffer, "vnode_pool value is = %d", pool);
			log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_NODE, LOG_DEBUG, id, log_buffer);
			if (pool <= 0) {
				log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
					LOG_WARNING, id, "invalid vnode_pool provided");
				return (PBSE_BADATVAL);
			}
			break;

		case ATR_ACTION_ALTER:
			log_event(PBSEVENT_ADMIN, PBS_EVENTCLASS_SERVER,
				LOG_DEBUG, id, "Unsupported actions for vnode_pool");
			return (PBSE_IVALREQ);

		default:
			log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER,
				LOG_DEBUG, id, "Unsupported actions for vnode_pool");
			return (PBSE_INTERNAL);
	}
	return PBSE_NONE;
}

/**
 * @brief
 * 		action routine for the queue's "partition" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_node_partition(attribute *pattr, void *pobj, int actmode)
{
	struct pbsnode *pnode;
	pbs_queue 	*pq;

	pnode = (pbsnode *)pobj;

	if (pnode->nd_attr[(int)ND_ATR_Queue].at_flags & ATR_VFLAG_SET) {
		pq = find_queuebyname(pnode->nd_attr[(int)ND_ATR_Queue].at_val.at_str);
		if (pq == 0)
			return PBSE_UNKQUE;
		if (pq->qu_attr[QA_ATR_partition].at_flags & ATR_VFLAG_SET &&
				pattr->at_flags & ATR_VFLAG_SET) {
			if (strcmp(pq->qu_attr[QA_ATR_partition].at_val.at_str, pattr->at_val.at_str) != 0)
				return PBSE_QUE_NOT_IN_PARTITION;
		}
	}

	return PBSE_NONE;
}
