#include "src/redis/RedisCli.h"

namespace hwnet { namespace redis {

void RedisConn::redisAddRead(void *privdata) {
    auto conn = (RedisConn*)privdata;
    if(!(conn->events & Poller::Read)) {
    	conn->events = conn->poller_->Enable(conn->GetSharePtr(),Poller::Read,conn->events);
    }
}

void RedisConn::redisDelRead(void *privdata) {
    auto conn = (RedisConn*)privdata;
    if(conn->events & Poller::Read) {
    	conn->events = conn->poller_->Disable(conn->GetSharePtr(),Poller::Read,conn->events);
    }
}

void RedisConn::redisAddWrite(void *privdata) {
    auto conn = (RedisConn*)privdata;
    if(!(conn->events & Poller::Write)) {
    	conn->events = conn->poller_->Enable(conn->GetSharePtr(),Poller::Write,conn->events);
    }	
}

void RedisConn::redisDelWrite(void *privdata) {
    auto conn = (RedisConn*)privdata;
	if(conn->events & Poller::Write) {    
    	conn->events = conn->poller_->Disable(conn->GetSharePtr(),Poller::Write,conn->events);
    }	
}

void RedisConn::redisCleanup(void *privdata) {
	auto conn = (RedisConn*)privdata;
	conn->events = conn->poller_->Disable(conn->GetSharePtr(), Poller::Read | Poller::Write,conn->events);
}


void RedisConn::connectCallback(const redisAsyncContext *c, int status) {
	auto conn = (RedisConn*)c->data;
	conn->mtx.unlock();
	auto ptr = conn->GetSharePtr();
	if(status == REDIS_OK){
		conn->connectCallback_(ptr,"ok");
	} else {
		conn->poller_->Remove(ptr);
		conn->connectCallback_(nullptr,c->errstr);
	}
	conn->mtx.lock();
}

void RedisConn::disconnectCallback(const redisAsyncContext *c, int status) {
	auto conn = (RedisConn*)c->data;
	conn->mtx.unlock();
	conn->closed = true;
	auto ptr = conn->GetSharePtr();
	conn->poller_->Remove(ptr);
	if(status == REDIS_OK){
		conn->disconnectedCallback_(ptr,"ok");
	} else {
		conn->disconnectedCallback_(ptr,c->errstr);
	}
	conn->mtx.lock();   
}


RedisConn::~RedisConn() {

}

void RedisConn::OnActive(int event) {
	this->mtx.lock();

	if(event & (Poller::ReadFlag() | Poller::ErrorFlag())) {
		redisAsyncHandleRead(this->context);
	}

	if(event & Poller::WriteFlag()) {
		redisAsyncHandleWrite(this->context);
	}

	this->mtx.unlock();
}

bool RedisConn::AsyncConnect(Poller *poller_,const std::string &ip,int port,
		const ConnectCallback &connectCb,const DisconnectedCallback &disconnectCb) {

	if(!poller_ || !connectCb || !disconnectCb) {
		return false;
	}

	redisAsyncContext *ac = redisAsyncConnect(ip.c_str(), port);
    if (ac->err) {
        return false;
    }

    auto conn = Ptr(new RedisConn);
    conn->context = ac;
    conn->poller_ = poller_;
    conn->connectCallback_ = connectCb;
    conn->disconnectedCallback_ = disconnectCb;

    ac->ev.addRead  = RedisConn::redisAddRead;
    ac->ev.delRead  = RedisConn::redisDelRead;
    ac->ev.addWrite = RedisConn::redisAddWrite;
    ac->ev.delWrite = RedisConn::redisDelWrite;
    ac->ev.cleanup  = RedisConn::redisCleanup;
    ac->ev.data = conn.get();
    ac->data = conn.get();

    redisAsyncSetConnectCallback(ac,RedisConn::connectCallback);
    redisAsyncSetDisconnectCallback(ac,RedisConn::disconnectCallback);

    conn->events = poller_->Add(conn,Poller::Read | Poller::Write);

    return true;
}

void RedisConn::getCallback(redisAsyncContext *c, void *r, void *privdata) {
	auto conn = (RedisConn*)c->data;
	RedisCallback &fn = conn->redisFns.front();
	conn->mtx.unlock();
	fn(conn->GetSharePtr(),(redisReply*)r,privdata);
	conn->mtx.lock();
	conn->redisFns.pop_front();
}

int RedisConn::redisAsyncCommand(const RedisCallback &fn, void *privdata, const char *format, ...) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed){
    	va_list ap;
    	int status;
    	va_start(ap,format);
    	if(fn) {
    		status = redisvAsyncCommand(this->context,RedisConn::getCallback,privdata,format,ap);
    		if(status == REDIS_OK) {
    			this->redisFns.push_back(fn);
    		}
    	} else {
    		status = redisvAsyncCommand(this->context,nullptr,privdata,format,ap);
    	}
    	va_end(ap);
    	return status;
	} else {
		return REDIS_ERR;
	}
}

}}