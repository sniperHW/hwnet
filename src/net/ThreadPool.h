#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <thread>
#include <vector>
#include <list>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <atomic>


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

		TaskQueue():closed(false),watting(0){}

		void PostTask(const Task::Ptr &task);

		void Close();

		Task::Ptr Get();

	private:
		TaskQueue(const TaskQueue&);
		TaskQueue& operator = (const TaskQueue&);
		bool closed;
		int  watting;//空闲线程数量
		std::mutex mtx;
		std::condition_variable_any cv;
		std::list<Task::Ptr> tasks;
	};

public:

	ThreadPool():inited(false){}

	bool Init(int threadCount = 0);

	void PostTask(const Task::Ptr &task) {
		queue_.PostTask(task);
	}

	void Close() {
		queue_.Close();
	}

private:

	static void threadFunc(ThreadPool::TaskQueue *queue_);

	ThreadPool(const ThreadPool&);
	ThreadPool& operator = (const ThreadPool&);	

	std::atomic_bool inited;
	TaskQueue queue_;
	std::vector<std::thread> threads_;
};

}

#endif


