#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	"net/Poller.h"
#include 	"net/TCPSocket.h"
#include 	"net/Address.h"
#include    "net/TCPListener.h"
#include    "net/SSLHandshake.h"
#include 	<time.h>
#include 	<unistd.h>
#include    <signal.h>

/*
 *       TCPSocket可以由Poller中任意一个空闲线程执行
 *
 */


using namespace hwnet;

std::atomic<std::int64_t>  bytes(0);
std::atomic_int  count(0);
std::atomic_bool serverStarted(false);
std::atomic_int  clientcount(0);

Poller poller_;

SSL_CTX *ctx;


void onAcceptError(const TCPListener::Ptr &l,int err) {
	printf("onAcceptError %s\n",strerror(err));
}

void onDataServer(const TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	bytes += n;
	count++;
	ss->Send(buff,n);	
}

void onClose(const TCPSocket::Ptr &ss) {
	printf("onClose\n");
}

void onError(const TCPSocket::Ptr &ss,int err) {
	printf("onError error:%d %s\n",err,strerror(err));
	ss->Close();
}

void onClient(const TCPListener::Ptr &l,int fd,const Addr &addr) {
	std::cout << "on client:" << fd << std::endl;
	SSLHandshake::Accept(&poller_,fd,ctx,[](int fd,SSL *ssl){
		std::cout << "ssl accept ok" << std::endl;


		auto sc = TCPSocket::New(&poller_,fd,ssl);
		auto recvBuff = Buffer::New(4096,4096);
		sc->SetRecvCallback(onDataServer)->SetCloseCallback(onClose)->SetErrorCallback(onError);
		sc->SetFlushCallback([recvBuff](const TCPSocket::Ptr &s){
			s->Recv(recvBuff);
		});

		/*sc->SetRecvTimeoutCallback(5000,[](const TCPSocket::Ptr &s){
			std::cout << "receive timeout" << std::endl;
			s->Close();
		});
		sc->SetSendTimeoutCallback(5000,[](const TCPSocket::Ptr &s){
			std::cout << "send timeout" << std::endl;
			s->Close();
		});*/

		sc->Start()->Recv(recvBuff);
		std::cout << sc->LocalAddr().ToStr() << " <-> " << sc->RemoteAddr().ToStr() << std::endl;

	},[](int err){
		std::cout << "ssl accept error:" << err << std::endl;
	});
}


const char *ip = "localhost";
const int   port = 8888;

int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);
	auto threadCount = std::thread::hardware_concurrency();

	if(argc < 2) {
		printf("usage sslserver threadCount\n");
		return 0;
	}

	const char *certificate = "./test/cacert.pem";
	const char *privatekey = "./test/privkey.pem";


    ctx = SSL_CTX_new(SSLv23_server_method());
    /* 也可以用 SSLv2_server_method() 或 SSLv3_server_method() 单独表示 V2 或 V3标准 */
    if (ctx == NULL) {
        ERR_print_errors_fp(stdout);
        return 0;
    }
    /* 载入用户的数字证书， 此证书用来发送给客户端。 证书里包含有公钥 */
    if (SSL_CTX_use_certificate_file(ctx,certificate, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stdout);
        return 0;
    }
    /* 载入用户私钥 */
    if (SSL_CTX_use_PrivateKey_file(ctx, privatekey, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stdout);
        return 0;
    }
    /* 检查用户私钥是否正确 */
    if (!SSL_CTX_check_private_key(ctx)) {
        ERR_print_errors_fp(stdout);
        return 0;
    }

	threadCount = ::atoi(argv[1]);

	printf("threadCount:%d\n",threadCount);
	
	ThreadPool pool;
	pool.Init(threadCount);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	poller_.addTimer(1000,[](hwnet::util::Timer::Ptr t){
		printf("packetcount:%d bytes:%d MB/sec\n",count.load(),(int)(bytes.load()/1024/1024));
		bytes.store(0);
		count.store(0);				
	});


	TCPListener::New(&poller_,Addr::MakeIP4Addr(ip,port))->Start(onClient,onAcceptError);
	serverStarted.store(true);

	poller_.Run();

	return 0;
}
