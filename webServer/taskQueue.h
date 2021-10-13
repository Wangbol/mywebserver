#pragma once
#include<iostream>
//#include<unistd.h>
#include<queue>
using namespace std;

typedef void (*callback)(int*);
class Task
{
public:
	Task();
	Task(callback fun, int* arg);
	callback func;
	int* arg;
};

class TaskQueue
{
public:
	TaskQueue();
	~TaskQueue();
	void addTask(Task& task);        // 添加任务
	Task takeTask();                // 取出任务
	int getTaskNum()               // 任务队列中任务的数量
	{
		return taskQueue.size();
	}

private:
	pthread_mutex_t taskMutex;
	queue<Task> taskQueue;
};