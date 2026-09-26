/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * log_format — pure printf-style formatter behind tn_logf
 * (z.ai step 9b item 1). See log_format.c for the supported set.
 */

#ifndef TOLUNNET_LOG_FORMAT_H
#define TOLUNNET_LOG_FORMAT_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

void tn_logf_vformat(char *buf, unsigned long cap, const char *fmt, va_list ap);

#endif /* TOLUNNET_LOG_FORMAT_H */
