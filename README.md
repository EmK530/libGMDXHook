# libGMDXHook
Custom Direct3D11 replacement that intercepts GameMaker's allocation of texture atlases on the GPU and converts them from 8-bit RGBA to either BC1 or BC7.
> [!CAUTION]
>  This library is only recommended for use on low VRAM devices when absolutely necessary.<br>
>  Texture compression has a general quality loss and isn't recommended for regular use.

## Attributions
- BC7 Compressor: [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) - Copyright(c) 2020-2021 Richard Geldreich, Jr. (MIT or public domain)
- BC1 Compressor: [stb_dxt](https://github.com/nothings/stb) - Original by Fabian "ryg" Giesen, ported to C by Sean Barrett (public domain)
- Hashing Algorithm: [xxHash](https://github.com/Cyan4973/xxHash) - Copyright (C) 2012-2023 Yann Collet (BSD 2-Clause)
- JSON Parsing: [cJSON](https://github.com/DaveGamble/cJSON) - Copyright (c) 2009-2017 Dave Gamble and cJSON contributors (MIT)
- PNG Encoder: [stb_image_write](https://github.com/nothings/stb) - Copyright (c) 2017 Sean Barrett (MIT or public domain)

## How to use
- Add the `gmdxh.dll` file to your game folder of choice.
- Open the game executable in a hex editor like HxD.
- Locate the string `d3d11.dll` in the file.
- Replace the string with `gmdxh.dll`.
- The library should be ready! Try opening the game and observe VRAM usage.
