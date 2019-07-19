#ifndef _REDISCLI_H
#define _REDISCLI_H

#include <list>
#include <mutex>
#include <functional>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include "net/Poller.h"
#include "redis/hiredis/hiredis.h"
#include "redis/hiredis/async.h"


namespace hwnet { namespace redis {


class RedisConn : public Channel ,public std::enable_shared_from_this<RedisConn> {

public:
	typedef std::shared_ptr<RedisConn> Ptr;

	typedef std::function<void (const RedisConn::Ptr&,const std::string &status)> ConnectCallback;

	typedef std::function<void (const RedisConn::Ptr&,const std::string &status)> DisconnectedCallback;

	typedef std::function<void (const RedisConn::Ptr&,redisReply *reply,void *privdata)> RedisCallback;


	static bool AsyncConnect(Poller *poller_,const std::string &ip,int port,
		const ConnectCallback &connectCb,const DisconnectedCallback &disconnectCb);


	int redisAsyncCommand(RedisCallback &&fn, void *privdata, const char *format, ...);


	void OnActive(int event);

	int Fd() const {
		return this->context->c.fd;
	}

	Ptr GetSharePtr() {
		return shared_from_this();
	}

	~RedisConn();

	void Close() {
		std::lock_guard<std::mutex> guard(this->mtx);
		if(!this->closed) {
			this->closed = true;
			redisAsyncDisconnect(this->context);
		}
	}

private:

	static void redisAddRead(void *privdata);
	static void redisDelRead(void *privdata); 
	static void redisAddWrite(void *privdata);
	static void redisDelWrite(void *privdata);
	static void redisCleanup(void *privdata);
	static void connectCallback(const redisAsyncContext *c, int status);
	static void disconnectCallback(const redisAsyncContext *c, int status);
	static void getCallback(redisAsyncContext *c, void *r, void *privdata);

	RedisConn():context(nullptr),poller_(nullptr),events(0),closed(false),connectCallback_(nullptr),disconnectedCallback_(nullptr){

	}

	RedisConn(const RedisConn&) = delete;
	RedisConn& operator = (const RedisConn&) = delete;

	mutable std::mutex mtx;
	redisAsyncContext *context;
	Poller             *poller_;
	int                events;
	bool               closed;

	std::list<RedisCallback> redisFns;
	ConnectCallback      connectCallback_;
	DisconnectedCallback disconnectedCallback_;

};	


}}	



#endif