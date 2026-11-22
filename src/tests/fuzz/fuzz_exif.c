/**
 * @file fuzz_exif.c
 * @brief libFuzzer target: the EXIF reader (img2_exif_parse_tiff / img2_exif_parse_jpeg)
 *
 * Input layout: byte 0 bit 0 selects the entry point (0: raw TIFF/IFD data, 1: JPEG with an APP1 "Exif"
 * segment), the rest is the data. Every string field of the result must stay NUL-terminated.
 */

#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	(void)argv;
	fuzz_setup_logging();
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	img2_exif_t exif;

	if (size < 1) {
		return 0;
	}
	img2_exif_init(&exif);
	if (data[0] & 1) {
		(void)img2_exif_parse_jpeg(&exif, data + 1, size - 1);
	} else {
		(void)img2_exif_parse_tiff(&exif, data + 1, size - 1);
	}
	FUZZ_CHECK_CSTR(exif.make);
	FUZZ_CHECK_CSTR(exif.model);
	FUZZ_CHECK_CSTR(exif.software);
	FUZZ_CHECK_CSTR(exif.date_time);
	FUZZ_CHECK_CSTR(exif.lens_make);
	FUZZ_CHECK_CSTR(exif.lens_model);
	FUZZ_CHECK_CSTR(exif.gps_datestamp);
	FUZZ_CHECK_CSTR(exif.gps_timestamp);
	FUZZ_CHECK_CSTR(exif.description);
	FUZZ_CHECK_CSTR(exif.copyright);
	FUZZ_CHECK_CSTR(exif.artist);
	FUZZ_CHECK_CSTR(exif.user_comment);
	img2_exif_clear(&exif);
	return 0;
}
