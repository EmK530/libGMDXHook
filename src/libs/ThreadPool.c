#include "libs/ThreadPool.h"
#include <stdlib.h>

uint32_t GetCPUThreadCount()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);

    return info.dwNumberOfProcessors;
}

typedef struct {
    ThreadPool* pool;
    LPTHREAD_START_ROUTINE function;
    void* argument;
} ThreadData;

DWORD WINAPI ThreadPool_Wrapper(void* ptr)
{
    ThreadData* data = ptr;

    data->function(data->argument);

    ReleaseSemaphore(data->pool->semaphore, 1, NULL);

    free(data);

    return 0;
}

ThreadPool* ThreadPool_Init()
{
    ThreadPool* pool = calloc(1, sizeof(ThreadPool));

    uint32_t threads = 2 - 1;
    
    if(threads == 0)
        return NULL;

    pool->threadLimit = threads;

    pool->semaphore = CreateSemaphore(NULL, threads, threads, NULL);

    return pool;
}

void ThreadPool_Dispose(ThreadPool* pool)
{
    if (!pool)
        return;

    CloseHandle(pool->semaphore);

    free(pool);
}

void ThreadPool_Run(ThreadPool* pool, LPTHREAD_START_ROUTINE function, void* argument)
{
    // Blocks here if all slots are occupied
    WaitForSingleObject(pool->semaphore, INFINITE);

    ThreadData* data = malloc(sizeof(ThreadData));

    data->pool = pool;
    data->function = function;
    data->argument = argument;

    HANDLE thread = CreateThread(NULL, 0, ThreadPool_Wrapper, data, 0, NULL);

    CloseHandle(thread);
}