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
	auto ptr = conn->GetSharePtr();
	if(status == REDIS_OK){
		conn->connectCallback_(ptr,"ok");
	} else {
		conn->poller_->Remove(ptr);
		conn->connectCallback_(ptr,c->errstr);
	}
}

void RedisConn::disconnectCallback(const redisAsyncContext *c, int status) {
	auto conn = (RedisConn*)c->data;
	auto ptr = conn->GetSharePtr();
	conn->poller_->Remove(ptr);
	if(status == REDIS_OK){
		conn->disconnectedCallback_(ptr,"ok");
	} else {
		conn->disconnectedCallback_(ptr,c->errstr);
	}   
}


RedisConn::~RedisConn() {

}

void RedisConn::OnActive(int event) {
	if(event & (Poller::ReadFlag() | Poller::ErrorFlag())) {
		redisAsyncHandleRead(this->context);
	}

	if(event & Poller::WriteFlag()) {
		redisAsyncHandleWrite(this->context);
	}
}

bool RedisConn::AsyncConnect(Poller *poller_,const std::string &ip,int port,
		const ConnectCallback &connectCb,const DisconnectedCallback &disconnectCb) {

	if(!poller_ || !connectCb || !disconnectCb) {
		return false;
	}

	redisAsyncContext *ac = redisAsyncConnect("127.0.0.1", 6379);
    if (ac->err) {
        printf("Error: %s\n", ac->errstr);
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


int RedisConn::redisAsyncCommand(redisCallbackFn *fn, void *privdata, const char *format, ...) {
	std::lock_guard<std::mutex> guard(this->mtx);
	if(!this->closed){
    	va_list ap;
    	int status;
    	va_start(ap,format);
    	status = redisvAsyncCommand(this->context,fn,privdata,format,ap);
    	va_end(ap);
    	return status;
	} else {
		return REDIS_ERR;
	}
}






}}