#pragma once
#include<iostream>
#include<pthread.h>
#include<unistd.h>
#include<queue>
#include "taskQueue.h"
using namespace std;

class ThreadPool
{
public:
	ThreadPool(int min, int max);
	~ThreadPool();
	void addTask(Task task);
private:
	// 将worker和manger声明为静态函数，是为了防止在构造函数初始化创造管理者、工作者线程时，找不到其将运行的函数（worker、manger）的地址
	static void* worker(void* arg);       //工作线程函数
	static void* manger(void* arg);      // 管理线程函数
	void threadPoolExit();

	pthread_t mangerID;
	pthread_t* threadIDs;
	pthread_mutex_t threadPoolLock;
	pthread_cond_t notEmpty;

	TaskQueue* taskQ;

	int m_minNum;
	int m_maxNum;
	int m_busyNum;
	int m_aliveNum;
	int m_exitNum;
	bool m_shutdown = false;
};