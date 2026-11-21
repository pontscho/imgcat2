/**
 * @file status.c
 * @brief img2_strerror()
 */

#include "status.h"

const char *img2_strerror(img2_status_t st)
{
	switch (st) {
		case IMG2_OK: return "success";
		case IMG2_E_NOMEM: return "out of memory";
		case IMG2_E_ARG: return "invalid argument";
		case IMG2_E_FORMAT: return "malformed or unrecognised data";
		case IMG2_E_UNSUPPORTED: return "unsupported format";
		case IMG2_E_LIMIT: return "resource limit exceeded";
		case IMG2_E_IO: return "I/O error";
		case IMG2_E_CODEC: return "codec error";
		case IMG2_E_INTERNAL: return "internal error";
		case IMG2_E_NOT_FOUND: return "not found";
	}
	return "unknown status";
}
