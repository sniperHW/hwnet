#include "SocketHelper.h"
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>

namespace hwnet {

int SetNoBlock(int fd,bool nonBlock) {
	int flags;
    if((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    	return -1;
    }

    if(nonBlock){
    	flags |= O_NONBLOCK;
    } else {
    	flags ^= O_NONBLOCK;
    }

    return fcntl(fd, F_SETFL, flags);
}

int SetCloseOnExec(int fd) {
	int flags;
    if((flags = fcntl(fd, F_GETFD, 0)) == -1) {
    	return -1;
    }	
    return fcntl(fd, F_SETFD, flags|FD_CLOEXEC);
}

int ReuseAddr(int fd) {
	int yes = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}


}