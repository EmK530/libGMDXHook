# libGMDXHook
Custom Direct3D11 replacement that intercepts GameMaker's allocation of texture atlases on the GPU and converts them from R8G8B8A8_UNORM to BC7_UNORM.
> [!CAUTION]
>  This library is only recommended for use on low VRAM devices when necessary.<br>
>  Due to the nature of BC7 compression, boot times are way longer even with multithreading.

## Attributions
- BC7 Compressor: [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) - Copyright(c) 2020-2021 Richard Geldreich, Jr.

## How to use
- Add the `gmdxh.dll` file to your game folder of choice.
- Open the game executable in a hex editor like HxD.
- Locate the string `d3d11.dll` in the file.
- Replace the string with `gmdxh.dll`.
- The library should be ready! Try opening the game and observe VRAM usage.
