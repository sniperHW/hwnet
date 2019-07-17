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

	p->Add(shared_from_this(),Read);

	return true;	
}

void Poller::notifyChannel::notify() {
	static const int e = 0;
	TEMP_FAILURE_RETRY(::write(this->notifyfds[1],&e,sizeof(e)));
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

int Poller::Add(const Channel::Ptr &channel,int flag) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(this->channels.find(channel->Fd()) != this->channels.end()) {
		return -1;
	} else {
		this->channels[channel->Fd()] = channel;
		return poller_.Add(channel,flag);
	}
}

void Poller::Remove(const Channel::Ptr &channel) {
	this->mtx.lock();
	auto it = this->channels.find(channel->Fd());
	if(it == this->channels.end()){
		this->mtx.unlock();
		return;		
	}
	this->waitRemove.push_back(channel);
	this->channels.erase(it);
	poller_.Remove(channel);
	++clearWaitRemove;
	this->mtx.unlock();
	this->notifyChannel_->notify();
}

int Poller::Enable(const Channel::Ptr &channel,int flag,int oldEvents) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(this->channels.find(channel->Fd()) == this->channels.end()) {
		return oldEvents;
	} else {
		return poller_.Enable(channel,flag,oldEvents);
	}
}

int Poller::Disable(const Channel::Ptr &channel,int flag,int oldEvents) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(this->channels.find(channel->Fd()) == this->channels.end()) {
		return oldEvents;
	} else {	
		return poller_.Disable(channel,flag,oldEvents);
	}
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
			if(clearWaitRemove.load() != 0) {
				std::lock_guard<std::mutex> guard(this->mtx);
				clearWaitRemove.store(0);
				waitRemove.clear();
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
