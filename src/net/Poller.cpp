#include "Poller.h"

namespace hwnet {

bool Poller::Init(int threadCount) {

	bool expected = false;
	if(!this->inited.compare_exchange_strong(expected,true)) {
		return false;
	}

	if(!pool_.Init(threadCount)) {
		return false;
	}

	if(!poller_.Init()) {
		return false;
	}

	return true;	
}


void Poller::processFinish() {
	this->processing = false;
	this->mtx.lock();
	auto it = this->removeing.begin();
	auto end = this->removeing.end();
	for(; it != end; ++it){
		this->channels.erase(*it);
	}
	this->removeing.clear();
	this->mtx.unlock();	
}

void Poller::Add(const Channel::Ptr &channel) {
	this->mtx.lock();
	poller_.Add(channel);
	this->channels[channel->Fd()] = channel;
	this->mtx.unlock();
}

void Poller::Remove(const Channel::Ptr &channel) {
	this->mtx.lock();
	poller_.Remove(channel);	
	if(!this->processing){
		this->channels.erase(channel->Fd());
	} else{
		this->removeing.push_back(channel->Fd());
	}
	this->mtx.unlock();
}

void Poller::PostTask(const Task::Ptr &task) {
	pool_.PostTask(task);
}

void Poller::Run() {
	bool expected = false;
	if(this->running.compare_exchange_strong(expected,true)){
		poller_.Run();
	}
	this->running.store(false);
}

}