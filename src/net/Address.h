#ifndef _ADDRESS_H
#define _ADDRESS_H

#include	<netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include	<arpa/inet.h>	/* inet(3) functions */
#include	<sys/un.h>		/* for Unix domain sockets */

namespace hwnet {

struct Addr {

public:

	enum {
		SOCK_ADDR_EMPTY = 0,
		SOCK_ADDR_IPV4,
		SOCK_ADDR_IPV6,
		SOCK_ADDR_UN,
	};

    union {
        struct sockaddr_in  in;   //for ipv4 
        struct sockaddr_in6 in6;  //for ipv6
        struct sockaddr_un  un;   //for unix domain
    } sockaddr;

    static Addr MakeIP4Addr(const char *ip,int port) {
    	Addr addr;
    	memset(&addr.sockaddr,0,sizeof(addr.sockaddr));
    	addr.sockaddr.in.sin_family = AF_INET;
    	addr.sockaddr.in.sin_port = htons(port);
    	if(inet_pton(AF_INET,ip,&addr.sockaddr.in.sin_addr) < 0) {
    		addr.addrType = SOCK_ADDR_EMPTY;
    	} else {
    		addr.addrType = SOCK_ADDR_IPV4;
    	}
    	return addr;
    }

    struct sockaddr* Address() const {
    	return (struct sockaddr*)&this->sockaddr;
    }

    size_t AddrLen() const {
		if(this->addrType == SOCK_ADDR_IPV4) {
    		return sizeof(this->sockaddr.in);
    	} else if(this->addrType == SOCK_ADDR_IPV6) {
    		return sizeof(this->sockaddr.in6);
    	} else if(this->addrType == SOCK_ADDR_UN) {
    		return sizeof(this->sockaddr.un);
    	} else {
    		return 0;
    	}
    }


    bool IsVaild() const {
    	return this->addrType >= SOCK_ADDR_IPV4 && this->addrType <= SOCK_ADDR_UN;
    }

private:
	Addr(){}    
    int addrType;

};

}


#endif