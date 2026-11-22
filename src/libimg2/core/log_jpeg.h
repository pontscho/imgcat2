/**
 * @file log_jpeg.h
 * @brief libjpeg message output routed through img2_log() (shared by decoder_jpeg.c and encoder_jpeg.c)
 *
 * Needs <stdio.h> and <jpeglib.h> first.
 */

#ifndef IMG2_INTERNAL_CORE_LOG_JPEG_H
#define IMG2_INTERNAL_CORE_LOG_JPEG_H

/**
 * @brief output_message method of a libjpeg error manager: the formatted message at IMG2_LOG_WARN
 *
 * Replaces libjpeg's default (jerror.c output_message: fprintf(stderr, "%s\n", ...)), so warnings such as
 * "Corrupt JPEG data: N extraneous bytes before marker 0xNN" reach the host's log callback instead of fd 2.
 * libjpeg's emit_message is kept: it still decides what is shown (the first warning of an image, all of them
 * from trace_level 3; trace messages up to trace_level).
 */
void img2_jpeg_output_message(j_common_ptr cinfo);

#endif
