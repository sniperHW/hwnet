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

std::string longstrings[] = {
	std::string(1024,'a'),
	std::string(1024,'a'),
	std::string(1024,'a'),
	std::string(1024,'a'),	
};


void onClient(const TCPListener::Ptr &l,int fd,const Addr &addr) {
	auto sc = TCPSocket::New(&poller_,fd);
	auto session = HttpSession::New(sc,HttpSession::ServerSide);
	session->Start([session](HttpRequest::Ptr &req,HttpResponse::Ptr &resp) {
		std::cout << "on request" << std::endl;
		req->OnBody([req,resp](const char *data, size_t length){
			if(data){
				std::cout << "on body:" << std::string(data,length) << std::endl; 
			} else {
				resp->SetStatusCode(200).SetStatus("OK").SetField("a","b").SetField("Content-Length","4096");
				resp->WriteHeader();
				auto i = 0;

				std::function<void ()> flushComplete;

				flushComplete = [flushComplete,&i,resp](){
					std::this_thread::sleep_for(std::chrono::seconds(1));
					resp->WriteBody(longstrings[++i],flushComplete);
				};

				resp->WriteBody(longstrings[i],flushComplete);
			}
		});	
	});
}

void onAcceptError(const TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
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

	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
	
	poller_.Run();

	return 0;
}
