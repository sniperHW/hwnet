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
#include    "net/http/Http.h"
#include 	<time.h>
#include 	<unistd.h>
#include    <signal.h>

/*
 *       TCPSocket可以由Poller中任意一个空闲线程执行
 *
 */


using namespace hwnet;
using namespace hwnet::http;

Poller poller_;



const char *ip = "localhost";
const int   port = 8888;


void onClient(TCPListener::Ptr &l,int fd,const Addr &addr) {
	printf("onclient\n");
	auto sc = TCPSocket::New(&poller_,fd);
	auto session = HttpSession::New(sc,HttpSession::ServerSide);
	session->Start([session](HttpRequest::Ptr &req) {
		printf("on request\n");
		//"http/1.1 200 OK\r\n";
		//req->SendResp(resp);
		auto resp = HttpResponse("http/1.1","200","OK");
		//resp.setBody("hello");
		req->SendResp(resp,TCPSocket::ClosedOnFlush);
	});
}

void onAcceptError(TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
}

void server() {
	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
}


int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);
	auto threadCount = std::thread::hardware_concurrency();

	if(argc < 2) {
		printf("usage httpserver threadCount\n");
		return 0;
	}

	threadCount = ::atoi(argv[1]);

	printf("threadCount:%d\n",threadCount);
	
	ThreadPool pool;
	pool.Init(threadCount);//plus 1 for timer

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	auto s = std::thread(server);
	s.detach();
	

	poller_.Run();

	return 0;
}
