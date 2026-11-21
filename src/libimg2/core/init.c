/**
 * @file init.c
 * @brief img2_init() / img2_shutdown(): reference-counted process-global setup
 *
 * No API requires img2_init(): the codec tables are const and the font cache
 * initialises lazily. img2_init() emits the INFO registry lists; the final
 * img2_shutdown() frees the font cache. Any other img2_* call running
 * concurrently with the final img2_shutdown() is unsupported.
 */

#include <pthread.h>

#include "status.h"
#include "../decoders/decoder.h"
#include "../encoders/encoder.h"
#include "../text/font_manager.h"

/* [R19] init/shutdown bodies are serialised by s_init_lock; s_refs changes only under it. */
static pthread_mutex_t s_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_refs; /* guarded by s_init_lock */

img2_status_t img2_init(void)
{
	pthread_mutex_lock(&s_init_lock);
	if (s_refs++ == 0) {
		img2_encoder_registry_init(); /* encoders before decoders: main.c order */
		img2_decoder_registry_init();
	}
	pthread_mutex_unlock(&s_init_lock);
	return IMG2_OK;
}

void img2_shutdown(void)
{
	pthread_mutex_lock(&s_init_lock);
	if (s_refs > 0 && --s_refs == 0) { /* [R5] unbalanced shutdown at 0 is ignored */
		img2_font_manager_cleanup_final(&s_refs); /* re-checks s_refs == 0 under s_font_lock */
	}
	pthread_mutex_unlock(&s_init_lock);
}
