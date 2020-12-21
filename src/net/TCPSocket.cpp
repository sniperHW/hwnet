#include "TCPSocket.h"
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include "temp_failure_retry.h"
#include <stdio.h>
#include <iostream>
#include <stdio.h>

namespace hwnet {


int TCPSocket::timoutResolution = 1000;

#ifdef USE_SSL

static int inline ssl_again(int ssl_error) {
	if(ssl_error == SSL_ERROR_WANT_WRITE || ssl_error == SSL_ERROR_WANT_READ) {
		return 1;
	}
	else {
		return 0;
	}
}

#endif


class sendContextPool {

public:

	static const int poolCount = 257;
	static const int itemCount = 10000; 

	class pool {
	
	public:
		pool():count(0),head(nullptr){}

		~pool(){
			while(nullptr != this->head){
				auto c = this->head;
				this->head = c->next;
				free((void*)c);
			}
		}
		std::mutex mtx;
		int count;
		TCPSocket::sendContext *head;
	};

	TCPSocket::sendContext* get(int fd,const Buffer::Ptr &buff,size_t len) {
		auto index = fd%poolCount;
		pool &p = this->pools[index]; 
		std::lock_guard<std::mutex> guard(p.mtx);
		if(nullptr == p.head) {
			auto m = malloc(sizeof(TCPSocket::sendContext));
			auto s = new(m)TCPSocket::sendContext(buff,len);
			s->poolIndex = index;
			return s;
		} else {
			--p.count;
			auto s = p.head;
			p.head = p.head->next;
			s = new(s)TCPSocket::sendContext(buff,len);
			s->poolIndex = index;
			return s;
		}
	}	

	void put(TCPSocket::sendContext *s) {
		auto index = s->poolIndex;
		pool &p = this->pools[index]; 
		std::lock_guard<std::mutex> guard(p.mtx);
		s->~sendContext();		
		if(p.count < itemCount){
			p.count++;
			if(nullptr != p.head){
				s->next = p.head->next;
			}
			p.head = s;
		} else {
			free((void*)s);
		}
	}

	pool pools[poolCount];

};

static sendContextPool sContextPool;


TCPSocket::sendContext* TCPSocket::getSendContext(int fd,const Buffer::Ptr &buff,size_t len) {
	return sContextPool.get(fd,buff,len);
}

void TCPSocket::putSendContext(TCPSocket::sendContext *s) {
	sContextPool.put(s);	
}


TCPSocket::TCPSocket(Poller *poller_,int fd):fd(fd),err(0),flushCallback_(nullptr),
	errorCallback_(nullptr),closeCallback_(nullptr),readable(false),readableVer(0),writeable(false),
	writeableVer(0),closed(false),closedOnFlush(false),shutdown(false),doing(false),socketError(false),poller_(poller_),
	bytes4Send(0),highWaterSize(0),pool_(nullptr),started(false),
	recvTimeout(0),recvTimeoutCallback_(nullptr),lastRecvTime(std::chrono::steady_clock::now()),
	sendTimeout(0),sendTimeoutCallback_(nullptr),lastSendTime(std::chrono::steady_clock::now()) {
		slistIdx = 0;
		ptrSendlist = &sendLists[slistIdx];

#ifdef GATHER_RECV
		rlistIdx = 0;
		ptrRecvlist = &recvLists[rlistIdx];		
#endif

#ifdef USE_SSL
		this->ssl = nullptr;
#endif

	}


TCPSocket::TCPSocket(Poller *poller_,ThreadPool *pool_,int fd):fd(fd),err(0),flushCallback_(nullptr),
	errorCallback_(nullptr),closeCallback_(nullptr),readable(false),readableVer(0),writeable(false),
	writeableVer(0),closed(false),closedOnFlush(false),shutdown(false),doing(false),socketError(false),poller_(poller_),
	bytes4Send(0),highWaterSize(0),pool_(pool_),started(false),
	recvTimeout(0),recvTimeoutCallback_(nullptr),lastRecvTime(std::chrono::steady_clock::now()),
	sendTimeout(0),sendTimeoutCallback_(nullptr),lastSendTime(std::chrono::steady_clock::now()) {
		slistIdx = 0;
		ptrSendlist = &sendLists[slistIdx];		
#ifdef GATHER_RECV
		rlistIdx = 0;
		ptrRecvlist = &recvLists[rlistIdx];		
#endif	

#ifdef USE_SSL
		this->ssl = nullptr;
#endif

	}


TCPSocket::~TCPSocket() {
	if(auto sp = this->timer.lock()) {
		sp->cancel();
	}
	::close(this->fd);

#ifdef USE_SSL
	if(this->ssl) {
		SSL_free(this->ssl);
	}
#endif

}

enum {
	timerSend = 1,
	timerRecv = 2,
};

void TCPSocket::registerTimer(int tt) {
	if(tt == timerSend) {
		//send
		if(!this->ptrSendlist->empty() && !this->timer.lock()) {
			auto sp = shared_from_this();
			this->lastSendTime = std::chrono::steady_clock::now();
			this->timer = this->poller_->addTimer(timoutResolution,TCPSocket::onTimer,sp);
		}

	} else {
		//recv
		if(!this->recvListEmpty() && !this->timer.lock()) {	
			auto sp = shared_from_this();
			this->lastRecvTime = std::chrono::steady_clock::now();
			this->timer = this->poller_->addTimer(timoutResolution,TCPSocket::onTimer,sp);
		}
	}
}

void TCPSocket::checkTimeout(hwnet::util::Timer::Ptr t) {

	if(t != this->timer.lock()) {
		return;
	}
		
	if(!this->closed && this->sendTimeout > 0 && 
	   this->sendTimeoutCallback_ && !this->ptrSendlist->empty() &&
	   (util::milliseconds)((std::chrono::steady_clock::now() - this->lastSendTime).count()) > this->sendTimeout*1000000) {
		SendTimeoutCallback cb = this->sendTimeoutCallback_;

		this->sendTimeout = 0;
		this->sendTimeoutCallback_ = nullptr;

		this->mtx.unlock();
		cb(shared_from_this());
		this->mtx.lock();
	}

	if(!this->closed && this->recvTimeout > 0 && 
	   this->recvTimeoutCallback_ && !this->recvListEmpty() &&
	   (util::milliseconds)((std::chrono::steady_clock::now() - this->lastRecvTime).count()) > this->recvTimeout*1000000) {
		RecvTimeoutCallback cb = this->recvTimeoutCallback_;
		
		this->recvTimeout = 0;
		this->recvTimeoutCallback_ = nullptr;

		this->mtx.unlock();
		cb(shared_from_this());
		this->mtx.lock();
	}

	if(this->sendTimeout == 0 && this->recvTimeout == 0) {
		t->cancel();
		this->timer.reset();
	}

}


void TCPSocket::onTimer(const hwnet::util::Timer::Ptr &t,TCPSocket::Ptr s) {
	auto post = false;

	s->mtx.lock();
	
	if(s->sendTimeout > 0 && 
	   s->sendTimeoutCallback_ && !s->ptrSendlist->empty() &&
	   (util::milliseconds)((std::chrono::steady_clock::now() - s->lastSendTime).count()) > s->sendTimeout*1000000) {
		s->closures.push_back(std::bind(&TCPSocket::checkTimeout, s,t));
		if(!s->doing){
			s->doing = true;
			post = true;
		}
	}

	if(s->recvTimeout > 0 && 
	   s->recvTimeoutCallback_ && !s->recvListEmpty() &&
	   (util::milliseconds)((std::chrono::steady_clock::now() - s->lastRecvTime).count()) > s->recvTimeout*1000000) {
		s->closures.push_back(std::bind(&TCPSocket::checkTimeout, s,t));
		if(!s->doing){
			s->doing = true;
			post = true;
		}
	}


	s->mtx.unlock();

	if(post) {
		s->poller_->PostTask(s,s->pool_);
	}

}


TCPSocket::Ptr TCPSocket::SetSendTimeoutCallback(util::milliseconds timeout, const SendTimeoutCallback &callback) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed) {	
		this->sendTimeout = 0;
		this->sendTimeoutCallback_ = nullptr;

		if(timeout > 0 && callback) {
			this->sendTimeout = timeout;
			this->sendTimeoutCallback_ = callback;			
			this->registerTimer(timerSend);
		} else if(this->recvTimeout == 0) {
			if(auto sp = this->timer.lock()) {
				sp->cancel();
				this->timer.reset();
			}		
		}
	}

	return shared_from_this();
}

TCPSocket::Ptr TCPSocket::SetRecvTimeoutCallback(util::milliseconds timeout, const RecvTimeoutCallback &callback) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed) {
		this->recvTimeout = 0;
		this->recvTimeoutCallback_ = nullptr;

		if(timeout > 0 && callback) {
			this->recvTimeout = timeout;
			this->recvTimeoutCallback_ = callback;	
			this->registerTimer(timerRecv);
		} else if(this->sendTimeout == 0) {
			if(auto sp = this->timer.lock()) {
				sp->cancel();
				this->timer.reset();			
			}		
		}
	}

	return shared_from_this();

}

void TCPSocket::Do() {
	static const int maxLoop = 10;
	auto doClose = false;	
	//this->tid = std::this_thread::get_id();
	auto i = 0;
	for( ;;i++) {
		this->mtx.lock();
		if(!this->closures.empty()) {
			auto front = this->closures.front();
			this->closures.pop_front();
			front();
		}

		auto canRead_ = canRead();
		auto canWrite_ = canWrite();
		if(!(canRead_ || canWrite_) || this->closed) {
			if(!closed) {
				this->doing =false;
			} else {
				doClose = true;
			}
			this->mtx.unlock();
			if(doClose){
				this->doClose();
			}
			//this->tid = std::thread::id();
			return;
		}
		this->mtx.unlock();
		if(i >= maxLoop){
			//执行次数过多了，暂停一下，将cpu让给其它任务执行
			//this->tid = std::thread::id();
			poller_->PostTask(shared_from_this(),this->pool_);
			return;
		} else {


			if(canRead_){
				recvInWorker();
			}

#ifdef USE_SSL
			if(canWrite_){
				sendInWorkerSSL();
			}
#else
			if(canWrite_){
				sendInWorker();
			}
#endif

		}
	}
}

void TCPSocket::_recv(const Buffer::Ptr &buff,bool &post) {
	if(this->closed) {
		return;
	}

	if(this->recvCallback_ == nullptr) {
		return;
	}
#ifdef GATHER_RECV
	ptrRecvlist->push_back(buff);
#else
	recvList.push_back(buff);
#endif

	if(this->recvTimeout > 0) {
		this->registerTimer(timerRecv);
	}

	if(!this->doing && this->readable) {
		this->doing = true;
		post = true;
	}	
}

void TCPSocket::Recv(const Buffer::Ptr &buff) {
	if(buff == nullptr) {
		return;
	}

	if(buff->Len() == 0) {
		return;
	}

	auto post = false;
	this->mtx.lock();
	_recv(buff,post);
	this->mtx.unlock();

	if(post) {			
		poller_->PostTask(shared_from_this(),this->pool_);
	}
}

void TCPSocket::_send(const Buffer::Ptr &buff,size_t len,bool closedOnFlush_,bool &post) {
	if(this->closed || this->shutdown || this->closedOnFlush) {
		return;
	}

	this->closedOnFlush = closedOnFlush_;

	this->ptrSendlist->push_back(getSendContext(this->fd,buff,len));
	this->bytes4Send += len;

	if(this->sendTimeout > 0) {
		this->registerTimer(timerSend);
	}

	if(!this->doing && (this->writeable || this->highWater())) {
		this->doing = true;
		post = true;
	}	
}

void TCPSocket::SendAndClose(const Buffer::Ptr &buff,size_t len) {
	if(buff == nullptr) {
		return;
	}

	if(len == 0) {
		len = buff->Len();
	}

	if(len == 0) {
		return;
	}

	auto post = false;
	

	this->mtx.lock();
	_send(buff,len,true,post);
	this->mtx.unlock();		
	
	
	if(post) {
		poller_->PostTask(shared_from_this(),this->pool_);
	}
}

void TCPSocket::SendAndClose(const char *str,size_t len) {
	this->SendAndClose(Buffer::New(str,len),0);
}

void TCPSocket::SendAndClose(const std::string &str) {
	this->SendAndClose(Buffer::New(str),0);
}

void TCPSocket::Send(const char *str,size_t len) {
	this->Send(Buffer::New(str,len),0);
}

void TCPSocket::Send(const std::string &str) {
	this->Send(Buffer::New(str),0);
}


void TCPSocket::Send(const Buffer::Ptr &buff,size_t len) {
	if(buff == nullptr) {
		return;
	}

	if(len == 0) {
		len = buff->Len();
	}

	if(len == 0) {
		return;
	}

	auto post = false;
	
	this->mtx.lock();
	_send(buff,len,false,post);
	this->mtx.unlock();		
	
	
	if(post) {
		poller_->PostTask(shared_from_this(),this->pool_);
	}	

}

void TCPSocket::OnActive(int event) {
	auto post = false;

	if(event & Poller::WriteFlag()){
		this->poller_->Disable(shared_from_this(),Poller::Write);
	}

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
		poller_->PostTask(shared_from_this(),this->pool_);
	}
}


#ifdef USE_SSL

static int SSLwrite(SSL *ssl,const char *ptr,size_t len) {
	errno = 0;
	int bytes_transfer = TEMP_FAILURE_RETRY(SSL_write(ssl,(void*)ptr,len));
	int ssl_error = SSL_get_error(ssl,bytes_transfer);
	if(bytes_transfer <= 0 && ssl_again(ssl_error)){
		errno = EAGAIN;
		return -1;
	}		
	return bytes_transfer;
}

static int SSLread(SSL *ssl,const char *ptr,size_t len) {
	errno = 0;
	int bytes_transfer = TEMP_FAILURE_RETRY(SSL_read(ssl,(void*)ptr,len));
	int ssl_error = SSL_get_error(ssl,bytes_transfer);
	if(bytes_transfer <= 0 && ssl_again(ssl_error)){
		errno = EAGAIN;
		return -1;
	}		
	return bytes_transfer;
}

void TCPSocket::sendInWorkerSSL() {
	if(this->ssl) {

		auto t = 0;

		FlushCallback flushCallback = nullptr;
		ErrorCallback errorCallback = nullptr;
		HighWaterCallback highWaterCallback = nullptr;
		size_t bytes4Send_ = 0;
		auto closedOnFlush_ = false;
		auto enableWrite = false;


		this->mtx.lock();

		
		if(!this->canWrite()){
			this->mtx.unlock();
			return; 
		} else {
			if(!this->writeable && this->highWater()) {
				t = 1;
				bytes4Send_ = this->bytes4Send;
				highWaterCallback = this->highWaterCallback_;
			} else {
				auto localSendlist = this->ptrSendlist;
				auto localVer = this->writeableVer;
				this->slistIdx = this->slistIdx == 0 ? 1 : 0;
				this->ptrSendlist = &this->sendLists[this->slistIdx]; 

				this->mtx.unlock();
				
				auto cur =  localSendlist->front();
				auto totalBytes = cur->len;

				int n = SSLwrite(this->ssl,cur->ptr,cur->len);

				this->mtx.lock();

				closedOnFlush_ = this->closedOnFlush;

				if(n >= 0) {

					if(this->sendTimeout > 0) {
						this->lastSendTime = std::chrono::steady_clock::now();
					}

					if((size_t)n >= totalBytes) {
						localSendlist->pop_front();
					} else {
						cur->ptr += n;
						cur->ptr -= n;
					}

					this->ptrSendlist->add_front(localSendlist);
					this->bytes4Send -= n;
					if(this->ptrSendlist->empty()){
						if(this->shutdown){
							::shutdown(this->fd,SHUT_WR);
						}
						if(this->flushCallback_) {
							flushCallback = this->flushCallback_;
							t = 2;
						}
					}

					if((size_t)n < totalBytes && this->writeableVer == localVer) {
						this->writeable = false;
						enableWrite = true;
						//this->poller_->Enable(shared_from_this(),Poller::Write);
					}

				} else {
					if(n < 0 && errno == EAGAIN) {
						this->ptrSendlist->add_front(localSendlist);
						if(this->writeableVer == localVer) {
							this->writeable = false;
							enableWrite = true;
							//this->poller_->Enable(shared_from_this(),Poller::Write);
						}
					} else {
						if(!this->closed){
							errorCallback = this->errorCallback_;
							this->socketError = true;
							this->err = errno;
							t = 3;
						}
					}
				}
			}
		}


		this->mtx.unlock();

		if(enableWrite) {
			this->poller_->Enable(shared_from_this(),Poller::Write);
		}		

		switch(t){
			case 1:{
				if(highWaterCallback){
					highWaterCallback(shared_from_this(),bytes4Send_);
				}			
			}
			break;
			case 2:{
				if(flushCallback){
					flushCallback(shared_from_this());
				}
				if(closedOnFlush_){
					this->Close();
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
				if(closedOnFlush_) {
					this->Close();
				}
				return;
			}
		}		



	} else {
		this->sendInWorker();
	}
}

#endif

void TCPSocket::sendInWorker() {
	
	auto t = 0;

	FlushCallback flushCallback = nullptr;
	ErrorCallback errorCallback = nullptr;
	HighWaterCallback highWaterCallback = nullptr;
	size_t bytes4Send_ = 0;
	auto closedOnFlush_ = false;
	auto enableWrite = false;


	this->mtx.lock();

	
	if(!this->canWrite()){
		this->mtx.unlock();
		return; 
	} else {
		if(!this->writeable && this->highWater()) {
			t = 1;
			bytes4Send_ = this->bytes4Send;
			highWaterCallback = this->highWaterCallback_;
		} else {
			auto localSendlist = this->ptrSendlist;
			auto localVer = this->writeableVer;
			this->slistIdx = this->slistIdx == 0 ? 1 : 0;
			this->ptrSendlist = &this->sendLists[this->slistIdx]; 

			this->mtx.unlock();

			auto c   = 0;
			auto totalBytes = 0;
			auto cur =  localSendlist->head;
			for( ; cur != nullptr && c < max_send_iovec_size; cur = cur->next,++c) {
				send_iovec[c].iov_len = cur->len;
				send_iovec[c].iov_base = cur->ptr;
				totalBytes += cur->len;			
			}

			int n = TEMP_FAILURE_RETRY(::writev(this->fd,&this->send_iovec[0],c));

			if(n >= totalBytes && cur == nullptr){
				localSendlist->clear();
			}

			this->mtx.lock();

			closedOnFlush_ = this->closedOnFlush;

			if(n >= 0) {

				if(this->sendTimeout > 0) {
					this->lastSendTime = std::chrono::steady_clock::now();
				}

				if(n > 0 && (n < totalBytes || cur != nullptr)) {
					//部分发送
					size_t nn = (size_t)n;
					for( ; ; ) {
						auto front = localSendlist->front();
						if(nn >= front->len) {
							nn -= front->len;
							localSendlist->pop_front();
						} else {
							front->ptr += nn;
							front->len -= nn;
							break;
						}
					}
					this->ptrSendlist->add_front(localSendlist);
				}

				this->bytes4Send -= n;
				if(this->ptrSendlist->empty()){
					if(this->shutdown){
						::shutdown(this->fd,SHUT_WR);
					}
					if(this->flushCallback_) {
						flushCallback = this->flushCallback_;
						t = 2;
					}
				}

				if(n < totalBytes && this->writeableVer == localVer) {
					this->writeable = false;
					enableWrite = true;
					//this->poller_->Enable(shared_from_this(),Poller::Write);
					//printf("un writeable 1 fd:%d\n",this->fd);
				}

			} else {
				if(n < 0 && errno == EAGAIN) {
					this->ptrSendlist->add_front(localSendlist);
					if(this->writeableVer == localVer) {
						this->writeable = false;
						enableWrite = true;
						//this->poller_->Enable(shared_from_this(),Poller::Write);
						//printf("un writeable 2 fd:%d\n",this->fd);					
					}
				} else {
					if(!this->closed){
						errorCallback = this->errorCallback_;
						this->socketError = true;
						this->err = errno;
						t = 3;
					}
				}
			}
		}
	}


	this->mtx.unlock();

	if(enableWrite) {
		this->poller_->Enable(shared_from_this(),Poller::Write);
	}

	switch(t){
		case 1:{
			if(highWaterCallback){
				highWaterCallback(shared_from_this(),bytes4Send_);
			}			
		}
		break;
		case 2:{
			if(flushCallback){
				flushCallback(shared_from_this());
			}
			if(closedOnFlush_){
				this->Close();
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
			if(closedOnFlush_) {
				this->Close();
			}
			return;
		}
	}
}


#ifdef GATHER_RECV
void TCPSocket::recvInWorker() {

	ErrorCallback errorCallback = nullptr;
	RecvCallback  recvCallback  = nullptr;
	auto getError 				= false;
	auto self 					= shared_from_this();
	this->mtx.lock();
	recvCallback = this->recvCallback_;
	
	if(!this->canRead()){
		this->mtx.unlock();
	} else {
		if(this->socketError){
			errorCallback = this->errorCallback_;
			this->mtx.unlock();
			getError = true;
		} else {	
			auto localRecvlist = this->ptrRecvlist;
		
			auto localVer = this->readableVer;

			this->rlistIdx = this->rlistIdx == 0 ? 1 : 0;
			this->ptrRecvlist = &this->recvLists[this->rlistIdx]; 

			this->mtx.unlock();		


			auto c   = 0;
			auto totalBytes = 0;
			auto it = localRecvlist->begin();
			auto end = localRecvlist->end();
			for(; it != end; it++) {
				recv_iovec[c].iov_len = (*it)->Len();
				recv_iovec[c].iov_base = (*it)->BuffPtr();
				totalBytes += (*it)->Len();
				c++;		
			}


			int n = TEMP_FAILURE_RETRY(::readv(this->fd,&this->recv_iovec[0],c));

			if(n > 0) {
					size_t nn = (size_t)n;
					for( ; nn > 0 ; ) {
						auto front = localRecvlist->front();
						auto s = front->Len();
						s = nn >= s ? s : nn;
						nn -= s;
						localRecvlist->pop_front();
						if(recvCallback){
							recvCallback(self,front,s);	
							if(this->closed.load()) {
								return;
							}				
						}					
					}

					this->mtx.lock();

					if(this->recvTimeout > 0) {
						this->lastRecvTime = std::chrono::steady_clock::now();
					}

					for( ; !localRecvlist->empty(); ) {
						auto back = localRecvlist->back();
						this->ptrRecvlist->push_front(back);
						localRecvlist->pop_back();
					}

					if(n != totalBytes) {
						if(localVer == this->readableVer) {
							this->readable = false;
						}					
					}

					this->mtx.unlock();

			} else {
				this->mtx.lock();
				if(n < 0 && errno == EAGAIN) {
					for( ; !localRecvlist->empty(); ) {
						auto back = localRecvlist->back();
						this->ptrRecvlist->push_front(back);
						localRecvlist->pop_back();
					}										
					if(localVer == this->readableVer) {
						this->readable = false;
					}
				} else {
					getError = true;
					this->err = errno;
					this->socketError = true;
					errorCallback = this->errorCallback_;	
				}
				this->mtx.unlock();			
			}
		}


		if(getError) {
			if(errorCallback) {
				errorCallback(self,err);
			} else {
				this->Close();
			}		
		}

	}
}

#else

void TCPSocket::recvInWorker() {

	ErrorCallback errorCallback = nullptr;
	RecvCallback  recvCallback  = nullptr;
	auto getError 				= false;
	auto self 					= shared_from_this();
	this->mtx.lock();
	recvCallback = this->recvCallback_;
	
	if(!this->canRead()){
		this->mtx.unlock();
	} else {
		if(this->socketError) {
			errorCallback = this->errorCallback_;
			this->mtx.unlock();
			getError = true;
		} else {		
			auto front = recvList.front();
			recvList.pop_front();
			auto localVer = this->readableVer;
			this->mtx.unlock();

#ifdef USE_SSL
			int n = 0;
			if(this->ssl) {
				n = SSLread(this->ssl,front->BuffPtr(),front->Len());
			} else {
				n = TEMP_FAILURE_RETRY(::read(this->fd,front->BuffPtr(),front->Len()));
			}
#else
			int n = TEMP_FAILURE_RETRY(::read(this->fd,front->BuffPtr(),front->Len()));

#endif

			if(n > 0) {

				if(recvCallback){
					recvCallback(self,front,n);			
				}

				this->mtx.lock();
				if(this->recvTimeout > 0) {
					this->lastRecvTime = std::chrono::steady_clock::now();
				}

				if(size_t(n) < front->Len()) {
					if(localVer == this->readableVer) {
						this->readable = false;
					}
				}
				this->mtx.unlock();

			} else {
				this->mtx.lock();
				if(n < 0 && errno == EAGAIN) {					
					recvList.push_front(front);
					if(localVer == this->readableVer) {
						this->readable = false;
					}
				} else {
					getError = true;
					this->err = errno;
					this->socketError = true;
					errorCallback = this->errorCallback_;	
				}
				this->mtx.unlock();			
			}
		}

		if(getError) {
			if(errorCallback) {
				errorCallback(self,err);
			} else {
				this->Close();
			}		
		}
	}
}

#endif

void TCPSocket::Shutdown() {

	std::lock_guard<std::mutex> guard(this->mtx);
	if(this->closed || this->shutdown) {
		return;
	}

	this->shutdown = true;

	if(this->ptrSendlist->empty()){
		::shutdown(this->fd,SHUT_WR);
	}
}

void TCPSocket::Close() {
	
	auto post = false;
	{	
		std::lock_guard<std::mutex> guard(this->mtx);
		if(this->closed) {
			return;
		}

		if(auto sp = this->timer.lock()) {
			sp->cancel();
			this->timer.reset();
		}

		this->recvTimeout = 0;
		this->recvTimeoutCallback_ = nullptr;

		this->sendTimeout = 0;
		this->sendTimeoutCallback_ = nullptr;


		this->closed.store(true);
		::shutdown(this->fd,SHUT_RDWR);
		poller_->Remove(shared_from_this());	

		if(this->closeCallback_ && !this->doing){
			post = true;
		}
	}
	if(post) {
		poller_->PostTask(shared_from_this(),this->pool_);
	}	
}

void TCPSocket::doClose() {
	CloseCallback callback = nullptr;
	{
		std::lock_guard<std::mutex> guard(this->mtx);
		if(closeCallback_){
			callback = closeCallback_;
		}
		sendLists[0].clear();
		sendLists[1].clear();
	}

	if(callback) {
		callback(shared_from_this());
	}
}


const Addr& TCPSocket::RemoteAddr() const {
	std::lock_guard<std::mutex> guard(this->mtx);	
	if(!this->remoteAddr.IsVaild()) {
		socklen_t len = sizeof(this->remoteAddr);
		if(0 == ::getpeername(this->fd,this->remoteAddr.Address(),&len)) {
			this->remoteAddr = Addr::MakeBySockAddr(this->remoteAddr.Address(),len);
		}		
	}
	return this->remoteAddr;
}

const Addr& TCPSocket::LocalAddr() const {
	std::lock_guard<std::mutex> guard(this->mtx);	
	if(!this->localAddr.IsVaild()) {
		socklen_t len = sizeof(this->localAddr);
		if(0 == ::getsockname(this->fd,this->localAddr.Address(),&len)) {
			this->localAddr = Addr::MakeBySockAddr(this->localAddr.Address(),len);
		}		
	}
	return this->localAddr;	
}

}
