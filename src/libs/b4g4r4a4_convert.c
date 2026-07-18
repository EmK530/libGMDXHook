#if TEXTURE_MODE == 2
    #include "libs/b4g4r4a4_convert.h"

    void ConvertRGBA8ToB4G4R4A4(const uint8_t* srcRGBA, uint32_t width, uint32_t height, uint16_t* outB4G4R4A4)
    {
        uint32_t pixelCount = width * height;
        for (uint32_t i = 0; i < pixelCount; i++) {
            uint8_t r = srcRGBA[i * 4 + 0];
            uint8_t g = srcRGBA[i * 4 + 1];
            uint8_t b = srcRGBA[i * 4 + 2];
            uint8_t a = srcRGBA[i * 4 + 3];

            uint16_t r4 = r >> 4, g4 = g >> 4, b4 = b >> 4, a4 = a >> 4;

            outB4G4R4A4[i] = (a4 << 12) | (r4 << 8) | (g4 << 4) | b4;
        }
    }
#endif