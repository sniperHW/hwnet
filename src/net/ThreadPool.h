#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <thread>
#include <vector>
#include <list>
#include <condition_variable>
#include <memory>
#include <mutex>
#include "SpinMutex.h"
#include <atomic>
#include <deque>


namespace hwnet {

class Task {

public:
	typedef std::shared_ptr<Task> Ptr;
	virtual void Do() = 0;	
	virtual ~Task() {}
};



class ThreadPool {

	//任务队列
	class TaskQueue {

	public:

		typedef std::deque<Task::Ptr> taskque;

		TaskQueue():closed(false),watting(0){}

		void PostTask(const Task::Ptr &task);

		void Close();

		Task::Ptr Get();

		bool Get(taskque &out);

	private:
		TaskQueue(const TaskQueue&);
		TaskQueue& operator = (const TaskQueue&);
		bool closed;
		int  watting;//空闲线程数量
		std::mutex mtx;
		std::condition_variable_any cv;
		taskque tasks;
	};

public:

	static const int SwapMode = 1;

	ThreadPool():inited(false){}

	~ThreadPool();

	bool Init(int threadCount = 0,int swapMode = 0);

	void PostTask(const Task::Ptr &task) {
		queue_.PostTask(task);
	}

	void Close() {
		queue_.Close();
	}

private:

	static void threadFunc(ThreadPool::TaskQueue *queue_);
	static void threadFuncSwap(ThreadPool::TaskQueue *queue_);

	ThreadPool(const ThreadPool&);
	ThreadPool& operator = (const ThreadPool&);	

	std::atomic_bool inited;
	TaskQueue queue_;
	std::vector<std::thread> threads_;
};

}

#endif

