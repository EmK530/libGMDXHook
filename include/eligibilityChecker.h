#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <dxgicommon.h>

void EC_Init();

bool EC_IsBC1Eligible(const uint8_t* pSrcData, UINT pSrcDepth); 