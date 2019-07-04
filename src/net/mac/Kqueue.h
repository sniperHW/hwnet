#ifndef _KQUEUE_H
#define _KQUEUE_H

#include <sys/event.h>
#include "net/Channel.h"
#include "net/ThreadPool.h"

namespace hwnet {

class Poller;

class Kqueue {

	static const int readFlag  =  1 << 1;
	static const int writeFlag =  1 << 2;
	static const int errorFlag =  1 << 3;

public:
	explicit Kqueue():kfd(-1),events(nullptr),maxevents(0) {

	}

	~Kqueue();	

	static int  ReadFlag() {
		return readFlag;
	}

	static int  WriteFlag() {
		return writeFlag;
	}

	static int  ErrorFlag() {
		return errorFlag;
	}

	bool Init();

	void Add(const Channel::Ptr &channel,int flag);

	void Remove(const Channel::Ptr &channel);

	int  RunOnce();

private:

	Kqueue(const Kqueue&) = delete;
	Kqueue& operator = (const Kqueue&) = delete; 	

	int     kfd;
	struct  kevent *events;
	int     maxevents;
};

}


#endif