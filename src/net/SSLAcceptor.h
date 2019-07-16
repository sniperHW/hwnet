#ifndef _SSLACCEPTOR_H
#define _SSLACCEPTOR_H


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


class SSLAcceptor : public Task, public Channel ,public std::enable_shared_from_this<SSLAcceptor> {

public:
	typedef std::shared_ptr<SSLAcceptor> Ptr;

	typedef std::function<void (int,SSL *ssl)> OnAcceptOK;

	typedef std::function<void (int)> OnError;


	static SSLAcceptor::Ptr New(Poller *poller_,int fd,SSL_CTX *ssl_ctx,const OnAcceptOK &onOK,const OnError &onErr) {
		if(!poller_ || !ssl_ctx || !onOK) {
			return nullptr;
		} else { 
			auto ptr = SSLAcceptor::Ptr(new SSLAcceptor(poller_,fd,onOK,onErr));
			SetNoBlock(fd,true);

			ptr->ssl = SSL_new(ssl_ctx);
			if(!ptr->ssl) {
				ERR_print_errors_fp(stdout);
				return nullptr;
			}

			if(!SSL_set_fd(ptr->ssl,fd)){
				ERR_print_errors_fp(stdout);		
				return nullptr;
			}

			poller_->Add(ptr,Poller::Read | Poller::Write | Poller::ET);

			return ptr;
		}
	}

	void OnActive(int event);

	void Do();

	int Fd() const {
		return fd;
	}


	~SSLAcceptor(){
		if(this->ssl) {
	       	SSL_free(this->ssl);
		}		
	}

private:


	SSLAcceptor(Poller *poller_,int fd,const OnAcceptOK &onOK,const OnError &onErr):
		fd(fd),poller_(poller_),onOK_(onOK),onError_(onErr),ver(0),doing(false),socketError(false),err(0) {

	}

	SSLAcceptor(const SSLAcceptor&) = delete;
	SSLAcceptor& operator = (const SSLAcceptor&) = delete;

	int              fd;
	Poller           *poller_;
	SSL 			 *ssl;
	OnAcceptOK       onOK_;
	OnError          onError_;
	std::mutex       mtx;
	int              ver;
	bool             doing;	
	bool             socketError;
	int              err;
};

}


#endif