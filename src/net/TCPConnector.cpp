#include "TCPConnector.h"
#include "SocketHelper.h"

#include	<errno.h>
#include	<fcntl.h>		/* for nonblocking */
#include	<unistd.h>


namespace hwnet {

TCPConnector::~TCPConnector() {

}

void TCPConnector::connectTimeout(util::Timer::Ptr t,TCPConnector::Ptr self) {
	auto post = false;
	self->mtx.lock();
	if(!self->doing){
		self->doing = true;
		post = true;
		self->gotError = true;
		self->err = ETIMEDOUT;
	}
	self->mtx.unlock();
	if(post) {
		self->poller_->PostTask(self);
	}	
}

bool TCPConnector::ConnectWithTimeout(const ConnectCallback &connectFn,size_t timeout,const ErrorCallback &errorFn) {
	bool ret = Connect(connectFn,errorFn);
	if(ret && timeout > 0) {
		auto self = shared_from_this();
		this->connectTimer = poller_->addTimerOnce(timeout,TCPConnector::connectTimeout,self);
	}
	return ret;
}

bool TCPConnector::Connect(const ConnectCallback &connectFn,const ErrorCallback &errorFn) {

	bool expected = false;
	if(!this->started.compare_exchange_strong(expected,true)) {
		return false;
	}

	this->connectCallback_ = connectFn;
	this->errorCallback_ = errorFn;


	if(!this->connectCallback_) {
		return false;
	}

	if(!this->remoteAddr.IsVaild()) {
		return false;
	}

	this->fd = ::socket(this->remoteAddr.Family(),SOCK_STREAM,IPPROTO_TCP);
		
	if(this->fd < 0){
		return false;
	}

	SetNoBlock(this->fd,true);

	SetCloseOnExec(this->fd);

    int ret = ::connect(this->fd,this->remoteAddr.Address(),this->remoteAddr.AddrLen());

    if(ret == 0 || errno == EINPROGRESS) {
		std::lock_guard<std::mutex> guard(this->mtx);
    	this->poller_->Add(shared_from_this(),Poller::Write);
    	return true;
    } else {
    	::close(this->fd);
    	this->fd = -1;
    	return false;
    }
}


bool TCPConnector::checkError(int &err) {
	socklen_t len = sizeof(err);
	if(getsockopt(this->fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1){
		err = errno;
	    return true;
	}

	if(err) {
		return true;				
	}

	return false;
}

void TCPConnector::OnActive(int _) {
	(void)_;	

	this->gotError = this->checkError(this->err);

	this->poller_->Remove(shared_from_this());

	auto p = this->connectTimer.lock();
	if(p) {
		p->cancel();
	}
	
	auto post = false;
	this->mtx.lock();
	if(!this->doing) {
		post = true;
		this->doing = true;
	}
	this->mtx.unlock();

	if(post) {
		poller_->PostTask(shared_from_this());
	}
}

void TCPConnector::Do() {
	int fd_ = -1;
	ConnectCallback connectCallback = nullptr;
	ErrorCallback   errorCallback = nullptr;

	{
		std::lock_guard<std::mutex> guard(this->mtx);
		if(this->gotError) {
			errorCallback = this->errorCallback_;
			::close(this->fd);
		} else {	
			connectCallback = this->connectCallback_;
			fd_ = this->fd;
			this->fd = -1;
		}
	}

	if(nullptr != errorCallback) {
		errorCallback(this->err,this->remoteAddr);
	} else if(nullptr != connectCallback) {
		connectCallback(fd_);
	}
}

}