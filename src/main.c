#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/zms.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include "zephyrdb.h"
#include "zephyrdb_eventing_zbus.h"

#define ZDB_ZMS_PARTITION storage_partition
#define ZDB_ZMS_PARTITION_DEVICE PARTITION_DEVICE(ZDB_ZMS_PARTITION)
#define ZDB_ZMS_PARTITION_OFFSET PARTITION_OFFSET(ZDB_ZMS_PARTITION)

static struct zms_fs g_zms;

static void kv_listener_notify(const zdb_kv_event_t *event, void *user_ctx)
{
	ARG_UNUSED(user_ctx);
	if (event == NULL) {
		return;
	}

	printk("listener[kv]: type=%d ns=%s key=%s len=%u status=%d\n",
	       (int)event->type, event->namespace_name, event->key,
	       (unsigned int)event->value_len, (int)event->status);
}

static void ts_listener_notify(const zdb_ts_event_t *event, void *user_ctx)
{
	ARG_UNUSED(user_ctx);
	if (event == NULL) {
		return;
	}

	printk("listener[ts]: type=%d stream=%s sample_ts=%" PRIu64 " value=%" PRId64
	       " flushed=%u status=%d\n",
	       (int)event->type, event->stream_name, event->sample_ts_ms, event->sample_value,
	       (unsigned int)event->flushed_bytes, (int)event->status);
}

static void doc_listener_notify(const zdb_doc_event_t *event, void *user_ctx)
{
	ARG_UNUSED(user_ctx);
	if (event == NULL) {
		return;
	}

	printk("listener[doc]: type=%d collection=%s id=%s fields=%u bytes=%u status=%d\n",
	       (int)event->type, event->collection_name, event->document_id,
	       (unsigned int)event->field_count, (unsigned int)event->serialized_bytes,
	       (int)event->status);
}

static const zdb_event_listener_t g_kv_listeners[] = {
	{
		.notify = kv_listener_notify,
		.user_ctx = NULL,
	},
};

static const zdb_ts_event_listener_t g_ts_listeners[] = {
	{
		.notify = ts_listener_notify,
		.user_ctx = NULL,
	},
};

static const zdb_doc_event_listener_t g_doc_listeners[] = {
	{
		.notify = doc_listener_notify,
		.user_ctx = NULL,
	},
};

static zdb_cfg_t g_cfg = {
	.kv_backend_fs = NULL,
	.lfs_mount_point = CONFIG_ZDB_LFS_MOUNT_POINT,
	.work_q = &k_sys_work_q,
	.event_listeners = g_kv_listeners,
	.event_listener_count = ARRAY_SIZE(g_kv_listeners),
	.ts_event_listeners = g_ts_listeners,
	.ts_event_listener_count = ARRAY_SIZE(g_ts_listeners),
	.doc_event_listeners = g_doc_listeners,
	.doc_event_listener_count = ARRAY_SIZE(g_doc_listeners),
};

ZDB_DEFINE_STATIC(g_db, g_cfg);

static const char *kv_event_type_str(zdb_event_type_t type)
{
	switch (type) {
	case ZDB_EVENT_KV_SET:
		return "SET";
	case ZDB_EVENT_KV_DELETE:
		return "DELETE";
	default:
		return "UNKNOWN";
	}
}

static const char *ts_event_type_str(zdb_ts_event_type_t type)
{
	switch (type) {
	case ZDB_TS_EVENT_APPEND:
		return "APPEND";
	case ZDB_TS_EVENT_FLUSH:
		return "FLUSH";
	case ZDB_TS_EVENT_RECOVER:
		return "RECOVER";
	default:
		return "UNKNOWN";
	}
}

static const char *doc_event_type_str(zdb_doc_event_type_t type)
{
	switch (type) {
	case ZDB_DOC_EVENT_CREATE:
		return "CREATE";
	case ZDB_DOC_EVENT_SAVE:
		return "SAVE";
	case ZDB_DOC_EVENT_DELETE:
		return "DELETE";
	default:
		return "UNKNOWN";
	}
}

static int init_zms(struct zms_fs *fs)
{
	struct flash_pages_info info;
	int rc;

	fs->flash_device = ZDB_ZMS_PARTITION_DEVICE;
	if (!device_is_ready(fs->flash_device)) {
		printk("kitchen_sink: storage device not ready\n");
		return -ENODEV;
	}

	fs->offset = ZDB_ZMS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs->flash_device, fs->offset, &info);
	if (rc != 0) {
		printk("kitchen_sink: flash page info failed rc=%d\n", rc);
		return rc;
	}

	fs->sector_size = info.size;
	fs->sector_count = 3U;

	rc = zms_mount(fs);
	if (rc != 0) {
		printk("kitchen_sink: zms mount failed rc=%d\n", rc);
		return rc;
	}

	return 0;
}

/* zbus channels hold only the latest published value (not a queue),
 * so we do a single read to print the most recent event. */
static void print_latest_kv_event(void)
{
	zdb_kv_event_t event;
	if (zbus_chan_read(&zdb_kv_event_chan, &event, K_NO_WAIT) == 0) {
		printk("zbus[kv]: type=%s ns=%s key=%s len=%u status=%d ts=%" PRIu64 "\n",
		       kv_event_type_str(event.type), event.namespace_name, event.key,
		       (unsigned int)event.value_len, (int)event.status, event.timestamp_ms);
	}
}

static void print_latest_ts_event(void)
{
	zdb_ts_event_t event;
	if (zbus_chan_read(&zdb_ts_event_chan, &event, K_NO_WAIT) == 0) {
		printk("zbus[ts]: type=%s stream=%s sample_ts=%" PRIu64 " val=%" PRId64
		       " flushed=%u truncated=%u status=%d ts=%" PRIu64 "\n",
		       ts_event_type_str(event.type), event.stream_name, event.sample_ts_ms,
		       event.sample_value, (unsigned int)event.flushed_bytes,
		       (unsigned int)event.truncated_bytes, (int)event.status, event.timestamp_ms);
	}
}

static void print_latest_doc_event(void)
{
	zdb_doc_event_t event;
	if (zbus_chan_read(&zdb_doc_event_chan, &event, K_NO_WAIT) == 0) {
		printk("zbus[doc]: type=%s collection=%s id=%s fields=%u bytes=%u status=%d ts=%" PRIu64
		       "\n",
		       doc_event_type_str(event.type), event.collection_name, event.document_id,
		       (unsigned int)event.field_count, (unsigned int)event.serialized_bytes,
		       (int)event.status, event.timestamp_ms);
	}
}

static void print_health_and_stats(const char *label)
{
	zdb_health_t health;
	zdb_ts_stats_t stats;
	zdb_ts_stats_export_t exported;
	zdb_status_t rc;

	health = zdb_health(&g_db);
	zdb_ts_stats_get(&g_db, &stats);

	printk("%s: health=%d recover_runs=%u recover_failures=%u crc_failures=%u corrupt=%u\n",
	       label, (int)health, (unsigned int)stats.recover_runs,
	       (unsigned int)stats.recover_failures, (unsigned int)stats.crc_failures,
	       (unsigned int)stats.corrupt_records);

	rc = zdb_ts_stats_export(&g_db, &exported);
	if (rc == ZDB_OK) {
		rc = zdb_ts_stats_export_validate(&exported);
		printk("%s: stats_export validate rc=%d\n", label, (int)rc);
	}
}

static zdb_status_t run_kv_demo(void)
{
	zdb_status_t rc;
	zdb_kv_t kv;
	zdb_kv_iter_t iter;
	uint32_t boot_count = 3U;
	uint32_t read_u32 = 0U;
	char mode[] = "normal";
	size_t out_len = 0U;
	char key_buf[CONFIG_ZDB_MAX_KEY_LEN + 1];
	uint8_t value_buf[64];
	size_t key_len = 0U;
	size_t value_len = 0U;

	rc = zdb_kv_open(&g_db, "demo", &kv);
	if (rc != ZDB_OK) {
		printk("kv: open failed rc=%d\n", (int)rc);
		return rc;
	}

	rc = zdb_kv_set(&kv, "boot_count", &boot_count, sizeof(boot_count));
	if (rc != ZDB_OK) {
		printk("kv: set boot_count failed rc=%d\n", (int)rc);
		goto out_close;
	}

	rc = zdb_kv_set(&kv, "mode", mode, sizeof(mode));
	if (rc != ZDB_OK) {
		printk("kv: set mode failed rc=%d\n", (int)rc);
		goto out_close;
	}

	rc = zdb_kv_get(&kv, "boot_count", &read_u32, sizeof(read_u32), &out_len);
	if ((rc == ZDB_OK) && (out_len == sizeof(read_u32))) {
		printk("kv: boot_count=%u\n", (unsigned int)read_u32);
	} else {
		printk("kv: get boot_count failed rc=%d len=%u\n", (int)rc, (unsigned int)out_len);
	}

	rc = zdb_kv_iter_open(&kv, &iter);
	if (rc == ZDB_OK) {
		printk("kv: iterating keys\n");
		while (1) {
			rc = zdb_kv_iter_next(&iter, key_buf, sizeof(key_buf), &key_len, value_buf,
					      sizeof(value_buf), &value_len);
			if (rc == ZDB_ERR_NOT_FOUND) {
				break;
			}
			if (rc != ZDB_OK) {
				printk("kv: iter_next failed rc=%d\n", (int)rc);
				break;
			}
			printk("kv: key=%s value_len=%u\n", key_buf, (unsigned int)value_len);
		}
		(void)zdb_kv_iter_close(&iter);
		rc = ZDB_OK;
	}

	rc = zdb_kv_delete(&kv, "mode");
	if (rc != ZDB_OK) {
		printk("kv: delete mode failed rc=%d\n", (int)rc);
		goto out_close;
	}

	rc = zdb_kv_get(&kv, "mode", value_buf, sizeof(value_buf), &value_len);
	if (rc == ZDB_ERR_NOT_FOUND) {
		printk("kv: delete verified for mode\n");
		rc = ZDB_OK;
	}

out_close:
	(void)zdb_kv_close(&kv);
	print_latest_kv_event();
	return rc;
}

static zdb_status_t run_ts_demo(void)
{
	zdb_status_t rc;
	zdb_ts_t ts;
	zdb_ts_sample_i64_t sample;
	zdb_ts_sample_i64_t batch[3];
	zdb_ts_agg_result_t agg;
	zdb_ts_window_t window = ZDB_TS_WINDOW_ALL;
	uint8_t fb_buf[128];
	size_t fb_len = 0U;
	static const zdb_ts_agg_t agg_types[] = {
		ZDB_TS_AGG_MIN,
		ZDB_TS_AGG_MAX,
		ZDB_TS_AGG_AVG,
		ZDB_TS_AGG_SUM,
		ZDB_TS_AGG_COUNT,
	};
	size_t i;

	rc = zdb_ts_open(&g_db, "sensors", &ts);
	if (rc != ZDB_OK) {
		printk("ts: open failed rc=%d\n", (int)rc);
		return rc;
	}

	sample.ts_ms = (uint64_t)k_uptime_get();
	sample.value = 100;
	rc = zdb_ts_append_i64(&ts, &sample);
	if (rc != ZDB_OK) {
		printk("ts: append single failed rc=%d\n", (int)rc);
		goto out_close;
	}

	for (i = 0; i < ARRAY_SIZE(batch); ++i) {
		batch[i].ts_ms = sample.ts_ms + ((uint64_t)(i + 1) * 10U);
		batch[i].value = 100 + (int64_t)((i + 1) * 5);
	}

	rc = zdb_ts_append_batch_i64(&ts, batch, ARRAY_SIZE(batch));
	if (rc != ZDB_OK) {
		printk("ts: append batch failed rc=%d\n", (int)rc);
		goto out_close;
	}

	rc = zdb_ts_sample_i64_export_flatbuffer(&sample, fb_buf, sizeof(fb_buf), &fb_len);
	if (rc == ZDB_OK) {
		printk("ts: flatbuffer exported %u bytes\n", (unsigned int)fb_len);
	}

	rc = zdb_ts_flush_sync(&ts, K_SECONDS(2));
	if (rc != ZDB_OK) {
		printk("ts: flush failed rc=%d\n", (int)rc);
		goto out_close;
	}

	for (i = 0; i < ARRAY_SIZE(agg_types); ++i) {
		rc = zdb_ts_query_aggregate(&ts, window, agg_types[i], &agg);
		if (rc == ZDB_OK) {
			printk("ts: agg=%d value=%f points=%u\n", (int)agg.agg, agg.value,
			       (unsigned int)agg.points);
		} else {
			printk("ts: agg=%d rc=%d\n", (int)agg_types[i], (int)rc);
		}
	}

out_close:
	(void)zdb_ts_close(&ts);
	print_latest_ts_event();
	return rc;
}

static zdb_status_t run_doc_demo(void)
{
	zdb_status_t rc;
	zdb_doc_t doc;
	uint8_t profile_blob[] = {0xDE, 0xAD, 0xBE, 0xEF};
	int64_t age = 0;
	double score = 0.0;
	const char *name = NULL;
	bool active = false;
	zdb_bytes_t avatar = {0};
	uint8_t fb_buf[512];
	size_t fb_len = 0U;
	zdb_doc_query_filter_t filters[1];
	zdb_doc_query_t query;
	zdb_doc_metadata_t results[4];
	size_t result_count = ARRAY_SIZE(results);
	size_t i;

	rc = zdb_doc_create(&g_db, "users", "user-1", &doc);
	if (rc != ZDB_OK) {
		printk("doc: create failed rc=%d\n", (int)rc);
		return rc;
	}

	(void)zdb_doc_field_set_string(&doc, "name", "Ada Lovelace");
	(void)zdb_doc_field_set_i64(&doc, "age", 36);
	(void)zdb_doc_field_set_f64(&doc, "score", 98.5);
	(void)zdb_doc_field_set_bool(&doc, "active", true);
	(void)zdb_doc_field_set_bytes(&doc, "avatar", profile_blob, sizeof(profile_blob));

	rc = zdb_doc_save(&doc);
	if (rc != ZDB_OK) {
		printk("doc: save failed rc=%d\n", (int)rc);
		goto out_close;
	}

	if (zdb_doc_field_get_string(&doc, "name", &name) == ZDB_OK) {
		printk("doc: name=%s\n", name);
	}
	if (zdb_doc_field_get_i64(&doc, "age", &age) == ZDB_OK) {
		printk("doc: age=%" PRId64 "\n", age);
	}
	if (zdb_doc_field_get_f64(&doc, "score", &score) == ZDB_OK) {
		printk("doc: score=%f\n", score);
	}
	if (zdb_doc_field_get_bool(&doc, "active", &active) == ZDB_OK) {
		printk("doc: active=%d\n", active ? 1 : 0);
	}
	if (zdb_doc_field_get_bytes(&doc, "avatar", &avatar) == ZDB_OK) {
		printk("doc: avatar_len=%u\n", (unsigned int)avatar.len);
	}

	rc = zdb_doc_export_flatbuffer(&doc, fb_buf, sizeof(fb_buf), &fb_len);
	if (rc == ZDB_OK) {
		printk("doc: flatbuffer exported %u bytes\n", (unsigned int)fb_len);
	}

	filters[0].field_name = "active";
	filters[0].type = ZDB_DOC_FIELD_BOOL;
	filters[0].bool_value = true;
	filters[0].numeric_value = 0.0;
	filters[0].string_value = NULL;

	query.filters = filters;
	query.filter_count = ARRAY_SIZE(filters);
	query.from_ms = 0;
	query.to_ms = UINT64_MAX;
	query.limit = (uint32_t)ARRAY_SIZE(results);

	rc = zdb_doc_query(&g_db, &query, results, &result_count);
	if (rc == ZDB_OK) {
		printk("doc: query result_count=%u\n", (unsigned int)result_count);
		for (i = 0; i < result_count; ++i) {
			printk("doc: result[%u] collection=%s id=%s fields=%u\n", (unsigned int)i,
			       results[i].collection_name, results[i].document_id,
			       (unsigned int)results[i].field_count);
		}
		(void)zdb_doc_metadata_free(results, result_count);
	}

	rc = zdb_doc_delete(&g_db, "users", "user-1");
	if (rc == ZDB_OK) {
		printk("doc: delete verified for users/user-1\n");
	}

out_close:
	(void)zdb_doc_close(&doc);
	print_latest_doc_event();
	return rc;
}

int main(void)
{
	zdb_status_t rc;
	int zms_rc;

	printk("zephyrdb-example: kitchen sink starting\n");

	zms_rc = init_zms(&g_zms);
	if (zms_rc != 0) {
		return 1;
	}

	g_cfg.kv_backend_fs = &g_zms;

	rc = zdb_init(&g_db, &g_cfg);
	if (rc != ZDB_OK) {
		printk("zdb_init failed rc=%d (%s)\n", (int)rc, zdb_status_str(rc));
		return 1;
	}

	print_health_and_stats("before");

	rc = run_kv_demo();
	if (rc != ZDB_OK) {
		printk("kv demo failed rc=%d (%s)\n", (int)rc, zdb_status_str(rc));
	}

	rc = run_ts_demo();
	if (rc != ZDB_OK) {
		printk("ts demo failed rc=%d (%s)\n", (int)rc, zdb_status_str(rc));
	}

	rc = run_doc_demo();
	if (rc != ZDB_OK) {
		printk("doc demo failed rc=%d (%s)\n", (int)rc, zdb_status_str(rc));
	}

	print_health_and_stats("after");
	zdb_ts_stats_reset(&g_db);

	zdb_shell_register(&g_db);
	printk("zephyrdb-example: ready. Try shell commands: zdb health, zdb stats\n");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
