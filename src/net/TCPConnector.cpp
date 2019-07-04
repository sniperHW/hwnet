#include "TCPConnector.h"
#include "SocketHelper.h"

#include	<errno.h>
#include	<fcntl.h>		/* for nonblocking */
#include	<unistd.h>


namespace hwnet {

TCPConnector::~TCPConnector() {

}

bool TCPConnector::Connect(const ConnectCallback &connectFn,const ErrorCallback &errorFn) {

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
    	this->poller_->Add(shared_from_this(),/*Poller::addRead |*/ Poller::addWrite);
    	return true;
    } else {
    	printf("error 4 %s\n",strerror(errno));
    	::close(this->fd);
    	this->fd = -1;
    	return false;
    }
}


bool TCPConnector::checkError(int &err,ErrorCallback &errcb) {
	std::lock_guard<std::mutex> guard(this->mtx);	
	socklen_t len = sizeof(err);
	if(getsockopt(this->fd, SOL_SOCKET, SO_ERROR, &err, &len) == -1){
		err = errno;
	    errcb = this->errorCallback_;
	    return true;
	}

	if(err) {
		errcb = this->errorCallback_;
		return true;				
	}

	return false;
}

void TCPConnector::OnActive(int _) {
	(void)_;	
	int  err = 0;
	ErrorCallback errorCallback;

	auto gotError = this->checkError(err,errorCallback);

	this->poller_->Remove(shared_from_this());
	
	if(gotError) {
		if(nullptr != errorCallback) {
			printf("errorCallback_1 %d %p\n",this->fd,this);
			errorCallback(err,this->remoteAddr);
		}
		::close(this->fd);		
	} else {
		poller_->PostTask(shared_from_this());		
	}
}

void TCPConnector::Do() {
	int fd_ = -1;
	ConnectCallback connectCallback = nullptr;
	{
		std::lock_guard<std::mutex> guard(this->mtx);
		connectCallback = this->connectCallback_;
		fd_ = this->fd;
		this->fd = -1;
	}

	if(nullptr != connectCallback) {
		connectCallback(fd_);
	}
}

}