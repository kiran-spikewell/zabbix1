/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#ifndef ZABBIX_PREPROCESSING_H
#define ZABBIX_PREPROCESSING_H

#include "preproc.h"
#include "zbxpreproc.h"
#include "zbxipcservice.h"

#define ZBX_IPC_SERVICE_PREPROCESSING	"preprocessing"

#define ZBX_IPC_PREPROCESSOR_WORKER			1
#define ZBX_IPC_PREPROCESSOR_REQUEST			2
#define ZBX_IPC_PREPROCESSOR_RESULT			3
#define ZBX_IPC_PREPROCESSOR_QUEUE			4
#define ZBX_IPC_PREPROCESSOR_TEST_REQUEST		5
#define ZBX_IPC_PREPROCESSOR_TEST_RESULT		6
#define ZBX_IPC_PREPROCESSOR_DIAG_STATS			7
#define ZBX_IPC_PREPROCESSOR_DIAG_STATS_RESULT		8
#define ZBX_IPC_PREPROCESSOR_TOP_ITEMS			9
#define ZBX_IPC_PREPROCESSOR_TOP_ITEMS_RESULT		10
#define ZBX_IPC_PREPROCESSOR_TOP_OLDEST_PREPROC_ITEMS	11
#define ZBX_IPC_PREPROCESSOR_DEP_REQUEST		12
#define ZBX_IPC_PREPROCESSOR_DEP_REQUEST_CONT		13
#define ZBX_IPC_PREPROCESSOR_DEP_NEXT			14
#define ZBX_IPC_PREPROCESSOR_DEP_RESULT			15
#define ZBX_IPC_PREPROCESSOR_DEP_RESULT_CONT		16

/* item value data used in preprocessing manager */
typedef struct
{
	zbx_uint64_t		itemid;		 /* item id */
	zbx_uint64_t		hostid;		 /* host id */
	unsigned char		item_value_type; /* item value type */
	AGENT_RESULT		*result;	 /* item value (if any) */
	zbx_timespec_t		*ts;		 /* timestamp of a value */
	char			*error;		 /* error message (if any) */
	unsigned char		item_flags;	 /* item flags */
	unsigned char		state;		 /* item state */
}
zbx_preproc_item_value_t;

zbx_uint32_t	zbx_preprocessor_unpack_value(zbx_preproc_item_value_t *value, unsigned char *data);

void	zbx_preprocessor_unpack_test_request(zbx_pp_item_preproc_t *preproc, zbx_variant_t *value, zbx_timespec_t *ts,
		const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_test_result(unsigned char **data, const zbx_pp_result_t *results,
		int results_num, const zbx_pp_history_t *history);

void	zbx_preprocessor_unpack_test_result(zbx_vector_pp_result_ptr_t *results, zbx_pp_history_t *history,
		const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_diag_stats(unsigned char **data, int total, int queued, int processing, int done,
		int pending);

void	zbx_preprocessor_unpack_diag_stats(int *total, int *queued, int *processing, int *done,
		int *pending, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_top_items_request(unsigned char **data, int limit);

void	zbx_preprocessor_unpack_top_request(int *limit, const unsigned char *data);

zbx_uint32_t	zbx_preprocessor_pack_top_items_result(unsigned char **data, zbx_preproc_item_stats_t **items,
		int items_num);

void	zbx_preprocessor_unpack_top_result(zbx_vector_ptr_t *items, const unsigned char *data);

ZBX_PTR_VECTOR_DECL(ipcmsg, zbx_ipc_message_t *)

/* packed field data description */
typedef struct
{
	const void	*value;	/* value to be packed */
	zbx_uint32_t	size;	/* size of a value (can be 0 for strings) */
	unsigned char	type;	/* field type */
}
zbx_packed_field_t;

#endif /* ZABBIX_PREPROCESSING_H */
