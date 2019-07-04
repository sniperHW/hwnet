#ifndef _TCPCONNECTOR_H
#define _TCPCONNECTOR_H


#include <mutex>
#include <functional>
#include <atomic>
#include "net/Address.h"
#include "net/Poller.h"
#include "net/NonCopyable.h"

namespace hwnet {

class TCPConnector :public NonCopyable, public Task, public Channel ,public std::enable_shared_from_this<TCPConnector> {


public:
	typedef std::shared_ptr<TCPConnector> Ptr;
	typedef std::function<void (int)> ConnectCallback;
	typedef std::function<void (int,const Addr &remote)> ErrorCallback;

	static TCPConnector::Ptr New(Poller *poller_,const Addr &remote,const Addr &local = Addr()) {
		if(!poller_) {
			return nullptr;
		} else { 
			return TCPConnector::Ptr(new TCPConnector(poller_,remote,local));
		}
	}

	bool Connect(const ConnectCallback &connectFn,const ErrorCallback &errorFn = nullptr);

	void OnActive(int event);

	void Do();

	int Fd() const {
		return fd;
	}

	~TCPConnector();

private:

	TCPConnector(Poller *poller_,const Addr &remote,const Addr &local):
		remoteAddr(remote),localAddr(local),poller_(poller_) {

	}

	bool checkError(int &err,ErrorCallback &errcb);

	int  fd;

	Addr remoteAddr;
	Addr localAddr;
	ConnectCallback  connectCallback_;
	ErrorCallback    errorCallback_;
	std::atomic_bool started;
	Poller          *poller_;
	std::mutex       mtx;
};

}


#endif