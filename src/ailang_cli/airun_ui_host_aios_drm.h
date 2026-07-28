#ifndef AIRUN_UI_HOST_AIOS_DRM_H
#define AIRUN_UI_HOST_AIOS_DRM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int bits_per_pixel;
    size_t line_length;
    size_t bytes;
    uint8_t* memory;
} AirunAiosDrmSurface;

int airun_aios_drm_requested(void);
int airun_aios_drm_open(AirunAiosDrmSurface* out_surface);
int airun_aios_drm_active(void);
uint8_t* airun_aios_drm_begin_present(AirunAiosDrmSurface* out_surface);
void airun_aios_drm_end_present(void);
void airun_aios_drm_shutdown(void);

#endif
