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

using namespace hwnet;
using namespace hwnet::http;


Poller poller_;

const char *ip = "localhost";
const int   port = 8888;


void onConnect(int fd) {
	printf("onConnect\n");
	auto sc = TCPSocket::New(&poller_,fd);
	
	auto session = HttpSession::New(sc,HttpSession::ClientSide);
	session->Start([session](HttpResponse::Ptr &resp) {
		std::cout << "on response" << std::endl;
		resp->OnBody([resp](const char *data, size_t length){
			if(data){
				std::cout << "on body:" << std::string(data,length) << std::endl; 
			} else {
				poller_.Stop();
			}
		});	
	});

	auto req = HttpRequest(session,std::shared_ptr<HttpPacket>(new HttpPacket));
	req.SetMethod(hwnet::http::HTTP_GET).SetUrl("/").SetField("Host","localhost:8888").SetField("Content-Length","5");
	req.WriteHeader();
	req.WriteBody("hello");
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
