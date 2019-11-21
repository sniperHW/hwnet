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
#include    <signal.h>

using namespace hwnet;


int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);
	auto threadCount = std::thread::hardware_concurrency();

	ThreadPool pool;
	Poller poller_;
	pool.Init(threadCount);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	auto pollerPtr = &poller_;

	TCPListener::New(&poller_,Addr::MakeIP4Addr("localhost",8110))->Start([pollerPtr](const TCPListener::Ptr &l,int fd,const Addr &addr) {
		auto sc = TCPSocket::New(pollerPtr,fd);
		auto recvBuff = Buffer::New(1024,1024);
		sc->SetCloseCallback([](const TCPSocket::Ptr &ss){
			std::cout << ss->LocalAddr().ToStr() << " <-> " << ss->RemoteAddr().ToStr() << " " << "leave" << std::endl;
		});
		sc->SetRecvCallback([](const TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
			ss->Send(buff,n);	
		});
		sc->SetFlushCallback([recvBuff](const TCPSocket::Ptr &s){
			s->Recv(recvBuff);
		});
		sc->Start()->Recv(recvBuff);
		std::cout << sc->LocalAddr().ToStr() << " <-> " << sc->RemoteAddr().ToStr() << " " << "comming" << std::endl;	
	});	

	poller_.Run();

	return 0;
}