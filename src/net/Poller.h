#ifndef _POLLER_H
#define _POLLER_H

#include "net/ThreadPool.h"
#include "net/Channel.h"
#include <unordered_map>
#include <list>
#include <atomic>
#include <mutex>


#ifdef _LINUX
#include "linux/Epoll.h"
#define POLLER Epoll
#elif _MACOS
#include "mac/Kqueue.h"
#define POLLER Kqueue
#else
#   error "un support os!" 
#endif

namespace hwnet {

class Poller {

private:

	class notifyChannel : public Channel ,public std::enable_shared_from_this<notifyChannel> {

	public:

		typedef std::shared_ptr<notifyChannel> Ptr;

		notifyChannel() {

		}

		bool init(Poller *poller_);

		void notify();

		void OnActive(int event);
	
		int  Fd() const {
			return this->notifyfds[0];
		}


	private:
		int notifyfds[2];
	};

public:

	static const int addRead  = 1 << 1;
	static const int addWrite = 1 << 2;
	static const int addET    = 1 << 3;

	Poller():running(false),inited(false),closed(false){}

	~Poller() {
		if(this->poolCreateByNew){
			delete this->pool_;
		}
	}

	static int  ReadFlag() {
		return POLLER::ReadFlag();
	}

	static int  WriteFlag() {
		return POLLER::WriteFlag();
	}

	static int  ErrorFlag() {	
		return POLLER::ErrorFlag();
	}

	bool Init(ThreadPool *pool = nullptr);

	void Add(const Channel::Ptr &channel,int flag);

	void Remove(const Channel::Ptr &channel);

	void PostTask(const Task::Ptr &task,ThreadPool *tpool = nullptr);

	void Run();

	void Stop();

private:

	void processNotify();

	Poller(const Poller&) = delete;
	Poller& operator = (const Poller&) = delete; 

	bool poolCreateByNew;
	ThreadPool  *pool_;

	std::atomic_bool running;
	std::atomic_bool inited;

	std::mutex   mtx;
	std::unordered_map<int,Channel::Ptr> channels;

	POLLER       poller_;

	notifyChannel::Ptr notifyChannel_;

	std::list<Channel::Ptr> waitRemove;

	std::atomic_bool closed;

};

}

#endif
