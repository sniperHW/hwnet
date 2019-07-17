#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	"net/Poller.h"
#include    "redis/RedisCli.h"
#include 	<time.h>
#include 	<unistd.h>
#include    <signal.h>

/*
 *       TCPSocket可以由Poller中任意一个空闲线程执行
 *
 */


using namespace hwnet;



Poller poller_;

const char *ip = "localhost";
const int   port = 6379;


void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = (redisReply *)r;
    if (reply == NULL) return;
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);
}

void connectCallback(const redis::RedisConn::Ptr &conn,const std::string &status) {
	std::cout << "connectCallback:" << status << std::endl;

    conn->redisAsyncCommand(nullptr, nullptr, "SET key huangwei");

    conn->redisAsyncCommand(getCallback, (char*)"end-1", "GET key");
}

void disconnectCallback(const redis::RedisConn::Ptr&,const std::string &status) {
	std::cout << "disconnectCallback:" << status << std::endl;
}



int main(int argc,char **argv) {
	signal(SIGPIPE,SIG_IGN);
	
	ThreadPool pool;
	pool.Init(1);

	if(!poller_.Init(&pool)) {
		printf("init failed\n");
		return 0;
	}

	redis::RedisConn::AsyncConnect(&poller_,ip,port,connectCallback,disconnectCallback);

	poller_.Run();

	return 0;
}
