#ifndef _SSLHANDSHAKE_H
#define _SSLHANDSHAKE_H


#include <mutex>
#include <functional>
#include <atomic>
#include <thread>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "net/Address.h"
#include "net/Poller.h"
#include "SocketHelper.h"


namespace hwnet {


class SSLHandshake : public Task, public Channel ,public std::enable_shared_from_this<SSLHandshake> {

public:
	typedef std::shared_ptr<SSLHandshake> Ptr;

	typedef std::function<void (int,SSL *ssl)> OnHandshakeOK;

	typedef std::function<void (int)> OnError;

	typedef int (*handshakeMethod)(SSL*);

	void OnActive(int event);

	void Do();

	int Fd() const {
		return fd;
	}


	static bool Accept(Poller *poller_,int fd,SSL_CTX *ssl_ctx,const OnHandshakeOK &onOK,const OnError &onErr) {
		return SSLHandshake::New(poller_,fd,ssl_ctx,onOK,onErr,SSL_accept);
	}

	static bool Connect(Poller *poller_,int fd,SSL_CTX *ssl_ctx,const OnHandshakeOK &onOK,const OnError &onErr) {
		return SSLHandshake::New(poller_,fd,ssl_ctx,onOK,onErr,SSL_connect);
	}	

	~SSLHandshake(){
		if(this->ssl) {
	       	SSL_free(this->ssl);
		}		
	}

private:

	static bool New(Poller *poller_,int fd,SSL_CTX *ssl_ctx,const OnHandshakeOK &onOK,const OnError &onErr,handshakeMethod method) {
		if(!poller_ || !ssl_ctx || !onOK) {
			return false;
		} else { 
			auto ptr = SSLHandshake::Ptr(new SSLHandshake(poller_,fd,onOK,onErr));
			SetNoBlock(fd,true);

			ptr->ssl = SSL_new(ssl_ctx);
			if(!ptr->ssl) {
				ERR_print_errors_fp(stdout);
				return false;
			}

			if(!SSL_set_fd(ptr->ssl,fd)){
				ERR_print_errors_fp(stdout);		
				return false;
			}

			ptr->handshake = method;
			poller_->Add(ptr,Poller::Read | Poller::Write | Poller::ET);

			return true;
		}
	}


	SSLHandshake(Poller *poller_,int fd,const OnHandshakeOK &onOK,const OnError &onErr):
		fd(fd),poller_(poller_),onOK_(onOK),onError_(onErr),ver(0),doing(false),socketError(false),err(0) {

	}

	SSLHandshake(const SSLHandshake&) = delete;
	SSLHandshake& operator = (const SSLHandshake&) = delete;

	int              fd;
	Poller           *poller_;
	SSL 			 *ssl;
	OnHandshakeOK    onOK_;
	OnError          onError_;
	std::mutex       mtx;
	int              ver;
	bool             doing;	
	bool             socketError;
	int              err;
	handshakeMethod  handshake;
};

}


#endif