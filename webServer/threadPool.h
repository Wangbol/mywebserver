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
	// ��worker��manger����Ϊ��̬��������Ϊ�˷�ֹ�ڹ��캯����ʼ����������ߡ��������߳�ʱ���Ҳ����佫���еĺ�����worker��manger���ĵ�ַ
	static void* worker(void* arg);       //�����̺߳���
	static void* manger(void* arg);      // �����̺߳���
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