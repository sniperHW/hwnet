#ifndef _TCPSOCKET_H
#define _TCPSOCKET_H

#include "Buffer.h"
#include "Poller.h"
#include <list>
#include <mutex>
#include <functional>

namespace hwnet {

class TCPSocket : public Task, public Channel ,public std::enable_shared_from_this<TCPSocket> {

public:

	typedef std::shared_ptr<TCPSocket> Ptr;

public:

	/*
	*  接收回调，作为Recv的其中一个参数被传入
	*/
	typedef std::function<void (TCPSocket::Ptr,Buffer::Ptr &buff)> RecvCallback;
	
	/*
	*  发送完成回调，当sendList被冲刷完成之后回调
	*/
	typedef std::function<void (TCPSocket::Ptr)> WriteCompleteCallback;

	/*
	*  错误回调，当连接出现错误时回调，可根据错误码做适当处理，如果没有设置，当错误出现时将直接调用Close 
	*/
	typedef std::function<void (TCPSocket::Ptr,int)> ErrorCallback;
	
	/*
	*  连接关闭回调，连接被销毁前回调，确保只调用一次 
	*/
	typedef std::function<void (TCPSocket::Ptr)> CloseCallback;

	/*
	*  发送阻塞回调，如果设置了highwatersize和callback,当连接不可写，且待发送数据长度超过一定值时被回调
	*/
	typedef std::function<void (TCPSocket::Ptr,size_t)> HighWaterCallback;
	
	/*
	*  输入buff处理器,将输入buff转换成输出buff(例如将原始buff加密，压缩)
	*  注：此处理函数在工作者线程中执行
	*/
	typedef std::function<Buffer::Ptr (const Buffer::Ptr &)> BufferProcessFunc;

private:

	class recvReq {

	public:
		recvReq(Buffer::Ptr buff,const RecvCallback& callback):buff(buff),recvCallback_(callback){

		}

		Buffer::Ptr  buff;
		RecvCallback recvCallback_;	
	};

	class sendContext {
	public:
		sendContext(Buffer::Ptr buff):buff(buff),ptr(buff->BuffPtr()),len(buff->Len()) {

		}

		sendContext& operator = (const sendContext &o) {
			if(this != &o){
				buff = o.buff;
				ptr  = o.buff->BuffPtr();
				len  = o.buff->Len();
			}
			return *this;
		}

		Buffer::Ptr  buff;
		char        *ptr;
		size_t       len;
	};


public:
	static TCPSocket::Ptr New(Poller *poller_,int fd,const CloseCallback &closeCallback = nullptr,const ErrorCallback &errCallback = nullptr) {
		auto ret = Ptr(new TCPSocket(poller_,fd));
		ret->mtx.lock();
		ret->closeCallback_ = closeCallback;
		ret->errorCallback_ = errCallback;
		ret->mtx.unlock();
		poller_->Add(ret);
		return ret;
	}

	void SetBufferProcessFunc(const BufferProcessFunc &func) {
		std::lock_guard<std::mutex> guard(this->mtx);		
		bufferProcessFunc_ = func;
	}

	void SetHighWater(const HighWaterCallback &callback,size_t highWaterSize){		
		std::lock_guard<std::mutex> guard(this->mtx);			
		this->highWaterSize = highWaterSize;
		this->highWaterCallback_ = callback;
	}

	void Shutdown();
	
	void Close();
	
	void Recv(const Buffer::Ptr &buff,const RecvCallback& callback);
	void Send(const Buffer::Ptr &buff);
	void Send(const char *str,size_t len);
	void Send(const std::string &str);

	void OnActive(int event);

	virtual void Do();

	int Fd() const {
		return fd;
	}

	~TCPSocket() {
		printf("~StreamSocket\n");
	}

private:

	bool highWater() {
		return highWaterSize > 0 && bytes4Send > highWaterSize;
	}

	bool canRead() {
		return (readable && !recvList.empty()) || socketError;
	}

	bool canWrite() {
		return (writeable && (!sendList.empty() || !localSendList.empty())) || highWater(); 
	}

	void sendInWorker();
	void recvInWorker();

	TCPSocket(Poller *poller_,int fd);

	TCPSocket(const TCPSocket&);
	TCPSocket& operator = (const TCPSocket&); 

	void doClose();

private:
	static const int       max_recv_iovec_size = 64;
	static const int       max_send_iovec_size = 128;
	iovec                  recv_iovec[max_recv_iovec_size];
	iovec                  send_iovec[max_send_iovec_size];
	std::list<recvReq>     recvList;
	std::list<Buffer::Ptr> sendList;
	std::list<sendContext> localSendList;	
	int 			       fd;
	int                    err;
	WriteCompleteCallback  writeCompleteCallback_;
	ErrorCallback          errorCallback_;
	CloseCallback          closeCallback_;
	HighWaterCallback      highWaterCallback_;
	BufferProcessFunc      bufferProcessFunc_;
	bool                   readable;
	int                    readableVer;	
	bool                   writeable;
	int                    writeableVer;
	std::atomic_bool       closed;
	std::atomic_bool       shutdown;
	bool                   doing;
	bool                   socketError;
	Poller                *poller_;
	std::mutex             mtx;
	size_t                 bytes4Send;
	size_t                 highWaterSize;
};

}
#endif


