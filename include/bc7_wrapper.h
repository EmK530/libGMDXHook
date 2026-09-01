#ifndef BC7_WRAPPER_H
#define BC7_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ConvertRGBA8ToBC7(const uint8_t* srcRGBA, uint32_t width, uint32_t height, uint8_t* outBC7);
void BC7Init();

#ifdef __cplusplus
}
#endif

#endif