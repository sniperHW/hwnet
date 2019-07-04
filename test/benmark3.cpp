#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	"net/Poller.h"
#include 	"net/TCPSocket.h"
#include 	"net/Address.h"
#include    "net/TCPListener.h"
#include    "net/TCPConnector.h"
#include 	<time.h>
#include 	<unistd.h>
#include    <vector>

/*
 *  Codecc非线程安全，所以必须将TCPSocket绑定到固定的处理线程
 *  对于非绑定线程方式,hwnet保证TCPSocket同时只被一个线程处理,但是，如果两次处理之间发生线程切换
 *  后面的处理不一定能看到第一次处理时的最新值，为了保证能取到最新值，Codecc中的变量需要使用volatile修饰
 *
 *
 */

using namespace hwnet;

std::atomic<std::int64_t>  bytes(0);
std::atomic_int  count(0);
std::atomic_bool serverStarted(false);
std::atomic_int  clientcount(0);

Poller poller_;

std::vector<ThreadPool*> pools;

int packetSize = 4096;

class Codecc {

public:

	Codecc(size_t maxPacketSize):w(0),r(0),maxPacketSize(maxPacketSize) {
		this->buffer = Buffer::New(maxPacketSize*2,maxPacketSize*2);
	}

	std::pair<Buffer::Ptr,int> Decode() {
		size_t size = this->w - this->r;
		//std::cout << "decode " << this->w << "," << this->r << std::endl; 
		if(size > 4) {
			char *ptr = this->buffer->BuffPtr() + this->r;
			size_t len = (size_t)*(int*)&ptr[0];//包体大小
			if(size >= len+4) {
				Buffer::Ptr b = Buffer::New(len+4);
				b->Append(ptr,len+4);

				//int len = *(int*)&b->BuffPtr()[0];
				//int no = *(int*)&b->BuffPtr()[4];
				//std::cout << len << "," << no << "," << Buffer::New(b,8,b->Len())->ToString()  << std::endl;

				this->r += (len+4);
				return std::make_pair(b,0);
			}
		}
		return std::make_pair(nullptr,0);
	}

	void onData(size_t n) {
		this->w += n;
	}

	Buffer::Ptr GetRecvBuffer() {		
		if(this->w == this->r) {
			this->w = this->r = 0;
			return this->buffer;
		} else {
			if(this->buffer->Cap() - this->w < this->maxPacketSize/4) {
				char *ptr = this->buffer->BuffPtr();
				memmove(ptr,&ptr[this->r],this->w-this->r);
				this->w = this->w - this->r;
				this->r = 0;
			}
			return Buffer::New(this->buffer,this->w,this->buffer->Cap());		
		}
	}

private:

	Buffer::Ptr buffer;
	//如果不是绑定线程需要打开volatile	
	/*volatile*/ size_t w;
	/*volatile*/ size_t r;
	size_t maxPacketSize;
};


void show() {
	for(;;){
		printf("client:%d packetcount:%d bytes:%d MB/sec\n",clientcount.load(),count.load(),(int)(bytes.load()/1024/1024));
		bytes.store(0);
		count.store(0);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

class Timer : public Task,public std::enable_shared_from_this<Timer> {

public:

	static Timer::Ptr New() {
		return Timer::Ptr(new Timer);
	}

	void Do() {
		show();
	}
	~Timer(){}
};

void onDataServer(TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	bytes += n;
	Codecc *code = any_cast<Codecc*>(ss->GetUserData());
	code->onData(n);
	for( ; ; ) {
		if(ss->IsClosed()) return;
		std::pair<Buffer::Ptr,int> ret = code->Decode();
		if(ret.second == 0) {
			if(ret.first != nullptr) {
				count++;
				//int len = *(int*)&ret.first->BuffPtr()[0];
				//int no = *(int*)&ret.first->BuffPtr()[4];
				//std::cout << len << "," << no << "," << Buffer::New(ret.first,8,ret.first->Len())->ToString()  << std::endl;

				ss->Send(ret.first);
			} else {
				break;
			}
		} else {
			ss->Close();
			return;
		}
	}
	ss->Recv(code->GetRecvBuffer());
}

void onDataClient(TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	Codecc *code = any_cast<Codecc*>(ss->GetUserData());
	code->onData(n);
	for( ; ; ) {
		if(ss->IsClosed()) return;
		std::pair<Buffer::Ptr,int> ret = code->Decode();
		if(ret.second == 0) {
			if(ret.first != nullptr) {
				ss->Send(ret.first);
			} else {
				break;
			}
		} else {
			ss->Close();
			return;
		}
	}
	ss->Recv(code->GetRecvBuffer());
}


void onClose(TCPSocket::Ptr &ss) {
	clientcount--;
	printf("onClose\n");
}

void onError(TCPSocket::Ptr &ss,int err) {
	printf("onError error:%d %s\n",err,strerror(err));
	ss->Close();
}

void onAcceptError(TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
}

void onClient(TCPListener::Ptr &l,int fd,const Addr &addr) {
	clientcount++;
	ThreadPool *t = pools[fd%pools.size()];
	auto sc = TCPSocket::New(&poller_,t,fd);
	auto code = new Codecc(packetSize);
	sc->SetUserData(code)->SetRecvCallback(onDataServer)->SetCloseCallback(onClose)->SetErrorCallback(onError)->Start()->Recv(code->GetRecvBuffer());
	std::cout << sc->LocalAddr().ToStr() << " <-> " << sc->RemoteAddr().ToStr() << std::endl;
}


const char *ip = "localhost";
const int   port = 8888;

void server() {
	poller_.PostTask(Timer::New());
	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
	serverStarted.store(true);
}

void onConnect(int fd) {
	printf("onConnect\n");
	ThreadPool *t = pools[fd%pools.size()];
	auto sc = TCPSocket::New(&poller_,t,fd);
	auto code = new Codecc(packetSize);
	sc->SetUserData(code)->SetRecvCallback(onDataClient)->SetErrorCallback(onError)->Start()->Recv(code->GetRecvBuffer());
	for(int i = 0; i < 10; i++){
		std::string msg = std::string("hello");
		auto sendMsg = Buffer::New(5+4+4);
		int len = 9;
		sendMsg->Append((const char *)&len,4);
		sendMsg->Append((const char *)&i,4);
		sendMsg->Append(msg);
		sc->Send(sendMsg);
	}
}

void connectError(int err,const Addr &remote) {
	printf("connectError %s\n",strerror(err));
}

void client(int count) {
	for(int i = 0; i < count; ++i) {
		if(!TCPConnector::New(&poller_,Addr::MakeIP4Addr(ip,port))->Connect(onConnect,connectError)){
			printf("connect error\n");
		}
	}
}


int main(int argc,char **argv) {

	auto count = 0;
	auto threadCount = std::thread::hardware_concurrency();

	auto mode = argv[2];

	if(argc < 3) {
		printf("usage benmark threadCount both|server|client clientcount packetSize\n");
		return 0;
	}

	threadCount = ::atoi(argv[1]);

	if(argc > 3) {
		count = ::atoi(argv[3]);
	}

	if(argc > 4) {
		packetSize = ::atoi(argv[4]);
	}

	printf("threadCount:%d\n",threadCount);
	
	ThreadPool pool;
	pool.Init(2);//1 for timer,1 for listener,connector

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}


	//每个pool只有一个线程，因此TCPSocket被绑定到单一线程上
	for(int i = 0; i < int(threadCount); i++) {
		ThreadPool *t = new ThreadPool();
		t->Init(1,ThreadPool::SwapMode);
		pools.push_back(t);
	}


	if(mode == std::string("both")) {
		auto s = std::thread(server);
		s.detach();
		for( ; !serverStarted.load() ;);
		client(count);
	} else if(mode == std::string("server")) {
		auto s = std::thread(server);
		s.detach();
	} else if(mode == std::string("client")) {
		client(count);
	} else {
		printf("usage benmark threadCount both|server|client clientcount packetSize\n");
		return 0;		
	}

	poller_.Run();

	return 0;
}