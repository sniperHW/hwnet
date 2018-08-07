#ifndef _POLLER_H
#define _POLLER_H

#include "ThreadPool.h"
#include "Channel.h"
#include <map>
#include <list>
#include <atomic>

#ifdef _LINUX
#include "linux/Epoll.h"
#elif _MACOS
#include "mac/Kqueue.h"
#else
#   error "un support os!" 
#endif

namespace hwnet {

class Poller {


#ifdef _LINUX		
	friend class Epoll;
#else
	friend class Kqueue;
#endif

public:
	Poller():running(false),inited(false),poller_(this),processing(false){}

	static int  ReadFlag() {
#ifdef _LINUX		
		return Epoll::ReadFlag();
#else
		return Kqueue::ReadFlag();
#endif
	}

	static int  WriteFlag() {
#ifdef _LINUX		
		return Epoll::WriteFlag();
#else
		return Kqueue::WriteFlag();		
#endif
	}

	static int  ErrorFlag() {
#ifdef _LINUX		
		return Epoll::ErrorFlag();
#else
		return Kqueue::ErrorFlag();		
#endif
	}

	bool Init(int threadCount);

	void Add(const Channel::Ptr &channel);

	void Remove(const Channel::Ptr &channel);

	void PostTask(const Task::Ptr &task);

	void Run();

	//void Stop();

	void processBegin(){
		this->processing = true;
	}

	void processFinish();

	bool verify(int fd) {
		bool ok = false;
		this->mtx.lock();
		ok = this->channels.find(fd) != this->channels.end();
		this->mtx.unlock();
		return ok;
	}	

private:

	Poller(const Poller&);
	Poller& operator = (const Poller&); 	

	ThreadPool   pool_;

	std::atomic_bool running;
	std::atomic_bool inited;

	std::mutex   mtx;
	std::map<int,Channel::Ptr> channels;




#ifdef _LINUX		
	Epoll        poller_;
#else
	Kqueue       poller_;	
#endif

	bool         processing;

	std::list<int> removeing;

};

}

#endif