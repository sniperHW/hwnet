#ifndef _EPOLL_H
#define _EPOLL_H

#include <sys/epoll.h>
#include "net/Channel.h"
#include "net/ThreadPool.h"

namespace hwnet {

class Poller;

class Epoll {

public:
	explicit Epoll():epfd(-1),events(nullptr),maxevents(0) {

	}

	~Epoll();

	bool Init();

	static int  ReadFlag() {
		return EPOLLIN;
	}

	static int  WriteFlag() {
		return EPOLLOUT;
	}

	static int  ErrorFlag() {
		return EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	}

	void Add(const Channel::Ptr &channel,int flag);

	void Remove(const Channel::Ptr &channel);

	int  RunOnce();

private:

	Epoll(const Epoll&) = delete;
	Epoll& operator = (const Epoll&) = delete; 	

	int          epfd;
	epoll_event *events;
	int          maxevents;	
};

}

#endif