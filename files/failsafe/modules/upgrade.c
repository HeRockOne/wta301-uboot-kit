/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe upgrade module
 */

/*
 * Modified by: chenxin527
 */

#include <common.h>
#include <malloc.h>
#include <net/tcp.h>
#include <net/httpd.h>
#include <failsafe/failsafe.h>
#include <failsafe/tplink_tpimg.h>
#include <ipq_api.h>

#include "modules.h"

struct upload_session {
	char header_buf[4096];
	int ret;
    int body_sent;
};

struct result_session {
	char buf[4096];
	int ret;
	int body_sent;
	bool auto_reboot;
};

static const struct {
	upgrade_type_t type;
	const char *label;
} upgrade_types[] = {
	{ WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE, "firmware" },
	{ WEBFAILSAFE_UPGRADE_TYPE_UBOOT, "uboot" },
	{ WEBFAILSAFE_UPGRADE_TYPE_ART, "art" },
	{ WEBFAILSAFE_UPGRADE_TYPE_CDT, "cdt" },
	{ WEBFAILSAFE_UPGRADE_TYPE_PTABLE, "ptable" },
	{ WEBFAILSAFE_UPGRADE_TYPE_SIMG, "simg" },
	{ WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS, "initramfs" }
};

bool auto_action_pending;
const void *upload_data;
upgrade_type_t upgrade_type;

static u32 fs_upload_id;
static u32 upload_data_id;
static size_t upload_size;

static void upload_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct httpd_form_value *form_value;
	struct upload_session *sess;
	int idx;

	if (status == HTTP_CB_NEW) {
		sess = calloc(1, sizeof(*sess));
		if (!sess) {
			response->info.code = 500;
			return;
		}

		handle_start_led_state();

		response->session_data = sess;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->info.code = 200;

		response->size = http_make_response_header(&response->info,
							sess->header_buf, sizeof(sess->header_buf));

		response->data = sess->header_buf;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		sess = response->session_data;

		if (sess->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		/* new upload session identifier */
		fs_upload_id = rand();

		for (idx = 0; idx < ARRAY_SIZE(upgrade_types); idx++) {
			form_value = httpd_request_find_value(request, upgrade_types[idx].label);
			if (form_value) {
				upgrade_type = upgrade_types[idx].type;
				break;
			}
		}

		if (idx == ARRAY_SIZE(upgrade_types)) {
			puts("NO supported upgrade type found!\n");

			/* 没有匹配的 upgrade_type，返回 fail*/
			response->data = "{\"status\":\"fail\","
							"\"info\":{\"type\":\"wrong_upgrade_type\"}}";
			response->size = strlen(response->data);
			sess->body_sent = 1;

			httpd_debug("response message: %s\n", response->data);

			return;
		}

		upload_data_id = fs_upload_id;
		upload_data = form_value->data;
		upload_size = form_value->size;

		/* u-boot 页的「写入目标」三选一：0=两槽 1=image_1 2=image_2。
		 * 在 /upload 阶段就记下来（这里是真正解析过 multipart 的地方），
		 * /result 阶段直接用 tpimg_uboot_target，不再依赖 body 解析。 */
		if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_UBOOT) {
			struct httpd_form_value *slot =
				httpd_request_find_value(request, "slot");

			tpimg_uboot_target = TPIMG_UBOOT_TARGET_BOTH;
			if (slot && slot->data && slot->size == 1 &&
			    (slot->data[0] == '1' || slot->data[0] == '2'))
				tpimg_uboot_target = slot->data[0] - '0';
			printf("upgrade: u-boot target slot = %d\n",
			       tpimg_uboot_target);
		}

		httpd_debug("upload_data = 0x%lx, upload_size = %lu (0x%lx)\n",
			(ulong)upload_data, (ulong)upload_size, (ulong)upload_size);

		sess->ret = failsafe_validate_image(upgrade_type, form_value->filename,
						upload_data, (ulong)upload_size, response);

		sess->body_sent = 1;

		httpd_debug("response message: %s\n", response->data);

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		sess = response->session_data;

		if (sess->ret == RET_SUCCESS)
			handle_success_led_state();
		else
			handle_fail_led_state();

		free(response->session_data);
	}
}

static void result_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct httpd_form_value *auto_reboot;
	struct result_session *st;
	u32 size;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		handle_start_led_state();

		auto_reboot = httpd_request_find_value(request, "auto_reboot");

		/*
		 * 默认不自动重启。前端 index.html 的交互就是：
		 *   写入完成 -> 提示「点下方重启」-> 按钮点亮 -> 用户点 /reboot -> do_reset()
		 * 若这里默认 true，设备会在 /result 关闭连接后立刻复位，
		 * 前端只会看到「连接中断，设备可能已出错」的误导提示，反而像失败。
		 *
		 * 保留显式开关：前端传 auto_reboot=true 才自动重启。
		 */
		if (auto_reboot && auto_reboot->data &&
		    !strcmp(auto_reboot->data, "true"))
			st->auto_reboot = true;
		else
			st->auto_reboot = false;

		st->ret = RET_FAILURE;

		response->session_data = st;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->info.code = 200;

		size = http_make_response_header(&response->info,
			st->buf, sizeof(st->buf));

		response->data = st->buf;
		response->size = size;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		st = response->session_data;

		if (st->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (upload_data_id == fs_upload_id) {
			if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS) {
				st->auto_reboot = true; /* 启动 Initramfs 等同于自动重启 */
				st->ret = RET_SUCCESS;
			} else {
				st->ret = failsafe_write_image(upgrade_type, (ulong)upload_data,
					(ulong)upload_size, response);
			}
		} else {
			snprintf(st->buf, sizeof(st->buf),
				"{\"status\":\"fail\","
				"\"info\":{\"type\":\"upload_id_mismatch\","
				"\"upload_data_id\":\"%u\",\"fs_upload_id\":\"%u\"}}",
				upload_data_id, fs_upload_id);
			response->data = st->buf;
			st->ret = RET_UPLOAD_ID_MISMATCH;
		}

		/* invalidate upload identifier */
		upload_data_id = rand();

		if (st->ret == RET_SUCCESS) {
			snprintf(st->buf, sizeof(st->buf),
				"{\"status\":\"success\",\"info\":{\"reboot\":%s}}",
				st->auto_reboot ? "true" : "false");
			response->data = st->buf;
		}

		response->size = strlen(response->data);
		st->body_sent = 1;

		httpd_debug("response message: %s\n", response->data);

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		bool upgrade_success;

		st = response->session_data;
		upgrade_success = st->ret == RET_SUCCESS ? true : false;
		auto_action_pending = st->auto_reboot && upgrade_success;

		free(response->session_data);

		if (upgrade_success)
			handle_success_led_state();
		else
			handle_fail_led_state();

		if (auto_action_pending)
			tcp_close_all_conn();
	}
}

void upgrade_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
}
