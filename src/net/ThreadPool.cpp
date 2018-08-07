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

void ThreadPool::TaskQueue::Close() {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed) {
		this->closed = true;
		this->cv.notify_all();
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
		--this->watting;//减少正在等待的线程数量
		this->cv.notify_one();
	}
}

bool ThreadPool::Init(int threadCount) {

	bool expected = false;
	if(!this->inited.compare_exchange_strong(expected,true)) {
		return false;
	}

	if(threadCount <= 0) {
		threadCount = 1;
	}

	for(auto i = 0; i < threadCount; ++i) {
		threads_.push_back(std::thread(threadFunc,&queue_));
	}

	return true;
}

}


