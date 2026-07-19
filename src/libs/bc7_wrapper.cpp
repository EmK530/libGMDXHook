#include "external/bc7enc_rdo/bc7enc.h"

#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>

static std::once_flag bc7InitFlag;

static void InitBC7()
{
    bc7enc_compress_block_init();
}

static uint32_t GetThreadCount()
{
    uint32_t count = std::thread::hardware_concurrency();

    return count;
}

extern "C" {
void ConvertRGBA8ToBC7(const uint8_t* srcRGBA, uint32_t width, uint32_t height, uint8_t* outBC7)
{
    std::call_once(bc7InitFlag, InitBC7);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);

    params.m_max_partitions = 16;
    params.m_uber_level = 0;
    params.m_perceptual = false;
    params.m_try_least_squares = false;
    params.m_mode17_partition_estimation_filterbank = true;

    uint32_t blocksWide = width / 4;
    uint32_t blocksHigh = height / 4;
    uint32_t totalBlocks = blocksWide * blocksHigh;

    std::atomic<uint32_t> nextBlock = 0;

    auto worker = [&, params]()
    {
        while (true)
        {
            // Grab a chunk of blocks instead of one at a time
            uint32_t start = nextBlock.fetch_add(64);

            if (start >= totalBlocks)
                break;

            uint32_t end = start + 64;
            if (end > totalBlocks)
                end = totalBlocks;

            for (uint32_t block = start; block < end; block++)
            {
                uint32_t bx = block % blocksWide;
                uint32_t by = block / blocksWide;

                uint8_t blockPixels[64];

                const uint8_t* srcBlock =
                    srcRGBA + ((by * 4 * width) + (bx * 4)) * 4;

                for (int y = 0; y < 4; y++)
                {
                    memcpy(blockPixels + y * 16, srcBlock + y * width * 4, 16 );
                }

                uint8_t* outBlock = outBC7 + block * BC7ENC_BLOCK_SIZE;

                bc7enc_compress_block(outBlock, blockPixels, &params);
            }
        }
    };

    uint32_t threads = GetThreadCount();

    std::vector<std::thread> workers;
    workers.reserve(threads);

    for (uint32_t i = 0; i < threads; i++)
        workers.emplace_back(worker);

    for (auto& t : workers)
        t.join();
}
}