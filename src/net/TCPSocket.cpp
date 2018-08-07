#include "TCPSocket.h"
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include "temp_failure_retry.h"

namespace hwnet {

TCPSocket::TCPSocket(Poller *poller_,int fd):fd(fd),err(0),writeCompleteCallback_(nullptr),
	errorCallback_(nullptr),closeCallback_(nullptr),readable(false),readableVer(0),writeable(false),
	writeableVer(0),closed(false),shutdown(false),doing(false),socketError(false),poller_(poller_),
	bytes4Send(0),highWaterSize(0) {
	int flags;
    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
}

void TCPSocket::Do() {
	auto doClose = false;	
	for( ; ; ) {
		this->mtx.lock();
		auto closed = this->closed.load();
		if(!(canRead() || canWrite()) || closed) {
			if(!closed) {
				this->doing =false;
			} else {
				doClose = true;
			}
			this->mtx.unlock();
			if(doClose){
				this->doClose();
			}
			return;
		}
		this->mtx.unlock();
		recvInWorker();
		sendInWorker();
	}
}

void TCPSocket::Recv(const Buffer::Ptr &buff,const RecvCallback& callback) {
	if(buff == nullptr || callback == nullptr) {
		return;
	}
	if(closed.load() == false) {
		auto post = false;
		this->mtx.lock();
		recvList.push_back(recvReq(buff,callback));
		if(!this->doing && this->readable) {
			this->doing = true;
			post = true;	
		}
		this->mtx.unlock();
		if(post) {			
			poller_->PostTask(shared_from_this());
		}
	}
}

void TCPSocket::Send(const char *str,size_t len) {
	this->Send(Buffer::New(str,len));
}

void TCPSocket::Send(const std::string &str) {
	this->Send(Buffer::New(str));
}

void TCPSocket::Send(const Buffer::Ptr &buff) {
	if(buff == nullptr) {
		return;
	}
	if(closed.load() == false && shutdown.load() == false) {
		auto post = false;
		this->mtx.lock();
		if(this->bufferProcessFunc_){
			sendList.push_back(buff);
		} else {
			localSendList.push_back(sendContext(buff));
		}
		this->bytes4Send += buff->Len();
		if(!this->doing && (this->writeable || this->highWater())) {
			this->doing = true;
			post = true;	
		}
		this->mtx.unlock();
		if(post) {
			poller_->PostTask(shared_from_this());
		}
	}
}

void TCPSocket::OnActive(int event) {
	auto post = false;
	this->mtx.lock();
	if(event & Poller::ReadFlag()) {
		++this->readableVer;
		this->readable = true;
	}

	if(event & Poller::WriteFlag()){
		++this->writeableVer;
		this->writeable = true;
	}

	if(event & Poller::ErrorFlag()) {
  		int optval;
  		socklen_t optlen = static_cast<socklen_t>(sizeof optval);
  		if (::getsockopt(this->fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
    		err = errno;
  		} else {
  			err = optval;
  		}
		this->socketError = true;
	}

	if((canRead() || canWrite()) && !doing) {
		this->doing = true;
		post = true;
	}
	this->mtx.unlock();

	if(post) {
		poller_->PostTask(shared_from_this());
	}
}


void TCPSocket::sendInWorker() {

	auto t = 0;

	WriteCompleteCallback *writeCompleteCallback = nullptr;
	ErrorCallback errorCallback = nullptr;
	HighWaterCallback highWaterCallback = nullptr;
	size_t bytes4Send = 0;

	this->mtx.lock();

	do{
		if(!this->canWrite())
			break;

		if(!this->writeable && this->highWater()) {
			t = 1;
			bytes4Send = this->bytes4Send;
			highWaterCallback = this->highWaterCallback_;
			break;
		}

		if(this->bufferProcessFunc_ && !this->sendList.empty()){
			auto it = sendList.begin();
			for( ; it != sendList.end(); ++it){
				auto oldLen = (*it)->Len();
				//这里可能会执行耗时的压缩加密工作，所以先释放锁
				this->mtx.unlock();
				(*it) = this->bufferProcessFunc_((*it));
				this->mtx.lock();
				//bufferProcessFunc可能会改变待发送数据的大小
				if(oldLen > (*it)->Len()){
					//数据被压缩了需要减小bytes4Send
					this->bytes4Send -= (oldLen - (*it)->Len());
				}else if(oldLen < (*it)->Len()){
					//数据变大了需要增加bytes4Send
					this->bytes4Send += ((*it)->Len() - oldLen);
				}
				localSendList.push_back(sendContext(*it));
			}
			sendList.clear();
		}

		auto it  = localSendList.begin();
		auto end = localSendList.end();
		auto c   = 0;
		for( ; it != end && c < max_send_iovec_size; ++it,++c) {
			send_iovec[c].iov_len = (*it).len;
			send_iovec[c].iov_base = (*it).ptr;
		}
		auto localVer = this->writeableVer;
		this->mtx.unlock();
		int n = TEMP_FAILURE_RETRY(::writev(this->fd,&this->send_iovec[0],c));
		this->mtx.lock();
		if(n >= 0) {
			for(auto i = 0; i < c && !localSendList.empty(); ++i) {
				auto front = localSendList.front();
				auto len = 0;
				if(n >= (int)this->send_iovec[i].iov_len) {
					len = this->send_iovec[i].iov_len;
				} else {
					len = n;
				}

				front.ptr += len;
				front.len -= len;
				this->bytes4Send -= len;
				if(front.len == 0) {
					localSendList.pop_front();
				} else {
					break;
				}
			}

			if(localSendList.empty()){
				if(this->shutdown.load()){
					::shutdown(this->fd,SHUT_WR);
				}
				if(this->writeCompleteCallback_) {
					writeCompleteCallback = &this->writeCompleteCallback_;
					t = 2;
				}
			}		
		} else {
			if(errno == EAGAIN) {
				if(this->writeableVer == localVer) {
					this->writeable = false;
				}
			} else {
				errorCallback = this->errorCallback_;
				t = 3;
			}
		}

	}while(0);

	this->mtx.unlock();

	switch(t){
		case 1:{
			if(highWaterCallback){
				highWaterCallback(shared_from_this(),bytes4Send);
			}			
		}
		break;
		case 2:{
			if(writeCompleteCallback){
				(*writeCompleteCallback)(shared_from_this());
			}			
		}
		break;
		case 3:{
			if(errorCallback) {
				errorCallback(shared_from_this(),err);
			} else {
				this->Close();
			}				
		}
		break;
		default:{
			return;
		}
		break;
	}
}

void TCPSocket::recvInWorker() {
	ErrorCallback errorCallback = nullptr;
	auto t = 0;
	std::list<recvReq> okList;
	this->mtx.lock();
	do{
		if(!this->canRead())
			break;	

		if(this->socketError){
			t = 1;
			errorCallback = this->errorCallback_;
			errno = this->err;
			break;
		}

		auto it  = recvList.begin();
		auto end = recvList.end();
		auto c   = 0;
		for( ; it != end && c < max_recv_iovec_size; ++it,++c) {
			(*it).buff->PrepareRecv(recv_iovec[c]);
		}
		auto localVer = this->readableVer;
		this->mtx.unlock();
		int n = TEMP_FAILURE_RETRY(::readv(this->fd,&this->recv_iovec[0],c));
		this->mtx.lock();		
		if(n > 0) {
			for(auto i = 0; i < c && n > 0 ; ++i) {
				auto front = recvList.front();
				auto len = 0;
				if(n > (int)this->recv_iovec[i].iov_len) {
					len = this->recv_iovec[i].iov_len;
				} else {
					len = n;
				}
				front.buff->RecvFinish(len);
				n -= len;
				okList.push_back(front);
				recvList.pop_front();
			}
			t = 2;
		} else {
			if(errno == EAGAIN) {
				if(localVer == this->readableVer) {
					this->readable = false;
				}
			} else {
				t = 1;
				errorCallback = this->errorCallback_;	
			}
		}
	}while(0);
	this->mtx.unlock();

	switch(t){
		case 1:{
			if(errorCallback) {
				errorCallback(shared_from_this(),err);
			} else {
				this->Close();
			}	
		}
		break;
		case 2:{
			for(auto it = okList.begin(); it != okList.end(); ++it) {
				if((*it).recvCallback_){
					(*it).recvCallback_(shared_from_this(),(*it).buff);
					if(closed.load() == true){
						//在回调中调用了Close,不再调用回调
						return;
					}
				}
			}			
		}
		break;
		default:{
			return;
		}
		break;
	}
}

void TCPSocket::Shutdown() {
	if(this->closed.load() == true) {
		return;
	}
	bool expected = false;
	if(!this->shutdown.compare_exchange_strong(expected,true)) {
		return;
	}

	this->mtx.lock();
	if(this->sendList.empty()){
		::shutdown(this->fd,SHUT_WR);
	}
	this->mtx.unlock();	
}

void TCPSocket::Close() {
	this->mtx.lock();
	bool expected = false;
	if(!this->closed.compare_exchange_strong(expected,true)) {
		this->mtx.unlock();
		return;
	}
	poller_->Remove(shared_from_this());	
	::close(fd);

	if(closeCallback_ && !this->doing){
		poller_->PostTask(shared_from_this());
	}
	this->mtx.unlock();
}

void TCPSocket::doClose() {
	CloseCallback callback = nullptr;
	{
		std::lock_guard<std::mutex> guard(this->mtx);
		if(closeCallback_){
			callback = closeCallback_;
		}
	}

	if(callback) {
		callback(shared_from_this());
	}
}

}