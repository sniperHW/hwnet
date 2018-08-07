CFLAGS  = -g -Wall -fno-strict-aliasing -std=c++11
LDFLAGS = -lpthread
DEPDIR  = 
INCLUDE = -I./src
MAKE    =
CC      =

source.lib = src/net/TCPSocket.cpp\
			 src/net/ThreadPool.cpp\
			 src/net/Poller.cpp\

# Platform-specific overrides
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Linux)
	MAKE += make
	CC += g++
	DEFINE += -D_LINUX
	LDFLAGS += -lrt -ldl
	SHAREFLAGS += -shared
	source.lib += src/net/linux/Epoll.cpp
endif

ifeq ($(uname_S),Darwin)
	MAKE += make
	CC += clang
	DEFINE += -D_MACOS
	SHAREFLAGS += -bundle -undefined dynamic_lookup	
	source.lib += src/net/mac/Kqueue.cpp	
endif


all:
	g++ -c $(CFLAGS) $(DEFINE) $(source.lib) $(INCLUDE)
	ar -rc net.lib *.o
	rm *.o	
	g++ -o benmark test/benmark.cpp net.lib $(CFLAGS) $(DEFINE) $(LDFLAGS) $(INCLUDE)	