#include "threadPool.h"
#include <string.h>
const int THREADNUMBER = 2;   // 管理者每次增加或删除的线程池中线程数
ThreadPool::ThreadPool(int min, int max)
{
	taskQ = new TaskQueue;    //实例化任务队列
	do
	{
		//初始化线程池各参数
		this->m_maxNum = max;
		this->m_minNum = min;
		this->m_busyNum = 0;
		this->m_aliveNum = min;
		this->m_exitNum = 0;
		// 给线程池分配空间
		this->threadIDs = new pthread_t[max];
		if (threadIDs == NULL)
		{
			cout << "线程池内存分配失败" << endl;
			break;
		}
		//初始化，将线程池中内存置0
		memset(threadIDs, 0, sizeof(pthread_t) * max);
		//初始化互斥锁、条件变量
		if (pthread_mutex_init(&this->threadPoolLock, NULL) != 0 || pthread_cond_init(&this->notEmpty, NULL) != 0)
		{
			cout << "互斥锁、条件变量初始化失败..." << endl;
			break;
		}
		// 初始按照给定的最小数创建线程池中的线程数量----工作者线程
		for (int i = 0; i < min; i++)
		{
			pthread_create(&this->threadIDs[i], NULL, worker, this);
			cout << "创建工作者线程：id=" << (this->threadIDs[i]) << endl;
		}
		//创建管理者线程
		pthread_create(&this->mangerID, NULL, manger, this);
	} while (0);
}

ThreadPool::~ThreadPool()
{
	this->m_shutdown = true;   // 线程池关闭、销毁标志
	pthread_join(this->mangerID, NULL);     //等待回收销毁管理者线程
	//唤醒所有工作者线程，让其自毁
	for (int i = 0; i < this->m_aliveNum; i++)
	{
		pthread_cond_signal(&this->notEmpty);
	}
	if (this->taskQ)
	{
		delete this->taskQ;
		this->taskQ = NULL;
	}
	if (this->threadIDs)
	{
		delete[] this->threadIDs;
		this->threadIDs = NULL;
	}
	pthread_mutex_destroy(&this->threadPoolLock);
	pthread_cond_destroy(&this->notEmpty);

}

void ThreadPool::addTask(Task task)
{
	if (this->m_shutdown)
	{
		return;
	}
	this->taskQ->addTask(task);    //添加任务时使用的互斥锁已在任务队列的添加任务函数中实现
	pthread_cond_signal(&this->notEmpty);    // 添加任务后，唤醒工作者线程
}

void* ThreadPool::worker(void* arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);    //将void类型的this指针转换成ThreadPool类型
	while (1)
	{
		pthread_mutex_lock(&pool->threadPoolLock);      //访问公共资源时需上锁
		// 判断任务队列是否为空，若为空则线程阻塞等待
		while (pool->taskQ->getTaskNum() == 0 && !pool->m_shutdown)
		{
			pthread_cond_wait(&pool->notEmpty, &pool->threadPoolLock);

			// 根据现存线程数量与任务数的比较，得到m_exitNum来判断是否销毁线程
			if (pool->m_exitNum > 0)
			{
				pool->m_exitNum--;
				if (pool->m_aliveNum > pool->m_minNum)    //保证线程数量至少为定义的最小值
				{
					pool->m_aliveNum--;
					pthread_mutex_unlock(&pool->threadPoolLock);
					pool->threadPoolExit();
				}
			}
		}
		//是否关闭线程池
		if (pool->m_shutdown)
		{
			pthread_mutex_unlock(&pool->threadPoolLock);
			pool->threadPoolExit();
		}
		//工作线程处理任务
		Task task = pool->taskQ->takeTask();       // 从任务队列中取出一个任务
		pool->m_busyNum++;                        // 工作线程数加一
		pthread_mutex_unlock(&pool->threadPoolLock);   //公共资源访问结束，解锁

		//执行任务
		task.func(task.arg);
		// 任务函数的参数定义在堆区，执行完后需释放其空间
		delete []task.arg;
		task.arg = NULL;
		// 任务执行完后，需将忙碌的工作线程数减一
		pthread_mutex_lock(&pool->threadPoolLock);
		pool->m_busyNum--;
		pthread_mutex_unlock(&pool->threadPoolLock);
	}
	return nullptr;
}

void* ThreadPool::manger(void* arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	//若线程池没有关闭，则循环检测工作线程数与任务数，以便增加或删除线程
	while (!pool->m_shutdown)
	{
		sleep(3);            //每3秒检测一次
		pthread_mutex_lock(&pool->threadPoolLock);
		int taskNum = pool->taskQ->getTaskNum();     // 获取当前任务数
		int liveNum = pool->m_aliveNum;             //  获取现存的线程池中线程数
		int busyNum = pool->m_busyNum;              //  获取现有的线程池中正在工作的线程数
		pthread_mutex_unlock(&pool->threadPoolLock);
		// 当前任务数 > 线程池中现存的线程数，并且现存的线程数 < 定义的最大线程个数时，增加线程
		if (taskNum > liveNum && liveNum < pool->m_maxNum)
		{
			pthread_mutex_lock(&pool->threadPoolLock);
			int num = 0;
			for (int i = 0; i < pool->m_maxNum && num < THREADNUMBER && pool->m_aliveNum < pool->m_maxNum; i++)
			{
				if (pool->threadIDs[i] == 0)
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					num++;
					pool->m_aliveNum++;
				}
			}
			pthread_mutex_unlock(&pool->threadPoolLock);
		}
		//正在工作的线程数*2 < 现存的线程数目，并且现存的线程数 > 最小线程数时，销毁线程池中的线程
		if (busyNum * 2 < liveNum && liveNum > pool->m_minNum)
		{
			pthread_mutex_lock(&pool->threadPoolLock);
			pool->m_exitNum = THREADNUMBER;
			pthread_mutex_unlock(&pool->threadPoolLock);
			for (int i = 0; i < THREADNUMBER; i++)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return nullptr;
}

void ThreadPool::threadPoolExit()
{
	pthread_t tid = pthread_self();      // 获取要销毁线程的id
	for (int i = 0; i < this->m_maxNum; i++)
	{
		if (this->threadIDs[i] == tid)
		{
			this->threadIDs[i] = 0;
			break;
		}
	}
	pthread_exit(NULL);
}
