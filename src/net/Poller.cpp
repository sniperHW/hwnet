#include "Poller.h"
#include "temp_failure_retry.h"
#include "SocketHelper.h"
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

namespace hwnet {

void Poller::notifyChannel::OnActive(int _) {
	(void)_;
	static int tmp[1024];
	while(TEMP_FAILURE_RETRY(::read(this->notifyfds[0],&tmp,sizeof(tmp))) > 0);
}	

bool Poller::notifyChannel::init(Poller *p) {

#ifdef _LINUX
	if(pipe2(this->notifyfds,O_NONBLOCK | O_CLOEXEC) != 0) {
	    return false;	
	}
#elif _MACOS
	if(pipe(this->notifyfds) != 0){
		return false;
	}
	
	SetNoBlock(this->notifyfds[0],true);
	SetCloseOnExec(this->notifyfds[0]);

	SetNoBlock(this->notifyfds[1],true);
	SetCloseOnExec(this->notifyfds[1]);

#endif

	p->Add(shared_from_this(),addRead);

	return true;	
}

void Poller::notifyChannel::notify() {
	static const int e = 0;
	TEMP_FAILURE_RETRY(::write(this->notifyfds[1],&e,sizeof(e)));
}

void Poller::Remove(const Channel::Ptr &channel) {
	poller_.Remove(channel);
	{
		std::lock_guard<std::mutex> guard(this->mtx);
		this->waitRemove.push_back(channel);
	}
	this->notifyChannel_->notify();
}

bool Poller::Init(ThreadPool *pool) {

	auto poolCreateByNew_ = false;

	if(nullptr == pool) {
		pool = new ThreadPool;
		pool->Init(std::thread::hardware_concurrency()*2);
		poolCreateByNew_ = true;
	}

	bool expected = false;
	if(!this->inited.compare_exchange_strong(expected,true)) {
		if(poolCreateByNew_) {
			delete pool;
		}
		return false;
	}

	this->pool_ = pool;

	if(!poller_.Init()) {
		if(poolCreateByNew_) {
			delete pool;
		}
		this->pool_ = nullptr;	
		return false;
	}

	this->notifyChannel_ = notifyChannel::Ptr(new notifyChannel);

	if(!this->notifyChannel_->init(this)) {
		if(poolCreateByNew_) {
			delete pool;
		}
		this->pool_ = nullptr;	
		this->notifyChannel_ = nullptr;
		return false;
	}

	this->poolCreateByNew = poolCreateByNew_;

	return true;	
}

void Poller::Add(const Channel::Ptr &channel,int flag) {
	std::lock_guard<std::mutex> guard(this->mtx);
	poller_.Add(channel,flag);
	this->channels[channel->Fd()] = channel;
}

void Poller::PostTask(const Task::Ptr &task,ThreadPool *tpool) {
	if(tpool) {
		tpool->PostTask(task);
	} else {
		this->pool_->PostTask(task);
	}
}

void Poller::Run() {
	bool expected = false;
	if(this->running.compare_exchange_strong(expected,true)){
		while(this->closed.load() == false) {
			auto ret = poller_.RunOnce();
			std::lock_guard<std::mutex> guard(this->mtx);
			while(!this->waitRemove.empty()) {
				auto c = this->waitRemove.front();
				this->channels.erase(c->Fd());
				this->waitRemove.pop_front();
			}		
			if(ret != 0) {
				break;
			}
		}
		this->running.store(false);
	}	
}

void Poller::Stop() {
	this->closed.store(true);
	this->notifyChannel_->notify();
}

}
