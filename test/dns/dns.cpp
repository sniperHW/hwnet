#include  "test/dns/Resolver.h"
#include  "net/Poller.h"
#include  "net/TCPSocket.h"
#include  "net/Address.h"

#include <stdio.h>


hwnet::Poller g_loop;
int count = 0;
int total = 0;

void quit()
{
  g_loop.Stop();
}

void resolveCallback(const std::string& host, const hwnet::Addr& addr)
{
  printf("resolveCallback %s -> %s\n", host.c_str(), addr.ToStr().c_str());
  if (++count == total)
    quit();
}

void resolve(Resolver &res, const std::string& host)
{
  res.resolve(host, std::bind(&resolveCallback, host, std::placeholders::_1));
}

int main(int argc, char* argv[]) {
  
  hwnet::ThreadPool pool;
  pool.Init(1);

  if(!g_loop.Init(&pool)) {
    printf("init failed\n");
    return 0;
  } 


  auto resolver = Resolver(&g_loop,argc == 1 ? Resolver::kDNSonly : Resolver::kDNSandHostsFile);
  if (argc == 1)
  {
    total = 3;
    resolve(resolver, "www.baidu.com");
    resolve(resolver, "www.google.com");
    resolve(resolver, "www.csdn.com");
  }
  else
  {
    total = argc-1;
    for (int i = 1; i < argc; ++i)
      resolve(resolver, argv[i]);
  }

  g_loop.Run();  

}
