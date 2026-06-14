#pragma once
#include <stdbool.h>

/* Called (from the detect task) each time the wake word fires. Keep it quick. */
typedef void (*wakenet_detected_cb_t)(void);

/* Loads the WakeNet model, creates AFE, and starts the feed + detect tasks.
   board_audio_init() must have been called first. Returns true on success. */
bool wakenet_start(wakenet_detected_cb_t on_detected);
