#ifndef _CHANNEL_H
#define _CHANNEL_H

#include <memory>

namespace hwnet {

class Channel {

#ifdef _LINUX
friend class Epoll;
#endif

public:
	typedef std::shared_ptr<Channel> Ptr;
	
	virtual void OnActive(int event) = 0;
	
	virtual int  Fd() const = 0;
	
	virtual ~Channel() {}

#ifdef _LINUX
private:
	int events;
#endif

};

}

#endif