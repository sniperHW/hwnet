#ifndef _TCPCONNECTOR_H
#define _TCPCONNECTOR_H


#include <mutex>
#include <functional>
#include <atomic>
#include "net/Address.h"
#include "net/Poller.h"
#include "util/Timer.h"

namespace hwnet {

class TCPConnector : public Task, public Channel ,public std::enable_shared_from_this<TCPConnector> {


public:
	typedef std::shared_ptr<TCPConnector> Ptr;
	typedef std::function<void (int)> ConnectCallback;
	typedef std::function<void (int,const Addr &remote)> ErrorCallback;

	TCPConnector(Poller *poller_,const Addr &remote,const Addr &local):
		remoteAddr(remote),localAddr(local),started(false),poller_(poller_),doing(false),gotError(false),err(0) {

	}	

	static TCPConnector::Ptr New(Poller *poller_,const Addr &remote,const Addr &local = Addr()) {
		if(!poller_) {
			return nullptr;
		} else { 
			return std::make_shared<TCPConnector>(poller_,remote,local);
		}
	}

	bool Connect(const ConnectCallback &connectFn,const ErrorCallback &errorFn = nullptr);

	bool ConnectWithTimeout(const ConnectCallback &connectFn,size_t timeout,const ErrorCallback &errorFn = nullptr);

	void OnActive(int event);

	void Do();

	int Fd() const {
		return fd;
	}

	~TCPConnector();

private:

	static void connectTimeout(const util::Timer::Ptr &t,TCPConnector::Ptr self);

	bool checkError();

	TCPConnector(const TCPConnector&) = delete;
	TCPConnector& operator = (const TCPConnector&) = delete;
	int  fd;

	Addr remoteAddr;
	Addr localAddr;
	ConnectCallback  		connectCallback_;
	ErrorCallback    		errorCallback_;
	std::atomic_bool 		started;
	Poller          		*poller_;
	std::mutex       		mtx;
	util::Timer::WeakPtr    connectTimer;	
	bool                    doing;
	bool                    gotError;
	int                     err;

};

}


#endif
