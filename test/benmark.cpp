#include    <iostream>
#include    <unistd.h>
#include	<sys/types.h>	/* basic system data types */
#include	<sys/socket.h>	/* basic socket definitions */
#include	<sys/time.h>	/* timeval{} for select() */
#include	<time.h>		/* timespec{} for pselect() */
#include	<netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include	<arpa/inet.h>	/* inet(3) functions */
#include	<errno.h>
#include	<fcntl.h>		/* for nonblocking */
#include	<netdb.h>
#include	<signal.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/stat.h>	/* for S_xxx file mode constants */
#include	<sys/uio.h>		/* for iovec{} and readv/writev */
#include	<unistd.h>
#include	<sys/wait.h>
#include	<sys/un.h>		/* for Unix domain sockets */
#include    <net/if.h>
#include    <sys/ioctl.h>
#include    <netinet/tcp.h>
#include    <fcntl.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	"net/Poller.h"
#include 	"net/TCPSocket.h"
#include 	"net/Address.h"


using namespace hwnet;

std::atomic_int bytes(0);
std::atomic_int count(0);

Poller poller_;

int packetSize = 4096;

void onDataServer(TCPSocket::Ptr ss,Buffer::Ptr &buff) {
	bytes += buff->Len();
	count++;
	ss->Recv(Buffer::New(packetSize),onDataServer);
	ss->Send(buff);
}

void onDataClient(TCPSocket::Ptr ss,Buffer::Ptr &buff) {
	ss->Recv(Buffer::New(packetSize),onDataClient);
	ss->Send(buff);
}

void onClose(TCPSocket::Ptr ss) {
	printf("onClose\n");
}

void onError(TCPSocket::Ptr ss,int err) {
	printf("error:%d\n",err);
	ss->Close();
}

void listenFunc(int listenfd) {
	sockaddr_in addr;
	socklen_t len = 0;

	for(;;) {
		auto fd = accept(listenfd,(sockaddr*)&addr,&len);
		if(fd >= 0) {
			printf("new client\n");
			auto sc = TCPSocket::New(&poller_,fd,onClose,onError);
			sc->Recv(Buffer::New(packetSize),onDataServer);
		}
	}
}

void show() {
	for(;;){
		printf("count:%d bytes:%d MB/sec\n",count.load(),bytes.load()/1024/1024);
		bytes.store(0);
		count.store(0);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}


const char *ip = "localhost";
const int   port = 8110;

void server() {

	auto fd = ::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	int yes = 1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	
	sockaddr_in addr;

	memset(&addr,0,sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(inet_pton(AF_INET,ip,&addr.sin_addr) < 0) {  
        printf("1 %s\n",strerror(errno));
    	return;
    }	

	if(0 != bind(fd,(struct sockaddr*)&addr,sizeof(addr))) {
        printf("2 %s %d\n",strerror(errno),errno);
		return;
	}
	
	if(0 != listen(fd,SOMAXCONN)) {
		printf("3 %s\n",strerror(errno));
		return;
	}


	auto listener = std::thread(listenFunc,fd);
	auto timer = std::thread(show);

	listener.join();
	timer.join();

}

void client(int count,int packetSize) {

	sockaddr_in addr;

	memset(&addr,0,sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(inet_pton(AF_INET,ip,&addr.sin_addr) < 0) {  
        printf("1 %s\n",strerror(errno));
    	return;
    }	

	std::string msg = std::string(packetSize,'a');

	auto sendMsg = Buffer::New(msg.size());
	sendMsg->Append((char*)msg.c_str(),msg.size());
    for(int i = 0; i < count; ++i) {
		auto fd = ::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);

		int ret = ::connect(fd,(struct sockaddr*)&addr,sizeof(addr));

		if(0 == ret) {
			printf("connect ok\n");
		}

		auto sc = TCPSocket::New(&poller_,fd,onClose,onError);
		sc->Recv(Buffer::New(packetSize),onDataClient);
		sc->Send(sendMsg);
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
	if(!poller_.Init(threadCount)) {
		printf("init failed\n");
		return 0;
	}

	if(mode == std::string("both")) {
		auto s = std::thread(server);
		s.detach();
		client(count,packetSize);
	} else if(mode == std::string("server")) {
		auto s = std::thread(server);
		s.detach();
	} else if(mode == std::string("client")) {
		client(count,packetSize);
	} else {
		printf("usage benmark threadCount both|server|client clientcount packetSize\n");
		return 0;		
	}

	poller_.Run();
	return 0;
}