#include "SSLHandshake.h"
#include "SocketHelper.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

namespace hwnet {

static inline int ssl_again(int ssl_error) {
	if(ssl_error == SSL_ERROR_WANT_WRITE || ssl_error == SSL_ERROR_WANT_READ) {
		return 1;
	}
	else {
		return 0;
	}
}

void SSLHandshake::OnActive(int event) {
	
	auto post = false;

	this->mtx.lock();

	if(event & Poller::ErrorFlag()) {
  		int optval;
  		socklen_t optlen = static_cast<socklen_t>(sizeof optval);
  		if (::getsockopt(this->fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
    		this->err = errno;
  		} else {
  			this->err = optval;
  		}
		this->socketError = true;
	} else {
		++this->ver;
	}

	if(!doing) {
		this->doing = true;
		post = true;
	}
	
	this->mtx.unlock();
	
	if(post) {
		poller_->PostTask(shared_from_this());
	}

}


void SSLHandshake::Do() {

	for(;;){
		this->mtx.lock();
		if(this->socketError){
			this->mtx.unlock();
			close(this->fd);
			this->poller_->Remove(shared_from_this());
			this->onError_(this->err);
			return;
		} else {
			auto localVer = this->ver;
			this->mtx.unlock();
			auto ret = this->handshake(this->ssl);
			this->mtx.lock();
			if(ret > 0) {
				auto ssl = this->ssl;
				this->ssl = nullptr;
				this->mtx.unlock();
				this->poller_->Remove(shared_from_this());	
				this->onOK_(this->fd,ssl);
				return;		
			} else {
				auto ssl_error = SSL_get_error(this->ssl,ret);
				if(ssl_again(ssl_error)){
					if(localVer != this->ver){
						this->mtx.unlock();
						//try again
					} else {
						this->doing = false;
						this->mtx.unlock();
						return;						
					}
				} else {
					this->mtx.unlock();					
					close(this->fd);
					this->poller_->Remove(shared_from_this());	
					ERR_print_errors_fp(stdout);
					this->onError_(SSL_get_error(this->ssl,ret));
					return;					
				}
			}
		}
	}
}

}