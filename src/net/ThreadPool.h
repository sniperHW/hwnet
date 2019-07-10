#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <thread>
#include <vector>
#include <list>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <atomic>
#include <deque>


namespace hwnet {

class Task {

public:
	typedef std::shared_ptr<Task> Ptr;
	virtual void Do() = 0;	
	virtual ~Task() {}
};


class ClosureTask : public Task,public std::enable_shared_from_this<ClosureTask> {

public:
	using Callback = std::function<void(void)>;	
	typedef std::shared_ptr<ClosureTask> Ptr;

	template<typename F, typename ...TArgs>
	static ClosureTask::Ptr New(F&& callback, TArgs&& ...args){
		return ClosureTask::Ptr(new ClosureTask(std::bind(std::forward<F>(callback),std::forward<TArgs>(args)...)));
	}

	void Do(){
		mCallback();
	}
private:

	ClosureTask(const Callback &cb):mCallback(cb){

	}

	Callback mCallback;
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
		TaskQueue(const TaskQueue&) = delete;
		TaskQueue& operator = (const TaskQueue&) = delete;
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

    /*template<typename F, typename ...TArgs>
	void PostTask(F&& callback, TArgs&& ...args){
		queue_.PostTask(std::forward<F>(callback),std::forward<TArgs>(args)...);
	}*/

	void Close() {
		queue_.Close();
	}

private:

	static void threadFunc(ThreadPool::TaskQueue *queue_);
	static void threadFuncSwap(ThreadPool::TaskQueue *queue_);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator = (const ThreadPool&) = delete;	

	std::atomic_bool inited;
	TaskQueue queue_;
	std::vector<std::thread> threads_;
};

}

#endif


