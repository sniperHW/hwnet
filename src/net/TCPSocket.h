#ifndef _TCPSOCKET_H
#define _TCPSOCKET_H

#include <list>
#include <mutex>
#include <functional>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include "net/Buffer.h"
#include "net/Poller.h"
#include "net/Address.h"
#include "net/SocketHelper.h"
#include "net/any.h"
#include "util/Timer.h"

#ifdef USE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#undef GATHER_RECV
#endif


namespace hwnet {

class TCPSocket : public Task, public Channel ,public std::enable_shared_from_this<TCPSocket> {

	friend class sendContextPool;

public:

	typedef std::shared_ptr<TCPSocket> Ptr;

	static const bool ClosedOnFlush = true;

	static int timoutResolution; //可按需求调整,更小的分辨率将带来更大的消耗(默认1000毫秒)

public:

	/*
	*  接收回调，作为Recv的其中一个参数被传入
	*/
	typedef std::function<void (const TCPSocket::Ptr&,const Buffer::Ptr &buff,size_t n)> RecvCallback;
	
	/*
	*  当sendList全部冲刷完之后回调
	*/
	typedef std::function<void (const TCPSocket::Ptr&)> FlushCallback;

	/*
	*  错误回调，当连接出现错误时回调，可根据错误码做适当处理，如果没有设置，当错误出现时将直接调用Close 
	*/
	typedef std::function<void (const TCPSocket::Ptr&,int)> ErrorCallback;
	
	/*
	*  连接关闭回调，连接被销毁前回调，确保只调用一次 
	*/
	typedef std::function<void (const TCPSocket::Ptr&)> CloseCallback;

	/*
	*  发送阻塞回调，如果设置了highwatersize和callback,当连接不可写，且待发送数据长度超过一定值时被回调
	*/
	typedef std::function<void (const TCPSocket::Ptr&,size_t)> HighWaterCallback;

	typedef std::function<void (const TCPSocket::Ptr&)> RecvTimeoutCallback;

	typedef std::function<void (const TCPSocket::Ptr&)> SendTimeoutCallback;

private:

	class sendContext {
	public:

		sendContext():ptr(nullptr),len(0),next(nullptr){

		}

		sendContext(const Buffer::Ptr &buff,size_t len = 0):buff(buff),ptr(buff->BuffPtr()),next(nullptr){
			if(len > 0) {
				this->len = len;
			} else {
				this->len = buff->Len();
			}
		}

		~sendContext(){
			ptr  = nullptr;
			len  = 0;
			next = nullptr;
		}

		sendContext(const sendContext&) = delete;
		sendContext(sendContext&&) = delete;
		sendContext& operator = (const sendContext&) = delete;


		Buffer::Ptr  buff;
		char        *ptr;
		size_t       len;
		sendContext  *next;
		int          poolIndex;
	};

	static sendContext* getSendContext(int fd,const Buffer::Ptr &buff,size_t len = 0);
	static void putSendContext(sendContext*);

	class linklist {

	public:
		sendContext *head;
		sendContext *tail;

		linklist():head(nullptr),tail(nullptr){
		}

		~linklist(){
			this->clear();
		}

		linklist(const linklist&) = delete;
		linklist(linklist&&) = delete;
		linklist& operator = (const linklist&) = delete;

		sendContext *front() {
			if(this->head == nullptr) {
				return nullptr;
			} else {
				return this->head;
			}
		}

		void pop_front() {
			if(this->head != nullptr){
				auto s = this->head;
				this->head = s->next;
				if(nullptr == this->head) {
					this->tail = nullptr;
				}
				putSendContext(s);
			}
		}

		void push_back(sendContext *s) {
			if(this->head == nullptr) {
				this->head = this->tail = s;
			} else {
				this->tail->next = s;
				this->tail = s;
			}
		}

		void clear() {
			if(this->head != nullptr) {
				auto cur = this->head;
				while(cur != nullptr){
					auto c = cur;
					cur = c->next;
					putSendContext(c);
				}
				this->head = this->tail = nullptr;
			}		
		}

		bool empty(){
			return this->head == nullptr;
		}


		void add_front(linklist *other){
			if(!other->empty()){
				if(this->head == nullptr){
					this->head = other->head;
					this->tail = other->tail;
				} else {
					other->tail->next = this->head;	
					this->head = other->head;
				}
				other->head = other->tail = nullptr;
			}
		}
	};


public:
	static TCPSocket::Ptr New(Poller *poller_,int fd){
		SetNoBlock(fd,true);
		SetCloseOnExec(fd);
		return Ptr(new TCPSocket(poller_,fd));
	}

	static TCPSocket::Ptr New(Poller *poller_,ThreadPool *pool_,int fd){
		SetNoBlock(fd,true);
		SetCloseOnExec(fd);
		return Ptr(new TCPSocket(poller_,pool_,fd));
	}

#ifdef USE_SSL

	static TCPSocket::Ptr New(Poller *poller_,int fd,SSL *ssl){
		SetNoBlock(fd,true);
		SetCloseOnExec(fd);
		auto ptr = Ptr(new TCPSocket(poller_,fd));
		ptr->ssl = ssl;
		return ptr;
	}

	static TCPSocket::Ptr New(Poller *poller_,ThreadPool *pool_,int fd,SSL *ssl){
		SetNoBlock(fd,true);
		SetCloseOnExec(fd);
		auto ptr = Ptr(new TCPSocket(poller_,pool_,fd));
		ptr->ssl = ssl;
		return ptr;
	}

#endif



	TCPSocket::Ptr SetHighWater(const HighWaterCallback &callback,size_t highWaterSize_){					
		std::lock_guard<std::mutex> guard(this->mtx);			
		this->highWaterSize = highWaterSize_;
		this->highWaterCallback_ = callback;
		return shared_from_this();
	}

	TCPSocket::Ptr SetFlushCallback(const FlushCallback &callback){
		std::lock_guard<std::mutex> guard(this->mtx);
		flushCallback_ = callback;
		return shared_from_this();		
	}

	TCPSocket::Ptr SetErrorCallback(const ErrorCallback &callback){ 
		std::lock_guard<std::mutex> guard(this->mtx);
	    errorCallback_ = callback;
		return shared_from_this();	    
	}

	TCPSocket::Ptr SetCloseCallback(const CloseCallback &callback){
		std::lock_guard<std::mutex> guard(this->mtx);
	    closeCallback_ = callback;
		return shared_from_this();	    
	}

	TCPSocket::Ptr SetRecvCallback(const RecvCallback &callback){
		std::lock_guard<std::mutex> guard(this->mtx);
	    recvCallback_ = callback;
		return shared_from_this();	    
	}	

	/*
	 * 以下两个超时处理在回调一次之后将会关闭，如需要再次开启必须重新注册 
	 */

	TCPSocket::Ptr SetRecvTimeoutCallback(util::milliseconds timeout, const RecvTimeoutCallback &callback);

	TCPSocket::Ptr SetSendTimeoutCallback(util::milliseconds timeout, const SendTimeoutCallback &callback);

	/*
	 *  将socket添加到poller中,如果需要设置ErrorCallback,务必在Start之前设置。
	 */
	TCPSocket::Ptr Start() {
		bool expected = false;
		if(this->started.compare_exchange_strong(expected,true)) {
			this->poller_->Add(shared_from_this(),Poller::Read | Poller::Write | Poller::ET);
			//this->poller_->Add(shared_from_this(),Poller::Read | Poller::ET);
		}
		return shared_from_this();
	}

	void Shutdown();
	
	void Close();

	bool IsClosed() {
		std::lock_guard<std::mutex> guard(this->mtx);
		return this->closed;
	}
	
	/*
	 *  recv接收数据时取buff->Len()   
	 */

	void Recv(const Buffer::Ptr &buff);
	
	/*
	 *  如果len == 0,发送取buff->Len()
	 */
	void Send(const Buffer::Ptr &buff,size_t len = 0);
	
	void Send(const char *str,size_t len);
	
	void Send(const std::string &str);

	void SendAndClose(const Buffer::Ptr &buff,size_t len = 0);

	void SendAndClose(const char *str,size_t len);

	void SendAndClose(const std::string &str);

	const Addr& RemoteAddr() const;

	const Addr& LocalAddr() const;

	const any& GetUserData() const {
		std::lock_guard<std::mutex> guard(this->mtx);
		return this->ud;
	}


	TCPSocket::Ptr SetUserData(any ud_) {
		std::lock_guard<std::mutex> guard(this->mtx);
		this->ud = ud_;	
		return shared_from_this();	
	}

	void OnActive(int event);

	virtual void Do();

	int Fd() const {
		return fd;
	}

	~TCPSocket();

private:

	void _recv(const Buffer::Ptr &buff,bool &post);

	void _send(const Buffer::Ptr &buff,size_t len,bool closedOnFlush,bool &post);	

	void registerTimer(int);

	void checkTimeout(hwnet::util::Timer::Ptr t);

	static void onTimer(const hwnet::util::Timer::Ptr &t,TCPSocket::Ptr s);

	bool highWater() {
		return highWaterSize > 0 && bytes4Send > highWaterSize;
	}

	bool canRead() {

#ifdef GATHER_RECV
		return (readable && !ptrRecvlist->empty()) || socketError;
#else
		return (readable && !recvList.empty()) || socketError;
#endif
	}

	bool recvListEmpty() {
#ifdef GATHER_RECV
		return ptrRecvlist->empty();
#else
		return recvList.empty();
#endif		
	}

	bool canWrite() {
		return (!shutdown) && (!closed) && (!socketError) && ((writeable && !this->ptrSendlist->empty()) || highWater()); 
	}

	void sendInWorker();
	void recvInWorker();

#ifdef USE_SSL
	void sendInWorkerSSL();
#endif


	TCPSocket(Poller *poller_,int fd);

	TCPSocket(Poller *poller_,ThreadPool *pool_,int fd);

	TCPSocket(const TCPSocket&) = delete;
	TCPSocket& operator = (const TCPSocket&) = delete;

	void doClose();

	static const int       max_send_iovec_size = 128;
	static const int       max_recv_iovec_size = 128;
	iovec                  send_iovec[max_send_iovec_size];

#ifdef GATHER_RECV 
	iovec                  recv_iovec[max_recv_iovec_size];
	std::list<Buffer::Ptr> recvLists[2];
	int                    rlistIdx;
	std::list<Buffer::Ptr> *ptrRecvlist;
#else
	std::list<Buffer::Ptr> recvList;
#endif

	linklist               sendLists[2];
	int                    slistIdx;
	linklist               *ptrSendlist;
	int 			       fd;
	int                    err;
	FlushCallback          flushCallback_;
	ErrorCallback          errorCallback_;
	CloseCallback          closeCallback_;
	HighWaterCallback      highWaterCallback_;
	RecvCallback           recvCallback_;
	bool                   readable;
	int                    readableVer;	
	bool                   writeable;
	int                    writeableVer;
	std::atomic_bool       closed;
	bool                   closedOnFlush; //sendlist被清空后关闭连接
	bool                   shutdown;
	bool                   doing;
	bool                   socketError;
	Poller                *poller_;
	mutable std::mutex     mtx;
	size_t                 bytes4Send;
	size_t                 highWaterSize;
	mutable Addr           remoteAddr;
	mutable Addr           localAddr;
	ThreadPool             *pool_;
	any                    ud;
	std::atomic_bool       started;
	std::thread::id 	   tid;
	std::list<std::function<void (void)>> closures;
	
	util::Timer::WeakPtr   timer;
	
	util::milliseconds     recvTimeout;
	RecvTimeoutCallback    recvTimeoutCallback_;
	std::chrono::steady_clock::time_point lastRecvTime;

	util::milliseconds     sendTimeout;
	SendTimeoutCallback    sendTimeoutCallback_;
	std::chrono::steady_clock::time_point lastSendTime;


#ifdef USE_SSL
	SSL                    *ssl;
#endif

};

}
#endif


