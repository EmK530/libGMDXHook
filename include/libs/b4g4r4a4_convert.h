#if TEXTURE_MODE == 2
    #include <stdint.h>

    void ConvertRGBA8ToB4G4R4A4(const uint8_t* srcRGBA, uint32_t width, uint32_t height, uint16_t* outB4G4R4A4);
#endif