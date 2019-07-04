#ifndef _SOCKETHELPER_H
#define _SOCKETHELPER_H


namespace hwnet {

int SetNoBlock(int fd,bool nonBlock);


int ReuseAddr(int fd);

int SetCloseOnExec(int fd);


}

#endif