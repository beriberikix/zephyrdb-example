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

static const zdb_event_listener_t g_kv_listeners[] = {
	{
		.notify = kv_listener_notify,
		.user_ctx = NULL,
	},
};

static zdb_cfg_t g_cfg = {
	.kv_backend_fs = NULL,
	.lfs_mount_point = CONFIG_ZDB_LFS_MOUNT_POINT,
	.work_q = &k_sys_work_q,
	.event_listeners = g_kv_listeners,
	.event_listener_count = ARRAY_SIZE(g_kv_listeners),
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

static void print_latest_ts_event(const char *label)
{
	zdb_ts_event_t event;

	if (zbus_chan_read(&zdb_ts_event_chan, &event, K_NO_WAIT) == 0) {
		printk("zbus[ts]: %s type=%s stream=%s status=%d ts=%" PRIu64 "\n",
		       label, ts_event_type_str(event.type),
		       (event.stream_name != NULL) ? event.stream_name : "(null)",
		       (int)event.status, event.timestamp_ms);
	}
}

static void print_latest_doc_event(const char *label)
{
	zdb_doc_event_t event;

	if (zbus_chan_read(&zdb_doc_event_chan, &event, K_NO_WAIT) == 0) {
		printk("zbus[doc]: %s type=%s collection=%s doc=%s status=%d ts=%" PRIu64 "\n",
		       label, doc_event_type_str(event.type),
		       (event.collection_name != NULL) ? event.collection_name : "(null)",
		       (event.document_id != NULL) ? event.document_id : "(null)",
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
	zdb_ts_t stream;
	zdb_ts_sample_i64_t sample = {
		.ts_ms = (uint64_t)k_uptime_get(),
		.value = 100,
	};
	zdb_ts_agg_result_t agg;
	zdb_ts_window_t window = ZDB_TS_WINDOW_ALL;

	rc = zdb_ts_open(&g_db, "metrics", &stream);
	if (rc != ZDB_OK) {
		printk("ts: open failed rc=%d (check LittleFS mount at %s)\n",
		       (int)rc, CONFIG_ZDB_LFS_MOUNT_POINT);
		return rc;
	}

	rc = zdb_ts_append_i64(&stream, &sample);
	if (rc != ZDB_OK) {
		printk("ts: append failed rc=%d\n", (int)rc);
		(void)zdb_ts_close(&stream);
		return rc;
	}
	print_latest_ts_event("after append");

	rc = zdb_ts_flush_sync(&stream, K_SECONDS(2));
	if (rc != ZDB_OK) {
		printk("ts: flush failed rc=%d\n", (int)rc);
		(void)zdb_ts_close(&stream);
		return rc;
	}
	print_latest_ts_event("after flush");

	rc = zdb_ts_query_aggregate(&stream, window, ZDB_TS_AGG_AVG, &agg);
	if (rc == ZDB_OK) {
		int64_t avg_milli = (int64_t)(agg.value * 1000.0);
		const char *avg_sign = "";
		uint64_t avg_whole;
		uint64_t avg_frac;

		if (avg_milli < 0) {
			avg_sign = "-";
			avg_milli = -avg_milli;
		}

		avg_whole = (uint64_t)avg_milli / 1000U;
		avg_frac = (uint64_t)avg_milli % 1000U;

		printk("ts: avg=%s%" PRIu64 ".%03" PRIu64 " points=%u\n",
		       avg_sign, avg_whole, avg_frac, (unsigned int)agg.points);
	} else {
		printk("ts: aggregate query rc=%d\n", (int)rc);
	}

	(void)zdb_ts_close(&stream);
	return ZDB_OK;
}

static zdb_status_t run_doc_demo(void)
{
	zdb_status_t rc;
	zdb_doc_t doc;

	rc = zdb_doc_create(&g_db, "users", "u100", &doc);
	if (rc != ZDB_OK) {
		printk("doc: create failed rc=%d\n", (int)rc);
		return rc;
	}
	print_latest_doc_event("after create");

	(void)zdb_doc_field_set_string(&doc, "name", "Ada");
	(void)zdb_doc_field_set_bool(&doc, "active", true);

	rc = zdb_doc_save(&doc);
	if (rc != ZDB_OK) {
		printk("doc: save failed rc=%d\n", (int)rc);
		(void)zdb_doc_close(&doc);
		return rc;
	}
	print_latest_doc_event("after save");

	printk("doc: created and saved user u100\n");
	(void)zdb_doc_close(&doc);

	rc = zdb_doc_delete(&g_db, "users", "u100");
	if (rc == ZDB_OK) {
		printk("doc: deleted user u100\n");
		print_latest_doc_event("after delete");
	}

	return ZDB_OK;
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
