/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
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

#include "pg_manager.h"
#include "pg_service.h"

#include "zbxcommon.h"
#include "zbxdbhigh.h"
#include "zbxself.h"
#include "zbxnix.h"
#include "zbxcacheconfig.h"

#define PGM_STATUS_CHECK_INTERVAL	5

/******************************************************************************
 *                                                                            *
 * Purpose: initialize proxy group manager                                    *
 *                                                                            *
 ******************************************************************************/
static void	pgm_init(zbx_pg_cache_t *cache)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;
	zbx_uint64_t	map_revision = 0;

	result = zbx_db_select("select nextid from ids where table_name='host_proxy' and field_name='revision'");

	if (NULL != (row = zbx_db_fetch(result)))
		ZBX_DBROW2UINT64(map_revision, row[0]);

	zbx_db_free_result(result);

	pg_cache_init(cache, map_revision);
}

/******************************************************************************
 *                                                                            *
 * Purpose: update proxy group cache from configuration cache                 *
 *                                                                            *
 ******************************************************************************/
static void	pgm_update_groups(zbx_pg_cache_t *cache)
{
	zbx_uint64_t	old_revision = cache->group_revision;

	if (SUCCEED != zbx_dc_get_proxy_groups(&cache->groups, &cache->group_revision))
		return;

	zbx_hashset_iter_t	iter;
	zbx_pg_group_t		*group;

	zbx_hashset_iter_reset(&cache->groups, &iter);
	while (NULL != (group = (zbx_pg_group_t *)zbx_hashset_iter_next(&iter)))
	{
		if (0 == group->sync_revision)
		{
			pg_group_clear(group);
			zbx_hashset_iter_remove(&iter);
			continue;
		}

		if (old_revision >= group->revision)
			continue;

		pg_cache_queue_group_update(cache, group);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: update host-proxy group assignments from database                 *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_get_hosts(zbx_pg_cache_t *cache)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;

	result = zbx_db_select("select hostid,proxy_groupid from hosts where proxy_groupid is not null");

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t	hostid, proxy_groupid;
		zbx_pg_group_t	*group;

		ZBX_DBROW2UINT64(hostid, row[0]);
		ZBX_DBROW2UINT64(proxy_groupid, row[1]);

		if (NULL == (group = (zbx_pg_group_t *)zbx_hashset_search(&cache->groups, &proxy_groupid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_uint64_append(&group->hostids, hostid);
	}
	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: update proxy cache from database                                  *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_get_proxies(zbx_pg_cache_t *cache)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;
	int		clock = 0;
	zbx_pg_proxy_t *proxy;

	result = zbx_db_select("select p.proxyid,p.proxy_groupid,rt.lastaccess,p.name"
				" from proxy p,proxy_rtdata rt"
				" where proxy_groupid is not null"
					" and p.proxyid=rt.proxyid");

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t	proxyid, proxy_groupid;
		zbx_pg_group_t	*group;

		ZBX_DBROW2UINT64(proxyid, row[0]);
		ZBX_DBROW2UINT64(proxy_groupid, row[1]);

		if (NULL == (group = (zbx_pg_group_t *)zbx_hashset_search(&cache->groups, &proxy_groupid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* the proxy lastacess is temporary stored in it's firstaccess */
		proxy = pg_cache_group_add_proxy(cache, group, proxyid, row[3], atoi(row[2]));

		if (proxy->firstaccess < clock)
			proxy->firstaccess = clock;
	}
	zbx_db_free_result(result);

	/* calculate proxy status by finding the highest proxy lastaccess time */
	/* and using it as current timestamp                                   */

	zbx_hashset_iter_t	iter;
	zbx_hashset_iter_reset(&cache->proxies, &iter);

	while (NULL != (proxy = (zbx_pg_proxy_t *)zbx_hashset_iter_next(&iter)))
	{
		if (clock - proxy->firstaccess >= proxy->group->failover_delay)
			proxy->status = ZBX_PG_PROXY_STATUS_OFFLINE;
		else
			proxy->status = ZBX_PG_PROXY_STATUS_ONLINE;

		proxy->firstaccess = 0;

		/* WDN: force online status */
		proxy->status = ZBX_PG_PROXY_STATUS_ONLINE;
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: update host-proxy mapping from database                           *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_get_hpmap(zbx_pg_cache_t *cache)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;

	result = zbx_db_select("select hostid,proxyid,revision from host_proxy");

	while (NULL != (row = zbx_db_fetch(result)))
	{
		zbx_uint64_t	hostid, proxyid, revision;
		zbx_pg_proxy_t	*proxy;

		ZBX_DBROW2UINT64(proxyid, row[1]);
		ZBX_DBROW2UINT64(hostid, row[0]);

		if (NULL == (proxy = (zbx_pg_proxy_t *)zbx_hashset_search(&cache->proxies, &proxyid)))
		{
			pg_cache_set_host_proxy(cache, hostid, 0);
			continue;
		}

		ZBX_STR2UINT64(revision, row[2]);

		zbx_pg_host_t	host_local = {
				.hostid = hostid,
				.proxyid = proxyid,
				.revision = revision
			}, *host;

		host = (zbx_pg_host_t *)zbx_hashset_insert(&cache->hpmap, &host_local, sizeof(host_local));
		zbx_vector_pg_host_ptr_append(&proxy->hosts, host);

		/* proxies with assigned hosts in most cases were online before restart */
		proxy->status = ZBX_PG_PROXY_STATUS_ONLINE;

	}
	zbx_db_free_result(result);

	/* queue unmapped hosts for proxy assignment */

	zbx_hashset_iter_t	iter;
	zbx_pg_group_t		*group;

	zbx_hashset_iter_reset(&cache->groups, &iter);
	while (NULL != (group = (zbx_pg_group_t *)zbx_hashset_iter_next(&iter)))
	{
		for (int i = 0; i < group->hostids.values_num; i++)
		{
			if (NULL == zbx_hashset_search(&cache->hpmap, &group->hostids.values[i]))
				zbx_vector_uint64_append(&group->new_hostids, group->hostids.values[i]);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: update proxy and proxy group status in cache                      *
 *                                                                            *
 ******************************************************************************/
static void	pgm_update_status(zbx_pg_cache_t *cache)
{
	pg_cache_lock(cache);

	zbx_dc_get_group_proxy_lastaccess(&cache->proxies);

	zbx_hashset_iter_t	iter;
	zbx_pg_proxy_t		*proxy;
	int			now;

	now = time(NULL);

	zbx_hashset_iter_reset(&cache->proxies, &iter);
	while (NULL != (proxy = (zbx_pg_proxy_t *)zbx_hashset_iter_next(&iter)))
	{
		/* WDN: override proxy last access for debugging */
		proxy->lastaccess = now;

		int	status = ZBX_PG_PROXY_STATUS_UNKNOWN;

		if (now - proxy->lastaccess >= proxy->group->failover_delay)
		{
			if (now - cache->startup_time >= proxy->group->failover_delay)
			{
				status = ZBX_PG_PROXY_STATUS_OFFLINE;
				proxy->firstaccess = 0;
			}
		}
		else
		{
			if (0 == proxy->firstaccess)
				proxy->firstaccess = proxy->lastaccess;

			if (now - proxy->firstaccess >= proxy->group->failover_delay)
				status = ZBX_PG_PROXY_STATUS_ONLINE;
		}

		if (ZBX_PG_PROXY_STATUS_UNKNOWN == status || proxy->status == status)
			continue;

		proxy->status = status;
		pg_cache_queue_group_update(cache, proxy->group);
	}

	for (int i = 0; i < cache->group_updates.values_num; i++)
	{
		zbx_pg_group_t	*group = cache->group_updates.values[i];
		int		online = 0, healthy = 0;

		for (int j = 0; j < group->proxies.values_num; j++)
		{
			if (ZBX_PG_PROXY_STATUS_ONLINE == group->proxies.values[i]->status)
			{
				online++;

				if (now - group->proxies.values[i]->lastaccess + PGM_STATUS_CHECK_INTERVAL <
						group->failover_delay)
				{
					healthy++;
				}
			}
		}

		int	status = group->status;

		switch (group->status)
		{
			case ZBX_PG_GROUP_STATUS_UNKNOWN:
				status = ZBX_PG_GROUP_STATUS_ONLINE;
				ZBX_FALLTHROUGH;
			case ZBX_PG_GROUP_STATUS_ONLINE:
				if (group->min_online > healthy)
					status = ZBX_PG_GROUP_STATUS_DECAY;
				break;
			case ZBX_PG_GROUP_STATUS_OFFLINE:
				if (group->min_online <= online)
					status = ZBX_PG_GROUP_STATUS_RECOVERY;
				break;
			case ZBX_PG_GROUP_STATUS_RECOVERY:
				if (group->min_online > healthy)
				{
					status = ZBX_PG_GROUP_STATUS_DECAY;
				}
				else if (now - group->status_time > group->failover_delay ||
						group->proxies.values_num == online)
				{
					status = ZBX_PG_GROUP_STATUS_RECOVERY;
				}
				break;
			case ZBX_PG_GROUP_STATUS_DECAY:
				if (group->min_online <= healthy)
					status = ZBX_PG_GROUP_STATUS_ONLINE;
				else if (group->min_online > online)
					status = ZBX_PG_GROUP_STATUS_OFFLINE;
				break;
		}

		if (status != group->status)
		{
			group->status = status;
			group->status_time = now;
			group->flags = ZBX_PG_GROUP_UPDATE_STATUS;
		}
	}

	pg_cache_unlock(cache);
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush proxy group updates to database                             *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_group_updates(char **sql, size_t *sql_alloc, size_t *sql_offset,
		zbx_vector_pg_update_t *groups)
{
	for (int i = 0; i < groups->values_num; i++)
	{
		if (0 == (groups->values[i].flags & ZBX_PG_GROUP_UPDATE_STATUS))
			continue;

		zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update proxy_group set status=%d where proxy_groupid=" ZBX_FS_UI64 ";\n",
				groups->values[i].status, groups->values[i].proxy_groupid);

		zbx_db_execute_overflowed_sql(sql, sql_alloc, sql_offset);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush host-proxy mapping changes to database                      *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_host_proxy_updates(char **sql, size_t *sql_alloc, size_t *sql_offset,
		zbx_vector_pg_host_t *hosts)
{
	for (int i = 0; i < hosts->values_num; i++)
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update host_proxy set proxyid=" ZBX_FS_UI64 ",revision=" ZBX_FS_UI64
					" where hostid=" ZBX_FS_UI64 ";\n",
				hosts->values[i].proxyid, hosts->values[i].revision, hosts->values[i].hostid);

		zbx_db_execute_overflowed_sql(sql, sql_alloc, sql_offset);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: delete removed host-proxy mapping records from database           *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_host_proxy_deletes(char **sql, size_t *sql_alloc, size_t *sql_offset,
		zbx_vector_pg_host_t *hosts)
{
	if (0 == hosts->values_num)
		return;

	zbx_vector_uint64_t	hostids;

	zbx_vector_uint64_create(&hostids);

	for (int i = 0; i < hosts->values_num; i++)
		zbx_vector_uint64_append(&hostids, hosts->values[i].hostid);

	zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "delete from host_proxy where ");
	zbx_db_add_condition_alloc(sql, sql_alloc, sql_offset, "hostid", hostids.values, hostids.values_num);
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ";\n");

	zbx_db_execute_overflowed_sql(sql, sql_alloc, sql_offset);

	zbx_vector_uint64_destroy(&hostids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: get record identifiers from database and lock them                *
 *                                                                            *
 * Parameters: ids   - [IN] vector with identifier to lock                    *
 *             table - [IN] target table                                      *
 *             field - [IN] record identifier field name                      *
 *             index - [OUT] locked identifiers                               *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_get_recids_for_update(zbx_vector_uint64_t *ids, const char *table, const char *field,
		zbx_hashset_t *index)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;
	zbx_uint64_t	id;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;

	zbx_vector_uint64_sort(ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select %s from %s where ", field, table);
	zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, field, ids->values, ids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ZBX_FOR_UPDATE);

	result = zbx_db_select("%s", sql);
	zbx_free(sql);

	while (NULL != (row = zbx_db_fetch(result)))
	{
		ZBX_DBROW2UINT64(id, row[0]);
		zbx_hashset_insert(index, &id, sizeof(id));
	}

	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush new host-mapping record batch to database                   *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_host_proxy_insert_batch(zbx_pg_host_t *hosts, int hosts_num)
{
	zbx_vector_uint64_t	hostids, proxyids;
	zbx_hashset_t		host_index, proxy_index;

	zbx_vector_uint64_create(&hostids);
	zbx_vector_uint64_create(&proxyids);

	zbx_hashset_create(&host_index, hosts_num, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_hashset_create(&proxy_index, hosts_num, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	for (int i = 0; i < hosts_num; i++)
	{
		zbx_vector_uint64_append(&hostids, hosts[i].hostid);
		zbx_vector_uint64_append(&proxyids, hosts[i].proxyid);
	}

	pgm_db_get_recids_for_update(&hostids, "hosts", "hostid", &host_index);
	pgm_db_get_recids_for_update(&proxyids, "proxy", "proxyid", &proxy_index);

	zbx_db_insert_t	db_insert;

	zbx_db_insert_prepare(&db_insert, "host_proxy", "hostproxyid", "hostid", "proxyid", "revision", NULL);

	for (int i = 0; i < hosts_num; i++)
	{
		if (NULL == zbx_hashset_search(&host_index, &hosts[i].hostid))
			continue;

		if (NULL == zbx_hashset_search(&proxy_index, &hosts[i].proxyid))
			continue;

		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), hosts[i].hostid, hosts[i].proxyid,
				hosts[i].revision);
	}

	zbx_db_insert_autoincrement(&db_insert, "hostproxyid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	zbx_hashset_destroy(&proxy_index);
	zbx_hashset_destroy(&host_index);

	zbx_vector_uint64_destroy(&proxyids);
	zbx_vector_uint64_destroy(&hostids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush new host-mapping records to database                        *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_host_proxy_inserts(zbx_vector_pg_host_t *hosts)
{
#define PGM_INSERT_BATCH_SIZE	1000
	zbx_db_insert_t	db_insert;

	zbx_db_insert_prepare(&db_insert, "host_proxy", "hostproxyid", "hostid", "proxyid", "revision", NULL);

	for (int i = 0; i < hosts->values_num; i += PGM_INSERT_BATCH_SIZE)
	{
		int	size = hosts->values_num - i;

		if (PGM_INSERT_BATCH_SIZE < size)
			size = PGM_INSERT_BATCH_SIZE;

		pgm_db_flush_host_proxy_insert_batch(hosts->values + i, size);
	}

	zbx_db_insert_autoincrement(&db_insert, "hostproxyid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

#undef PGM_INSERT_BATCH_SIZE
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush host-proxy mapping revision to database                     *
 *                                                                            *
 ******************************************************************************/
static void	pgm_db_flush_host_proxy_revision(zbx_uint64_t revision)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;

	result = zbx_db_select("select nextid from ids where table_name='host_proxy' and field_name='revision'");

	if (NULL == (row = zbx_db_fetch(result)))
	{
		zbx_db_insert_t	db_insert;

		zbx_db_insert_prepare(&db_insert, "ids", "table_name", "field_name", "nextid", NULL);
		zbx_db_insert_add_values(&db_insert, "host_proxy", "revision", revision);
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}
	else
	{
		zbx_db_execute("update ids set nextid=" ZBX_FS_UI64
				" where table_name='host_proxy' and field_name='revision'", revision);
	}

	zbx_db_free_result(result);
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush host-proxy mapping revision to configuration cache          *
 *                                                                            *
 ******************************************************************************/
static void	pgm_dc_flush_host_proxy_revision(zbx_vector_pg_update_t *groups, zbx_uint64_t revision)
{
	zbx_vector_uint64_t	groupids;

	zbx_vector_uint64_create(&groupids);

	for (int i = 0; i < groups->values_num; i++)
	{
		if (0 != (groups->values[i].flags & ZBX_PG_GROUP_UPDATE_HP_MAP))
			zbx_vector_uint64_append(&groupids, groups->values[i].proxy_groupid);
	}

	zbx_dc_update_group_hpmap_revision(&groupids, revision);

	zbx_vector_uint64_destroy(&groupids);
}

/******************************************************************************
 *                                                                            *
 * Purpose: flush proxy group and host-proxy mapping updates to database      *
 *                                                                            *
 ******************************************************************************/
static void	pgm_flush_updates(zbx_pg_cache_t *cache)
{
	zbx_vector_pg_update_t	groups;
	zbx_vector_pg_host_t	hosts_new, hosts_mod, hosts_del;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_pg_update_create(&groups);
	zbx_vector_pg_host_create(&hosts_new);
	zbx_vector_pg_host_create(&hosts_mod);
	zbx_vector_pg_host_create(&hosts_del);

	pg_cache_get_updates(cache, &groups, &hosts_new, &hosts_mod, &hosts_del);

	if (0 != groups.values_num || 0 != hosts_new.values_num || 0 != hosts_mod.values_num ||
			0 != hosts_del.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0;
		int	ret;

		do
		{
			size_t	sql_offset = 0;

			zbx_db_begin();

			zbx_db_begin_multiple_update(&sql, &sql_alloc, &sql_offset);

			pgm_db_flush_group_updates(&sql, &sql_alloc, &sql_offset, &groups);
			pgm_db_flush_host_proxy_updates(&sql, &sql_alloc, &sql_offset, &hosts_mod);
			pgm_db_flush_host_proxy_deletes(&sql, &sql_alloc, &sql_offset, &hosts_del);

			zbx_db_end_multiple_update(&sql, &sql_alloc, &sql_offset);

			if (16 < sql_offset)
				zbx_db_execute("%s", sql);

			pgm_db_flush_host_proxy_inserts(&hosts_new);

			pgm_db_flush_host_proxy_revision(cache->hpmap_revision);

		}
		while (ZBX_DB_DOWN == (ret = zbx_db_commit()));

		if (ZBX_DB_OK <= ret)
			pgm_dc_flush_host_proxy_revision(&groups, cache->hpmap_revision);

		zbx_free(sql);

		/* WDN: change to trace loglevel */
		if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
			pg_cache_dump(cache);
	}

	zbx_vector_pg_host_destroy(&hosts_del);
	zbx_vector_pg_host_destroy(&hosts_mod);
	zbx_vector_pg_host_destroy(&hosts_new);
	zbx_vector_pg_update_destroy(&groups);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	pgm_get_proxy_names(const zbx_vector_uint64_t *proxyids, zbx_vector_str_t *names)
{
	zbx_db_row_t	row;
	zbx_db_result_t	result;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	int		i = 0;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select proxyid,name from proxy where ");
	zbx_db_add_condition_alloc(&sql, &sql_alloc, &sql_offset, "proxyid", proxyids->values, proxyids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by proxyid");

	result = zbx_db_select("%s", sql);
	zbx_free(sql);

	while (NULL != (row = zbx_db_fetch(result)) && i < proxyids->values_num)
	{
		zbx_uint64_t	proxyid;

		ZBX_DBROW2UINT64(proxyid, row[0]);

		while (proxyids->values[i] != proxyid)
		{
			zbx_vector_str_append(names, NULL);
			if (++i == proxyids->values_num)
				goto out;
		}

		zbx_vector_str_append(names, zbx_strdup(NULL, row[1]));
		i++;
	}
out:
	zbx_db_free_result(result);
}

static void	pgm_update_proxies(zbx_pg_cache_t *cache)
{
	zbx_vector_uint64_t	proxyids;
	zbx_vector_str_t	names;

	zbx_vector_uint64_create(&proxyids);
	zbx_vector_str_create(&names);

	pg_cache_lock(cache);

	for (int i = 0; i < cache->relocated_proxies.values_num; i++)
	{
		zbx_objmove_t	*reloc = &cache->relocated_proxies.values[i];

		if (0 == reloc->dstid)
			continue;

		if (NULL == zbx_hashset_search(&cache->proxies, &reloc->objid))
			zbx_vector_uint64_append(&proxyids, reloc->objid);
	}

	if (0 != proxyids.values_num)
	{
		pg_cache_unlock(cache);

		zbx_vector_uint64_sort(&proxyids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&proxyids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		pgm_get_proxy_names(&proxyids, &names);

		pg_cache_lock(cache);
	}

	for (int i = 0; i < cache->relocated_proxies.values_num; i++)
	{
		zbx_pg_group_t	*group;
		zbx_pg_proxy_t	*proxy = NULL;
		zbx_objmove_t	*reloc = &cache->relocated_proxies.values[i];

		if (0 != reloc->srcid)
		{
			if (NULL != (group = (zbx_pg_group_t *)zbx_hashset_search(&cache->groups, &reloc->srcid)))
			{
				proxy = pg_cache_group_remove_proxy(cache, group, reloc->objid);
				pg_cache_queue_group_update(cache, group);
			}
		}

		if (0 != reloc->dstid)
		{
			if (NULL != (group = (zbx_pg_group_t *)zbx_hashset_search(&cache->groups, &reloc->dstid)))
			{
				if (NULL == proxy)
				{
					int	j;
					char	*name;

					if (FAIL != (j = zbx_vector_uint64_search(&proxyids, reloc->objid,
							ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
					{
						name = names.values[j];
					}
					else
						name = "";

					pg_cache_group_add_proxy(cache, group, reloc->objid, name, 0);
				}
				else
					zbx_vector_pg_proxy_ptr_append(&group->proxies, proxy);

				pg_cache_queue_group_update(cache, group);
			}
		}
		else if (NULL != proxy)
			pg_cache_proxy_free(cache, proxy);
	}

	zbx_vector_objmove_clear(&cache->relocated_proxies);

	pg_cache_unlock(cache);

	zbx_vector_str_clear_ext(&names, zbx_str_free);
	zbx_vector_str_destroy(&names);
	zbx_vector_uint64_destroy(&proxyids);
}

/*
 * main process loop
 */

ZBX_THREAD_ENTRY(pg_manager_thread, args)
{
	zbx_pg_service_t	pgs;
	char			*error = NULL;
	const zbx_thread_info_t	*info = &((zbx_thread_args_t *)args)->info;
	zbx_pg_cache_t		cache;
	double			time_update = 0;

	zbx_setproctitle("%s #%d starting", get_process_type_string(info->process_type), info->process_num);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(info->program_type),
			info->server_num, get_process_type_string(info->process_type), info->process_num);

	zbx_db_connect(ZBX_DB_CONNECT_NORMAL);

	pgm_init(&cache);

	if (FAIL == pg_service_init(&pgs, &cache, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start proxy group manager service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	pgm_update_groups(&cache);
	pgm_db_get_hosts(&cache);
	pgm_db_get_proxies(&cache);
	pgm_db_get_hpmap(&cache);

	/* WDN: change to trace loglevel */
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
		pg_cache_dump(&cache);

	time_update = zbx_time();

	zbx_setproctitle("%s #%d started", get_process_type_string(info->process_type), info->process_num);

	while (ZBX_IS_RUNNING())
	{
		double	time_now;

		time_now = zbx_time();

		if (PGM_STATUS_CHECK_INTERVAL >= time_update - time_now)
		{
			pgm_update_groups(&cache);

			pgm_update_status(&cache);

			time_update = time_now;
		}

		if (0 != cache.relocated_proxies.values_num)
			pgm_update_proxies(&cache);

		zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_IDLE);
		zbx_sleep_loop(info, 1);
		zbx_update_selfmon_counter(info, ZBX_PROCESS_STATE_BUSY);

		if (0 != cache.group_updates.values_num)
			pgm_flush_updates(&cache);
	}

	pg_service_destroy(&pgs);
	zbx_db_close();

	pg_cache_destroy(&cache);

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(info->process_type), info->process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
