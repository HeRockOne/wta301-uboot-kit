// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 chenxin527. All Rights Reserved.
 *
 * This file is part of the project uboot-qsdk12.5-build
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _FAILSAFE_MODULES_H_
#define _FAILSAFE_MODULES_H_

// =============================================================================
// 帮助函数
// =============================================================================

// =============================================================================
// Handlers 注册函数
// =============================================================================
void index_register_uri_handlers(struct httpd_instance *inst);
void html_register_uri_handlers(struct httpd_instance *inst);
void backup_register_uri_handlers(struct httpd_instance *inst);
void env_register_uri_handlers(struct httpd_instance *inst);
#ifdef CONFIG_FAILSAFE_MAC_MANAGEMENT
void mac_register_uri_handlers(struct httpd_instance *inst);
#endif /* CONFIG_FAILSAFE_MAC_MANAGEMENT */
void mibib_register_uri_handlers(struct httpd_instance *inst);
void misc_register_uri_handlers(struct httpd_instance *inst);
void network_register_uri_handlers(struct httpd_instance *inst);
void sysinfo_register_uri_handlers(struct httpd_instance *inst);
void syslog_register_uri_handlers(struct httpd_instance *inst);
void tplinkinfo_register_uri_handlers(struct httpd_instance *inst);
void upgrade_register_uri_handlers(struct httpd_instance *inst);
void webterm_register_uri_handlers(struct httpd_instance *inst);

#endif /* _FAILSAFE_MODULES_H_ */
