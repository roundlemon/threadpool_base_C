#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const int NUMBER = 2;

typedef struct Task
{
    void (*function)(void *arg);
    void *args;
} Task;

struct ThreadPool
{
    // 任务队列
    Task *taskQ;
    int queueCapacity; // 容量
    int queueSize;     // 当前任务数
    int queueFront;    // 队头->取数据
    int queueRear;     // 队尾->放数据

    pthread_t managerID;       // 管理者线程ID
    pthread_t *threadID;       // 工作的线程ID
    int minNum;                // 最小线程数量
    int maxNum;                // 最大线程数量
    int busyNum;               // 忙的线程数量
    int liveNum;               // 存活的线程数量
    int exitNum;               // 要销毁的线程数量
    pthread_mutex_t mutexPool; // 锁整个线程池
    pthread_mutex_t mutexBusy; // 锁busyNum;
    pthread_cond_t notFull;    // 线程池是否满了
    pthread_cond_t notEmpty;   // 线程池是否空了

    int shutdown; // 是否销毁线程池
};

ThreadPool *threadPoolCreate(int min, int max, int qsize)
{
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    do
    {
        if (pool == nullptr)
        {
            printf("malloc threadpool fail...\n");
            break;
        }
        pool->threadID = (pthread_t *)malloc(sizeof(pthread_t) * max);
        if (pool->threadID == nullptr)
        {
            printf("malloc threadID fail...\n");
            break;
        }
        memset(pool->threadID, 0, sizeof(pthread_t) * max);
        pool->busyNum = 0;
        pool->liveNum = min;
        pool->exitNum = 0;
        pool->maxNum = max;
        pool->minNum = min;

        if (pthread_mutex_init(&pool->mutexPool, nullptr) ||
            pthread_mutex_init(&pool->mutexBusy, nullptr) ||
            pthread_cond_init(&pool->notFull, nullptr) ||
            pthread_cond_init(&pool->notEmpty, nullptr))
        {
            printf("mutex or condition init fail...\n");
            break;
        }

        pool->taskQ = (Task *)malloc(sizeof(Task) * qsize);
        if (pool->taskQ == nullptr)
        {
            printf("malloc taskQ fail...\n");
            break;
        }
        pool->queueCapacity = qsize;
        pool->queueSize = 0;
        pool->queueFront = 0;
        pool->queueRear = 0;

        pool->shutdown = 0;

        pthread_create(&pool->managerID, nullptr, manager, pool);
        for (int i = 0; i < min; i++)
        {
            pthread_create(&pool->threadID[i], nullptr, worker, pool);
        }
        return pool;
    } while (0);
    if (pool && pool->taskQ)
    {
        free(pool->taskQ);
    }
    if (pool && pool->threadID)
    {
        free(pool->threadID);
    }
    if (pool)
    {
        free(pool);
    }
    return nullptr;
}

void *worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    while (1)
    {
        pthread_mutex_lock(&pool->mutexPool);
        while (pool->queueSize == 0 && !pool->shutdown)
        {
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
            if (pool->exitNum > 0)
            {
                pool->exitNum--;
                if (pool->exitNum > pool->minNum)
                {
                    pool->exitNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }
        }

        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutexPool);
            threadExit(pool);
        }
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.args = pool->taskQ[pool->queueFront].args;

        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity; //?????为啥不是queuesize?
        pool->queueSize--;
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);

        printf("thread %ld start working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        task.function(task.args);
        free(task.args);
        task.args = nullptr;

        printf("thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
    }
    return nullptr;
}

void *manager(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    while (!pool->shutdown)
    {
        sleep(3);
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNumber = pool->liveNum;
        int busyNumber = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexPool);
        // 任务的个数>存活的线程个数 && 存活的线程数<最大线程数
        if (queueSize > liveNumber && liveNumber < pool->maxNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            int counter = 0;
            for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++)
            {
                if (pool->threadID[i] == 0)
                {
                    pthread_create(&pool->threadID[i], nullptr, worker, pool);
                    pool->liveNum++;
                    counter++;
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }
        // 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
        if (busyNumber * 2 < liveNumber && liveNumber > pool->minNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            for (int i = 0; i < NUMBER; i++)
            {
                pthread_cond_signal(&pool->notEmpty);
            }
        }
    }
    return nullptr;
}

void threadExit(ThreadPool *pool)
{
    pthread_t pid = pthread_self();
    for (int i = 0; i < pool->maxNum; i++)
    {
        if (pool->threadID[i] == pid)
        {
            pool->threadID[i] = 0;
            printf("threadExit() called,%ld exiting...\n", pid);
            break;
        }
    }
    pthread_exit(nullptr);
}

void threadPoolAdd(ThreadPool *pool, void (*function)(void *), void *arg)
{
    pthread_mutex_lock(&pool->mutexPool);
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    pool->taskQ[pool->queueRear].function = function;
    pool->taskQ[pool->queueRear].args = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    pthread_cond_signal(&pool->notEmpty);

    pthread_mutex_unlock(&pool->mutexPool);
}

int getPoolBusynum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int busyn = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyn;
}
int getPoolLivenum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int liven = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return liven;
}

int threadPoolDestroy(ThreadPool *pool)
{
    if (pool == nullptr)
        return -1;
    pool->shutdown = 1;
    pthread_join(pool->managerID, nullptr);
    for (int i = 0; i < pool->liveNum; i++)
    {
        pthread_cond_signal(&pool->notEmpty);
    }
    if (pool->taskQ)
    {
        free(pool->taskQ);
    }
    if(pool->threadID)
    {
        free(pool->threadID);
    }
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);
    free(pool);
    pool = nullptr;
    return 0;
}