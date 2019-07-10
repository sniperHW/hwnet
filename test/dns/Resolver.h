#ifndef DNS_RESOLVER_H
#define DNS_RESOLVER_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <list>
#include "net/Address.h"
#include "net/Poller.h"

extern "C"
{
  struct hostent;
  struct ares_channeldata;
  typedef struct ares_channeldata* ares_channel;
}


class Resolver : public hwnet::Task ,public std::enable_shared_from_this<Resolver> {



class QueryChannel : public hwnet::Channel,public std::enable_shared_from_this<QueryChannel> {
public:
  typedef std::shared_ptr<QueryChannel> Ptr;

  std::weak_ptr<Resolver> r;
  int                     fd;

  QueryChannel(const std::weak_ptr<Resolver> &r,int fd):r(r),fd(fd){}

  void OnActive(int event);
  
  int  Fd() const {
    return fd;
  }  

};


public:
  typedef std::function<void(const hwnet::Addr&)> Callback;
  typedef std::shared_ptr<Resolver> Ptr;
  enum Option
  {
    kDNSandHostsFile,
    kDNSonly,
  };  

  static Resolver::Ptr New(hwnet::Poller *poller,Option opt = kDNSandHostsFile){
    return Ptr(new Resolver(poller,opt));
  }

  ~Resolver();

  bool resolve(const std::string hostname, const Callback& cb);  

  void Do();

private: 

  explicit Resolver(hwnet::Poller* poller, Option opt = kDNSandHostsFile);

  struct QueryData
  {
    Resolver* owner;
    Callback callback;
    QueryData(Resolver* o, const Callback& cb)
      : owner(o), callback(cb)
    {
    }
  };

  hwnet::Poller *poller_;
  ares_channel ctx_;
  typedef std::map<int, QueryChannel::Ptr> ChannelList;
  ChannelList channels_;
  std::list<std::function<void(void)>> closures;
  bool           doing;
  std::mutex     mtx;
  bool           timerActive_;
  
  void onQueryResult(int status, struct hostent* result, const Callback& cb);
  void onSockCreate(int sockfd, int type);
  void onSockStateChange(int sockfd, bool read, bool write);


  void onTimer();

  void addTimer(double timeout);

  
  template<typename F>
  void pushClouserAndPostTask(F &&closure) {
    std::lock_guard<std::mutex> guard(this->mtx);
    closures.push_back(closure);
    if(!this->doing) {
      this->doing = true;
      this->poller_->PostTask(shared_from_this());
    } 
  }

  static void ares_host_callback(void* data, int status, int timeouts, struct hostent* hostent);
  static int ares_sock_create_callback(int sockfd, int type, void* data);
  static void ares_sock_state_callback(void* data, int sockfd, int read, int write);


};


#endif 
