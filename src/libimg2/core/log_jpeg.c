/**
 * @file log_jpeg.c
 * @brief libjpeg message output routed through img2_log() instead of stderr
 */

/* clang-format off */
#include <stdio.h>
#include <jpeglib.h>
/* clang-format on */

#include "log.h"
#include "log_jpeg.h"

void img2_jpeg_output_message(j_common_ptr cinfo)
{
	/* Same text as libjpeg's default output_message (jerror.c); the default sink adds the "\n" */
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, buffer);
	img2_log(IMG2_LOG_WARN, "%s", buffer);
}
