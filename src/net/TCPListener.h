#ifndef _TCPLISTENER_H
#define _TCPLISTENER_H

#include <mutex>
#include <functional>
#include <atomic>
#include <thread>
#include "net/Address.h"
#include "net/Poller.h"


namespace hwnet {


class TCPListener : public Task, public Channel ,public std::enable_shared_from_this<TCPListener> {

public:
	typedef std::shared_ptr<TCPListener> Ptr;

	typedef std::function<void (const Ptr&,int,const Addr&)> OnNewConn;

	typedef std::function<void (const Ptr&,int)> OnError;

	TCPListener(Poller *poller_,const Addr &addr):fd(-1),poller_(poller_),addr(addr),started(false),stop(false),readableVer(0),doing(false){

	}

	static TCPListener::Ptr New(Poller *poller_,const Addr &addr) {
		if(!poller_) {
			return nullptr;
		} else { 
			return std::make_shared<TCPListener>(poller_,addr);
		}
	}

	bool Start(const OnNewConn &onNewConn,const OnError &onError = nullptr);

	void Stop();

	void OnActive(int event);

	void Do();

	int Fd() const {
		return fd;
	}


	~TCPListener(){}

private:

	bool _start(const OnNewConn &onNewConn,const OnError &onError = nullptr);

	int accept(int *fd,struct sockaddr *addr,socklen_t *len);

	TCPListener(const TCPListener&) = delete;
	TCPListener& operator = (const TCPListener&) = delete;

	int              fd;
	Poller           *poller_;
	Addr             addr;
	OnNewConn        onNewConn_;
	OnError          onError_;
	std::atomic_bool started;
	std::atomic_bool stop;
	std::mutex       mtx;
	int              readableVer;
	bool             doing;	
};

}

#endif
