#include "test/dns/Resolver.h"

#include <ares.h>
#include <netdb.h>
#include <arpa/inet.h>  // inet_ntop
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <unistd.h>


double getSeconds(struct timeval* tv)
{
  if (tv)
    return double(tv->tv_sec) + double(tv->tv_usec)/1000000.0;
  else
    return -1.0;
}

void Resolver::QueryChannel::OnActive(int event) {
  if(event & hwnet::Poller::ReadFlag()) {
      this->r->poller_->PostClosure([](Resolver *r,int fd){
        ares_process_fd(r->ctx_, fd, ARES_SOCKET_BAD);
      },this->r,this->fd);
  }
}

Resolver::Resolver(hwnet::Poller* poller, Option opt)
  : poller_(poller),ctx_(NULL),timerActive_(false)
{
  static char lookups[] = "b";
  struct ares_options options;
  int optmask = ARES_OPT_FLAGS;
  options.flags = ARES_FLAG_NOCHECKRESP;
  options.flags |= ARES_FLAG_STAYOPEN;
  options.flags |= ARES_FLAG_IGNTC; // UDP only
  optmask |= ARES_OPT_SOCK_STATE_CB;
  options.sock_state_cb = &Resolver::ares_sock_state_callback;
  options.sock_state_cb_data = this;
  optmask |= ARES_OPT_TIMEOUT;
  options.timeout = 2;
  if (opt == kDNSonly)
  {
    optmask |= ARES_OPT_LOOKUPS;
    options.lookups = lookups;
  }

  int status = ares_init_options(&ctx_, &options, optmask);
  if (status != ARES_SUCCESS)
  {
    assert(0);
  }
  ares_set_socket_callback(ctx_, &Resolver::ares_sock_create_callback, this);
}

Resolver::~Resolver()
{
  ares_destroy(ctx_);
}

bool Resolver::resolve(const std::string hostname, const Callback& cb)
{
  QueryData* queryData = new QueryData(this, cb);
  if(queryData){
      poller_->PostClosure([](Resolver *r,const std::string hostname,QueryData* queryData){
        ares_gethostbyname(r->ctx_, hostname.c_str(), AF_INET, &Resolver::ares_host_callback, queryData);
        struct timeval tv;
        struct timeval* tvp = ares_timeout(r->ctx_, NULL, &tv);
        double timeout = getSeconds(tvp);

        if(!r->timerActive_) {
            r->addTimer(timeout);
        }
      },this,hostname,queryData);
  }
  return queryData != NULL;  
}

void Resolver::addTimer(double timeout) {
  timerActive_ = true;
  poller_->addTimerOnce(timeout*1000,[this](hwnet::util::Timer::Ptr t){
      this->poller_->PostClosure([](Resolver *r){
        r->onTimer();
      },this);
  });
}

void Resolver::onTimer() {
  ares_process_fd(ctx_, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
  struct timeval tv;
  struct timeval* tvp = ares_timeout(ctx_, NULL, &tv);
  double timeout = getSeconds(tvp);

  if (timeout < 0)
  {
    timerActive_ = false;
  }
  else
  {
    addTimer(timeout);
  }
}

void Resolver::onQueryResult(int status, struct hostent* result, const Callback& callback)
{
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  if (result)
  {
    addr.sin_addr = *reinterpret_cast<in_addr*>(result->h_addr);
  }
  callback(hwnet::Addr::MakeBySockAddr((struct sockaddr*)&addr,sizeof(addr)));
}

void Resolver::onSockCreate(int sockfd, int type)
{
  assert(channels_.find(sockfd) == channels_.end());
  auto sp = QueryChannel::Ptr(new QueryChannel(this,sockfd));
  this->poller_->Add(sp,hwnet::Poller::Read | hwnet::Poller::ET);
  channels_[sockfd] = sp;
}

void Resolver::onSockStateChange(int sockfd, bool read, bool write)
{
  ChannelList::iterator it = channels_.find(sockfd);
  assert(it != channels_.end());
  if (read)
  {
    // updater
    // if (write) { } else { }
  }
  else
  {
    this->poller_->Remove(it->second);
    channels_.erase(it);
  }
}

void Resolver::ares_host_callback(void* data, int status, int timeouts, struct hostent* hostent)
{
  QueryData* query = static_cast<QueryData*>(data);
  query->owner->onQueryResult(status, hostent, query->callback);
  delete query;
}

int Resolver::ares_sock_create_callback(int sockfd, int type, void* data)
{
  static_cast<Resolver*>(data)->onSockCreate(sockfd, type);
  return 0;
}

void Resolver::ares_sock_state_callback(void* data, int sockfd, int read, int write)
{
  static_cast<Resolver*>(data)->onSockStateChange(sockfd, read, write);
}

