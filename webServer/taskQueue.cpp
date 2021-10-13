#include "taskQueue.h"

Task::Task()
{
	this->func = NULL;
	this->arg = NULL;
}

Task::Task(callback fun, int* arg)
{
	this->func = fun;
	this->arg = arg;
}

TaskQueue::TaskQueue()
{
	pthread_mutex_init(&taskMutex, NULL);
}

TaskQueue::~TaskQueue()
{
	pthread_mutex_destroy(&taskMutex);
}

void TaskQueue::addTask(Task& task)
{
	pthread_mutex_lock(&taskMutex);
	taskQueue.push(task);
	pthread_mutex_unlock(&taskMutex);
}

Task TaskQueue::takeTask()
{
	Task task;
	pthread_mutex_lock(&taskMutex);
	if (taskQueue.size() > 0)
	{
		task = taskQueue.front();
		taskQueue.pop();
	}
	pthread_mutex_unlock(&taskMutex);
	return task;
}

