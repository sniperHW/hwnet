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

/*
 *       TCPSocket可以由Poller中任意一个空闲线程执行
 *
 */


using namespace hwnet;


Poller poller_;

void onDataClient(TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	std::cout << buff->BuffPtr() << std::endl;
	poller_.Stop();
}


void onClose(TCPSocket::Ptr &ss) {
	printf("onClose\n");
}

void onError(TCPSocket::Ptr &ss,int err) {
	printf("onError error:%d %s\n",err,strerror(err));
	ss->Close();
}


const char *ip = "localhost";
const int   port = 8888;


void onConnect(int fd) {
	printf("onConnect\n");
	std::string request = "GET / HTTP/1.1\r\nHost: localhost:8888\r\nContent-Length:5\r\n\r\nhello";
	auto buff = Buffer::New(request);
	auto sc = TCPSocket::New(&poller_,fd);
	sc->SetRecvCallback(onDataClient)->SetErrorCallback(onError);	
	sc->Start();
	sc->Send(buff);
	sc->Recv(Buffer::New(1024,1024));
}

void connectError(int err,const Addr &remote) {
	printf("connectError %s\n",strerror(err));
}

int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);

	ThreadPool pool;
	pool.Init(1);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}


	TCPConnector::New(&poller_,Addr::MakeIP4Addr(ip,port))->Connect(onConnect,connectError);
	poller_.Run();
	return 0;
}
