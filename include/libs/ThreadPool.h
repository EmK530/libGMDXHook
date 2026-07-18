#pragma once

#include <windows.h>
#include <stdint.h>

typedef struct {
    HANDLE semaphore;
    uint32_t threadLimit;
} ThreadPool;

ThreadPool* ThreadPool_Init();
void ThreadPool_Dispose(ThreadPool* pool);
void ThreadPool_Run(ThreadPool* pool, LPTHREAD_START_ROUTINE function, void* argument);