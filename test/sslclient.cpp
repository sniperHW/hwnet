#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	"net/Poller.h"
#include 	"net/TCPSocket.h"
#include 	"net/Address.h"
#include    "net/SSLConnector.h"
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

SSL_CTX *ctx;

const char *ip = "localhost";
const int   port = 8888;

void onDataClient(const TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	ss->Send(buff,n);	 
}


void onClose(const TCPSocket::Ptr &ss) {
	printf("onClose\n");
}

void onError(const TCPSocket::Ptr &ss,int err) {
	printf("onError error:%d %s\n",err,strerror(err));
	ss->Close();
}

void onConnect(int fd) {
	std::cout << "onConnect" << std::endl;
	SSLConnector::New(&poller_,fd,ctx,[](int fd,SSL *ssl){
		std::cout << "ssl connect ok" << std::endl;
		std::string msg = std::string(4096,'a');
		auto buff = Buffer::New(msg);
		auto sc = TCPSocket::New(&poller_,fd,ssl);
		sc->SetRecvCallback(onDataClient)->SetErrorCallback(onError);
		sc->SetFlushCallback([buff](const TCPSocket::Ptr &s){
			s->Recv(buff);
		});
		sc->Start();
		sc->Send(buff);
	},[](int err){
		std::cout << "ssl connect error:" << err << std::endl;
	});

}

void connectError(int err,const Addr &remote) {
	printf("connectError %s\n",strerror(err));
}

int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);

	auto threadCount = std::thread::hardware_concurrency();

	if(argc < 3) {
		printf("usage sslclient threadCount clientcount\n");
		return 0;
	}

	int c = ::atoi(argv[2]);

	ctx = SSL_CTX_new(SSLv23_client_method());
	if(ctx == NULL) {
		ERR_print_errors_fp(stdout);
		return -1;
	}


	threadCount = ::atoi(argv[1]);

	printf("threadCount:%d\n",threadCount);
	
	ThreadPool pool;
	pool.Init(threadCount);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	for(int i = 0; i < c; i++) {
		TCPConnector::New(&poller_,Addr::MakeIP4Addr(ip,port))->Connect(onConnect,connectError);
	}

	poller_.Run();

	return 0;
}
