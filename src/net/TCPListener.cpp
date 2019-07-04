#include "TCPListener.h"
#include "SocketHelper.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

namespace hwnet {

void TCPListener::OnActive(int event) {
	auto post = false;

	this->mtx.lock();
	
	if(event & Poller::ReadFlag()) {
		++this->readableVer;
	}

	if(!doing) {
		this->doing = true;
		post = true;
	}
	
	this->mtx.unlock();
	
	if(post) {
		poller_->PostTask(shared_from_this());
	}
}


int TCPListener::accept(int *fd_,struct sockaddr *addr_,socklen_t *len) {
	while((*fd_ = ::accept(this->fd,addr_,len)) < 0){
#ifdef EPROTO
		if(errno == EPROTO || errno == ECONNABORTED)
#else
		if(errno == ECONNABORTED)
#endif
			continue;
		else{
			return errno;
		}
	}
	return 0;	

}

void TCPListener::Do() {
	for(;;) {
		struct sockaddr addr_;
		socklen_t len = this->addr.AddrLen();
		int       fd_;
		this->mtx.lock();		
		auto localVer = this->readableVer;
		this->mtx.unlock();
		auto err = this->accept(&fd_,&addr_,&len);
		if(0 == err) {
			auto self = shared_from_this();
			this->onNewConn_(self,fd_,Addr::MakeBySockAddr(&addr_,len));
		} else {
			if(err == EAGAIN) {
				std::lock_guard<std::mutex> guard(this->mtx);
				if(localVer == this->readableVer) {
					this->doing = false;
					break;
				}
			} else {
				if(nullptr != this->onError_){
					auto self = shared_from_this();
					this->onError_(self,err);
					break;
				}
			}
		}
		if(this->stop.load() == true) {
			break;
		}
	}
}

void TCPListener::Stop() {
	if(!this->started.load()) {
		return;
	}
	bool expected = false;
	if(!this->stop.compare_exchange_strong(expected,true)) {
		return;
	}
	this->poller_->Remove(shared_from_this());
	::close(this->fd);
}

bool TCPListener::_start(const OnNewConn &onNewConn,const OnError &onError) {
	this->fd = ::socket(this->addr.Family(),SOCK_STREAM,IPPROTO_TCP);
	
	if(this->fd < 0){
		return false;
	}

	SetNoBlock(this->fd,true);

	ReuseAddr(this->fd);

	if(0 != bind(this->fd,addr.Address(),addr.AddrLen())) {
        ::close(this->fd);
        this->fd = -1;
        printf("listen bind error:%s\n",strerror(errno));
		return false;
	}
	
	if(0 != listen(this->fd,SOMAXCONN)) {
		::close(this->fd);
		this->fd = -1;
		printf("listen error:%s\n",strerror(errno));
		return false;
	}

	this->onNewConn_ = onNewConn;
	this->onError_   = onError;
	this->poller_->Add(shared_from_this(),Poller::addRead | Poller::addET);
	return false;
}

bool TCPListener::Start(const OnNewConn &onNewConn,const OnError &onError) {
	if(!onNewConn) {
		return false;
	}

	if(!this->addr.IsVaild()) {
		return false;
	}

	bool expected = false;
	if(!this->started.compare_exchange_strong(expected,true)) {
		return false;
	}

	if(this->_start(onNewConn,onError)){
		return true;
	} else {
		this->started.store(false);
		return false;
	}
}

}