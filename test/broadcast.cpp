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
#include    <signal.h>

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
std::atomic_int  ccount(0);
volatile bool stoped = false;

Poller poller_;

std::vector<ThreadPool*> pools;
ThreadPool mainThread;
std::vector<TCPSocket::Ptr> clients;

int packetSize = 4096;


class TaskAdd : public Task,public std::enable_shared_from_this<TaskAdd> {

public:
	TCPSocket::Ptr socket;
	
	TaskAdd(const TCPSocket::Ptr &socket):socket(socket){}

	void Do() {
		clients.push_back(this->socket);
		clientcount++;
	}
};

class TaskDel : public Task,public std::enable_shared_from_this<TaskDel> {

public:
	TCPSocket::Ptr socket;
	TaskDel(const TCPSocket::Ptr &socket):socket(socket){}
	void Do() {
		for(size_t i = 0 ; i < clients.size(); i++) {
			if(clients[i]->Fd() == this->socket->Fd()) {
				clientcount--;
				swap(clients[i],clients[clients.size()-1]);
				clients.pop_back();
				if(clientcount == 0){
					stoped = true;
					poller_.Stop();
				}
			}
		}	
	}	
};

class TaskBroadCast : public Task,public std::enable_shared_from_this<TaskBroadCast> {

public:
	Buffer::Ptr msg;
	TaskBroadCast(const Buffer::Ptr &msg):msg(msg){}
	void Do() {
		ccount++;
		std::vector<TCPSocket::Ptr>::iterator it = clients.begin();
		std::vector<TCPSocket::Ptr>::iterator end = clients.end();
		for( ; it != end; it++){
			(*it)->Send(this->msg);
			count++;
		}
	}
	int fd;	
};

class Codecc {

public:

	Codecc(size_t maxPacketSize):w(0),r(0),maxPacketSize(maxPacketSize) {
		this->buffer = Buffer::New(8192,8192);
	}

	std::pair<Buffer::Ptr,int> Decode() {
		size_t size = this->w - this->r;
		if(size > 4) {
			char *ptr = this->buffer->BuffPtr() + this->r;
			size_t len = (size_t)*(int*)&ptr[0];//包体大小
			len = ntohl(len);
			if(size >= len+4) {
				Buffer::Ptr b = Buffer::New(len+4);
				b->Append(ptr,len+4);
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
	for(;!stoped;){
		printf("client:%d packetcount:%d ccount:%d\n",clientcount.load(),count.load(),ccount.load());
		bytes.store(0);
		count.store(0);
		ccount.store(0);
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
	//bytes += n;
	//printf("onData server\n");
	Codecc *code = any_cast<Codecc*>(ss->GetUserData());
	code->onData(n);
	for( ; ; ) {
		if(ss->IsClosed()) return;
		std::pair<Buffer::Ptr,int> ret = code->Decode();
		if(ret.second == 0) {
			if(ret.first != nullptr) {
				mainThread.PostTask(std::shared_ptr<Task>(new TaskBroadCast(ret.first)));
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
	//printf("onData client\n");
	Codecc *code = any_cast<Codecc*>(ss->GetUserData());
	code->onData(n);
	for( ; ; ) {
		if(ss->IsClosed()) return;
		std::pair<Buffer::Ptr,int> ret = code->Decode();
		if(ret.second == 0) {
			if(ret.first != nullptr) {
				count++;
				int fd = *(int*)&ret.first->BuffPtr()[4];
				if(fd == ss->Fd()){
					ccount++;
					ss->Send(ret.first);
				}
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
	mainThread.PostTask(std::shared_ptr<Task>(new TaskDel(ss)));
	printf("onClose\n");
}

void onError(TCPSocket::Ptr &ss,int err) {
	printf("onError fd:%d error:%d %s\n",ss->Fd(),err,strerror(err));
	ss->Close();
}

void onAcceptError(TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
}

void onClient(TCPListener::Ptr &l,int fd,const Addr &addr) {
	ThreadPool *t = pools[fd%pools.size()];
	auto sc = TCPSocket::New(&poller_,t,fd);
	auto code = new Codecc(packetSize);
	sc->SetUserData(code)->SetRecvCallback(onDataServer)->SetCloseCallback(onClose)->SetErrorCallback(onError);
	mainThread.PostTask(std::shared_ptr<Task>(new TaskAdd(sc)));
	sc->Start()->Recv(code->GetRecvBuffer());
	std::cout << sc->LocalAddr().ToStr() << " <-> " << sc->RemoteAddr().ToStr() << std::endl;
}


const char *ip = "localhost";
const int   port = 8888;

void server() {
	mainThread.Init(1,ThreadPool::SwapMode);
	poller_.PostTask(Timer::New());
	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
	serverStarted.store(true);
}

void onConnect(int fd) {
	printf("onConnect %d\n",fd);
	ThreadPool *t = pools[fd%pools.size()];
	auto sc = TCPSocket::New(&poller_,t,fd);
	auto code = new Codecc(packetSize);
	sc->SetUserData(code)->SetRecvCallback(onDataClient)->SetErrorCallback(onError)->Start()->Recv(code->GetRecvBuffer());
	
	std::string msg = std::string("hello");
	auto sendMsg = Buffer::New(5+4+4);
	int len = 5 + 4;
	len = htonl(len);
	sendMsg->Append((const char *)&len,4);
	sendMsg->Append((const char *)&fd,4);	
	sendMsg->Append(msg);
	sc->Send(sendMsg);
	ccount++;
	
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

	signal(SIGPIPE,SIG_IGN);

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

	printf("program end\n");

	return 0;
}
