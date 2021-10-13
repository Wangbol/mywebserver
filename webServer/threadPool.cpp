#include "threadPool.h"
#include <string.h>
const int THREADNUMBER = 2;   // ������ÿ�����ӻ�ɾ�����̳߳����߳���
ThreadPool::ThreadPool(int min, int max)
{
	taskQ = new TaskQueue;    //ʵ�����������
	do
	{
		//��ʼ���̳߳ظ�����
		this->m_maxNum = max;
		this->m_minNum = min;
		this->m_busyNum = 0;
		this->m_aliveNum = min;
		this->m_exitNum = 0;
		// ���̳߳ط���ռ�
		this->threadIDs = new pthread_t[max];
		if (threadIDs == NULL)
		{
			cout << "�̳߳��ڴ����ʧ��" << endl;
			break;
		}
		//��ʼ�������̳߳����ڴ���0
		memset(threadIDs, 0, sizeof(pthread_t) * max);
		//��ʼ������������������
		if (pthread_mutex_init(&this->threadPoolLock, NULL) != 0 || pthread_cond_init(&this->notEmpty, NULL) != 0)
		{
			cout << "������������������ʼ��ʧ��..." << endl;
			break;
		}
		// ��ʼ���ո�������С�������̳߳��е��߳�����----�������߳�
		for (int i = 0; i < min; i++)
		{
			pthread_create(&this->threadIDs[i], NULL, worker, this);
			cout << "�����������̣߳�id=" << (this->threadIDs[i]) << endl;
		}
		//�����������߳�
		pthread_create(&this->mangerID, NULL, manger, this);
	} while (0);
}

ThreadPool::~ThreadPool()
{
	this->m_shutdown = true;   // �̳߳عرա����ٱ�־
	pthread_join(this->mangerID, NULL);     //�ȴ��������ٹ������߳�
	//�������й������̣߳������Ի�
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
	this->taskQ->addTask(task);    //�������ʱʹ�õĻ���������������е������������ʵ��
	pthread_cond_signal(&this->notEmpty);    // �������󣬻��ѹ������߳�
}

void* ThreadPool::worker(void* arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);    //��void���͵�thisָ��ת����ThreadPool����
	while (1)
	{
		pthread_mutex_lock(&pool->threadPoolLock);      //���ʹ�����Դʱ������
		// �ж���������Ƿ�Ϊ�գ���Ϊ�����߳������ȴ�
		while (pool->taskQ->getTaskNum() == 0 && !pool->m_shutdown)
		{
			pthread_cond_wait(&pool->notEmpty, &pool->threadPoolLock);

			// �����ִ��߳��������������ıȽϣ��õ�m_exitNum���ж��Ƿ������߳�
			if (pool->m_exitNum > 0)
			{
				pool->m_exitNum--;
				if (pool->m_aliveNum > pool->m_minNum)    //��֤�߳���������Ϊ�������Сֵ
				{
					pool->m_aliveNum--;
					pthread_mutex_unlock(&pool->threadPoolLock);
					pool->threadPoolExit();
				}
			}
		}
		//�Ƿ�ر��̳߳�
		if (pool->m_shutdown)
		{
			pthread_mutex_unlock(&pool->threadPoolLock);
			pool->threadPoolExit();
		}
		//�����̴߳�������
		Task task = pool->taskQ->takeTask();       // �����������ȡ��һ������
		pool->m_busyNum++;                        // �����߳�����һ
		pthread_mutex_unlock(&pool->threadPoolLock);   //������Դ���ʽ���������

		//ִ������
		task.func(task.arg);
		// �������Ĳ��������ڶ�����ִ��������ͷ���ռ�
		delete []task.arg;
		task.arg = NULL;
		// ����ִ������轫æµ�Ĺ����߳�����һ
		pthread_mutex_lock(&pool->threadPoolLock);
		pool->m_busyNum--;
		pthread_mutex_unlock(&pool->threadPoolLock);
	}
	return nullptr;
}

void* ThreadPool::manger(void* arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	//���̳߳�û�йرգ���ѭ����⹤���߳��������������Ա����ӻ�ɾ���߳�
	while (!pool->m_shutdown)
	{
		sleep(3);            //ÿ3����һ��
		pthread_mutex_lock(&pool->threadPoolLock);
		int taskNum = pool->taskQ->getTaskNum();     // ��ȡ��ǰ������
		int liveNum = pool->m_aliveNum;             //  ��ȡ�ִ���̳߳����߳���
		int busyNum = pool->m_busyNum;              //  ��ȡ���е��̳߳������ڹ������߳���
		pthread_mutex_unlock(&pool->threadPoolLock);
		// ��ǰ������ > �̳߳����ִ���߳����������ִ���߳��� < ���������̸߳���ʱ�������߳�
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
		//���ڹ������߳���*2 < �ִ���߳���Ŀ�������ִ���߳��� > ��С�߳���ʱ�������̳߳��е��߳�
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
	pthread_t tid = pthread_self();      // ��ȡҪ�����̵߳�id
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
