#include "ThreadPool.h"
#include <stdio.h>

namespace hwnet {

void ThreadPool::threadFunc(ThreadPool::TaskQueue *queue_) {
	for(;;) {
		Task::Ptr task = queue_->Get();
		if(task == nullptr) {
			return;
		}
		task->Do();
	}
}


void ThreadPool::threadFuncSwap(ThreadPool::TaskQueue *queue_) {
	ThreadPool::TaskQueue::taskque local_tasks;
	for(;;) {
		if(!queue_->Get(local_tasks)) {
			return;
		}
		while(!local_tasks.empty()) {
			const Task::Ptr &task = local_tasks.front();
			task->Do();
			local_tasks.pop_front();
		}
	}
}


void ThreadPool::TaskQueue::Close() {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed) {
		this->closed = true;
		this->cv.notify_all();
	}
}


bool ThreadPool::TaskQueue::Get(ThreadPool::TaskQueue::taskque &out) {
	std::lock_guard<std::mutex> guard(this->mtx);
	for( ; ;) {
		if(this->closed) {
			if(this->tasks.empty()) {
				return false;
			} else {
				this->tasks.swap(out);
				return true;
			}
		} else {
			if(this->tasks.empty()) {
				++this->watting;//增加等待数量
				this->cv.wait(this->mtx);
				--this->watting;//减少正在等待的线程数量
			} else {
				this->tasks.swap(out);
				return true;
			}	
		}
	}
}


Task::Ptr ThreadPool::TaskQueue::Get() {
	std::lock_guard<std::mutex> guard(this->mtx);
	for( ; ;) {
		if(this->closed) {
			if(this->tasks.empty()) {
				return nullptr;
			} else {
				Task::Ptr &task = this->tasks.front();
				this->tasks.pop_front();
				return task;
			}
		} else {

			if(this->tasks.empty()) {
				++this->watting;//增加等待数量
				this->cv.wait(this->mtx);
				--this->watting;//减少正在等待的线程数量	
			} else {
				Task::Ptr task = this->tasks.front();
				this->tasks.pop_front();
				return task;
			}	
		}
	}
}


void ThreadPool::TaskQueue::PostTask(const Task::Ptr &task) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(this->closed) {
		return;
	}	
	this->tasks.push_back(task);
	if(this->watting > 0) {
		this->cv.notify_one();
	}
}

bool ThreadPool::Init(int threadCount,int swapMode) {

	bool expected = false;
	if(!this->inited.compare_exchange_strong(expected,true)) {
		return false;
	}

	if(threadCount <= 0) {
		threadCount = 1;
	}

	for(auto i = 0; i < threadCount; ++i) {
		if(swapMode == 0){
			threads_.push_back(std::thread(threadFunc,&queue_));
		} else {
			threads_.push_back(std::thread(threadFuncSwap,&queue_));
		}
	}

	return true;
}

ThreadPool::~ThreadPool() {
	queue_.Close();
	size_t i = 0;
	for( ; i < threads_.size(); ++i) {
		threads_[i].join();
	}	
}

}


