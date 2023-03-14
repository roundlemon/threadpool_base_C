#ifndef _THREADPOOL_H
#define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;

//创建线程池并初始化
ThreadPool* threadPoolCreate(int min,int max,int qsize);

//销毁线程池
int threadPoolDestroy(ThreadPool* pool);

//获取busyNum
int getPoolBusynum(ThreadPool* pool);
//获取liveNum
int getPoolLivenum(ThreadPool* pool);

//添加任务
void threadPoolAdd(ThreadPool* pool,void(*function)(void* ), void * arg);

//
void* worker(void* arg);



void* manager(void* arg);

void threadExit(ThreadPool* pool);

#endif //_THREADPOOL_H

