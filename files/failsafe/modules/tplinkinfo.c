// SPDX-License-Identifier: GPL-2.0
/*
 * TP-Link 双槽状态查询：GET /sysinfo/tplink
 *
 * 页面左栏那两块「image_1 / image_2」就是从这里取的。为什么单开一个接口
 * 而不是塞进 /sysinfo：这个请求要读 2×128K 头 + 2×256K 槽 + 两次 LZMA
 * 解码，1~2 秒；/sysinfo 是页面一进来就要的，不能被它拖慢。
 *
 * 判定口径与 tplcheck 完全一致（同一份 status_one_slot 代码），所以页面上
 * 显示「完好」与串口 tplcheck 打 OK 是同一件事，不会出现两处结论打架。
 */

#include <common.h>
#include <malloc.h>
#include <net/httpd.h>
#include <failsafe/tplink_tpimg.h>

#include "modules.h"

DECLARE_GLOBAL_DATA_PTR;

#define APPEND_JSON_TO_BUF(fmt, args...)	\
	do {	\
		len += snprintf(buf + len, left - len, fmt, ##args);	\
	} while (0)

static void respond_json(struct httpd_response *response, int code,
			 const char *data, const char *content_type)
{
	response->status = HTTP_RESP_STD;
	response->data = data ? data : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "text/plain";
}

/* i 是数组下标（0/1），不是槽号 —— 用槽号判逗号会拼出
 * "slots":[,{...}] 这种非法 JSON，前端 JSON.parse 整包失败。
 *
 * running 是槽号（1 或 2），来自 boot_flag。原来这里写的是
 *   i == 0 ? "true" : "false"
 * 于是永远把 image_1 标成运行中，而同一个响应里的 uboot_run.slot
 * 却是 image_2 —— 页面自己跟自己矛盾。两个字段必须同源。 */
static void tplink_slot_json(char *buf, int *plen, int left, int i,
			     int running, const struct tpimg_slot_state *s)
{
	int len = *plen;

	APPEND_JSON_TO_BUF("%s{\"name\":\"image_%d\",", i ? "," : "", i + 1);
	APPEND_JSON_TO_BUF("\"active\":%s,", (running == i + 1) ? "true" : "false");
	APPEND_JSON_TO_BUF("\"hdr_ok\":%s,", s->hdr_ok ? "true" : "false");
	APPEND_JSON_TO_BUF("\"md5_match\":%s,", s->md5_match ? "true" : "false");
	APPEND_JSON_TO_BUF("\"decode_ok\":%s,", s->decode_ok ? "true" : "false");
	APPEND_JSON_TO_BUF("\"oom\":%s,", s->oom ? "true" : "false");
	/* 字段名就是 md5 —— 前端读 s.md5。曾用 md5_ok，前端读不到，
	 * 页面上 md5 一栏永远是破折号。两边必须同名。 */
	APPEND_JSON_TO_BUF("\"md5\":\"%s\"}", s->md5);

	*plen = len;
}

/* ---- 用 GET /sysinfo/tplink 的返回值直接调试 ----
 * 它同时也是 tplcheck 的「安静版」，出问题时先看这个 JSON。 */
static void tplinkinfo_handler(enum httpd_uri_handler_status status,
			       struct httpd_request *request,
			       struct httpd_response *response)
{
	struct tpimg_status st;
	char *buf;
	int len = 0;
	int left = 4096;
	const char *run;
	int ret;

	(void)request;

	if (status == HTTP_CB_CLOSED && response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->session_data = NULL;

	ret = tpimg_get_status(&st);
	if (ret) {
		/* 内存不足或 NAND 未就绪 */
		respond_json(response, 500,
			     "{\"status\":\"fail\",\"reason\":\"nand_unavailable\"}",
			     "application/json");
		return;
	}

	buf = malloc(left);
	if (!buf) {
		respond_json(response, 500,
			     "{\"status\":\"fail\",\"reason\":\"no_mem\"}",
			     "application/json");
		return;
	}

	/* 当前跑的是哪个 u-boot 槽：只有 boot_flag 这一个线索。
	 * 一级 u-boot 先验 image_1，通过就启动 image_1，所以它写下的槽号
	 * 就是这次真正执行的那一个 —— 但仍然标 assumed，因为它不是
	 * 本进程能自证的身份（镜像本身没有槽号）。 */
	switch (st.boot_flag_slot) {
	case 1:  run = "image_1"; break;
	case 2:  run = "image_2"; break;
	default: run = "unknown"; break;
	}

	APPEND_JSON_TO_BUF("{\"status\":\"success\",");
	APPEND_JSON_TO_BUF("\"uboot_run\":{\"slot\":\"%s\",\"assumed\":true},",
			   run);
	APPEND_JSON_TO_BUF("\"boot_flag\":{\"ok\":%s,\"raw\":%lu},",
			   st.boot_flag_ok ? "true" : "false",
			   (ulong)st.boot_flag_slot);

	APPEND_JSON_TO_BUF("\"slots\":[");
	tplink_slot_json(buf, &len, left, 0, (int)st.boot_flag_slot, &st.slot[0]);
	tplink_slot_json(buf, &len, left, 1, (int)st.boot_flag_slot, &st.slot[1]);
	APPEND_JSON_TO_BUF("],");

	/* 内核+rootfs 实际来自哪个槽。tpimg_boot_slot() 先试 image_2（A 槽，
	 * kernel 0x4880000 / rootfs 0x4FC0000），失败才退回 image_1（B 槽，
	 * 0x880000 / 0xFC0000）。网页刷固件只写 A 槽，所以正常永远是 image_2。 */
	if (st.fw_slot == 2)
		APPEND_JSON_TO_BUF("\"firmware_slot\":{\"name\":\"image_2\","
				   "\"label\":\"A 槽 · kernel + rootfs\"},");
	else if (st.fw_slot == 1)
		APPEND_JSON_TO_BUF("\"firmware_slot\":{\"name\":\"image_1\","
				   "\"label\":\"B 槽（回退）· kernel + rootfs\"},");
	else
		APPEND_JSON_TO_BUF("\"firmware_slot\":{\"name\":\"-\","
				   "\"label\":\"两个槽都没有可用内核\"},");

	APPEND_JSON_TO_BUF("\"uboot_target\":%d}", tpimg_uboot_target);

	respond_json(response, 200, buf, "application/json");
	response->session_data = buf;
}

void tplinkinfo_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/sysinfo/tplink",
				   &tplinkinfo_handler, NULL);
}
