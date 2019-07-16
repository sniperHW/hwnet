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
#include <signal.h>

/*
 *       TCPSocket可以由Poller中任意一个空闲线程执行
 *
 */


using namespace hwnet;

std::atomic<std::int64_t>  bytes(0);
std::atomic_int  count(0);
std::atomic_int  clientcount(0);

Poller poller_;

int packetSize = 4096;

void onDataServer(const TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	bytes += n;
	count++;
	ss->Send(buff,n);	
}

void onDataClient(const TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	ss->Send(buff,n);	
}


void onClose(const TCPSocket::Ptr &ss) {
	clientcount--;
	printf("onClose\n");
}

void onError(const TCPSocket::Ptr &ss,int err) {
	printf("onError error:%d %s\n",err,strerror(err));
	ss->Close();
}

void onAcceptError(const TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
}

void onClient(const TCPListener::Ptr &l,int fd,const Addr &addr) {
	clientcount++;
	auto sc = TCPSocket::New(&poller_,fd);
	auto recvBuff = Buffer::New(packetSize,packetSize);
	sc->SetRecvCallback(onDataServer)->SetCloseCallback(onClose)->SetErrorCallback(onError);
	sc->SetFlushCallback([recvBuff](const TCPSocket::Ptr &s){
		s->Recv(recvBuff);
	});
	sc->SetRecvTimeoutCallback(5000,[](const TCPSocket::Ptr &s){
		std::cout << "receive timeout" << std::endl;
		s->Close();
	});
	sc->SetSendTimeoutCallback(5000,[](const TCPSocket::Ptr &s){
		std::cout << "send timeout" << std::endl;
		s->Close();
	});

	sc->Start()->Recv(recvBuff);
	std::cout << sc->LocalAddr().ToStr() << " <-> " << sc->RemoteAddr().ToStr() << std::endl;	
}


const char *ip = "localhost";
const int   port = 8889;

void server() {
	poller_.addTimer(1000,[](hwnet::util::Timer::Ptr t){
		printf("client:%d packetcount:%d bytes:%d MB/sec\n",clientcount.load(),count.load(),(int)(bytes.load()/1024/1024));
		bytes.store(0);
		count.store(0);				
	});

	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
}

void onConnect(int fd) {
	printf("onConnect\n");
	std::string msg = std::string(packetSize,'a');
	auto buff = Buffer::New(msg);
	auto sc = TCPSocket::New(&poller_,fd);
	sc->SetRecvCallback(onDataClient)->SetErrorCallback(onError);
	sc->SetFlushCallback([buff](const TCPSocket::Ptr &s){
		s->Recv(buff);
	});
	sc->Start();
	sc->Send(buff);
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
	pool.Init(threadCount);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	if(mode == std::string("both")) {
		server();
		client(count);
	} else if(mode == std::string("server")) {
		server();
	} else if(mode == std::string("client")) {
		client(count);
	} else {
		printf("usage benmark threadCount both|server|client clientcount packetSize\n");
		return 0;		
	}

	poller_.Run();

	return 0;
}
