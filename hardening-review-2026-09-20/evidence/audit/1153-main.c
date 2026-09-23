#define MINIAUDIO_IMPLEMENTATION
#define MA_DEBUG_OUTPUT
#include "miniaudio.h"

#include <stdio.h>
#include <math.h>

int main(void)
{
    ma_result result;
    ma_engine engine;
    ma_sound sound;

    const char* filePath = "test.wav"; /* Place next to exe */
    const float listenerRotationSpeed = 8.0f;          /* radians per second */

    /* Init engine */
    result = ma_engine_init(NULL, &engine);
    if (result != MA_SUCCESS) {
        printf("Failed to init engine.\n");
        return -1;
    }

    /* Load sound */
    result = ma_sound_init_from_file(
        &engine,
        filePath,
        MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_PITCH,
        NULL,
        NULL,
        &sound
    );

    if (result != MA_SUCCESS) {
        printf("Failed to load sound.\n");
        ma_engine_uninit(&engine);
        return -2;
    }

    /* Loop + spatial */
    ma_sound_set_looping(&sound, MA_TRUE);
    ma_sound_set_spatialization_enabled(&sound, MA_TRUE);
    ma_sound_set_doppler_factor(&sound, 0.0f);

    /* Fixed positions */
    ma_sound_set_position(&sound, 2.0f, 0.0f, 0.0f);      /* sound to the side */
    ma_engine_listener_set_position(&engine, 0, 0, 0, 0); /* listener at origin */

    /* Optional attenuation */
    ma_sound_set_attenuation_model(&sound, ma_attenuation_model_inverse);
    ma_sound_set_rolloff(&sound, 0.5f);

    /* Start */
    result = ma_sound_start(&sound);
    if (result != MA_SUCCESS) {
        printf("Failed to start sound.\n");
        ma_sound_uninit(&sound);
        ma_engine_uninit(&engine);
        return -3;
    }

    printf("Running... Rotating listener.\n");

    double t = 0.0;
    while (1) {
        /* Rotate forward vector in XZ plane */
        float forwardX = cosf((float)t);
        float forwardY = 0.0f;
        float forwardZ = sinf((float)t);

        ma_engine_listener_set_direction(&engine, 0, forwardX, forwardY, forwardZ);
        ma_engine_listener_set_world_up(&engine, 0, 0.0f, 1.0f, 0.0f);

        ma_sleep(16); /* ~60Hz */
        t += listenerRotationSpeed * 0.016;
    }

    return 0;
}