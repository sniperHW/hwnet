#ifdef _MACOS

#include "net/Poller.h"
#include "net/SocketHelper.h"
#include "Kqueue.h"
#include <unistd.h>
#include "net/temp_failure_retry.h"
#include <string.h>

namespace hwnet {

Kqueue::~Kqueue() {
	if(kfd >= 0) {
		::close(kfd);
	}

	if(events) {
		free(events);
	}
}

bool Kqueue::Init() {

	kfd = kqueue();
	if(kfd < 0) {
		return false;
	}

	SetCloseOnExec(kfd);

	maxevents = 64;
	events = (struct kevent*)calloc(1,(sizeof(*this->events)*this->maxevents));
	if(!events) {
		return false;
	}

	return true;	
}

int Kqueue::Add(const Channel::Ptr &channel,int flag) {
	struct kevent ke = {0};

	int et = 0;
	if(flag & Poller::ET){
		et = EV_CLEAR;
	}

	//int disableFlag = 0;
	//if(flag & Poller::DISABLE) {
	//	disableFlag = EV_DISABLE;
	//}

	int events = 0;

	if(flag & Poller::Read) {
		EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_ADD | et /*| disableFlag*/, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		//if(disableFlag==0) {
		events |= Poller::Read;
		//}
	}

	if(flag & Poller::Write) {
		EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_ADD | et /*| disableFlag*/, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		//if(disableFlag==0) {
		events |= Poller::Write;
		//}
	}

	return events;
}

void Kqueue::Remove(const Channel::Ptr &channel) {
	
	/*
	不能一次移除两个事件，有的channel没有同时注册读写
	struct kevent ke[2] = {{0},{0}};
	::memset(&ke,0,sizeof(ke));
	EV_SET(&ke[0], channel->Fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	EV_SET(&ke[1], channel->Fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, ke, 2, nullptr, 0, nullptr);	
	*/	
	struct kevent keR = {0};
	struct kevent keW = {0};
	EV_SET(&keR, channel->Fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, &keR, 1, nullptr, 0, nullptr);
	EV_SET(&keW, channel->Fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, &keW, 1, nullptr, 0, nullptr);	
}

int Kqueue::Enable(const Channel::Ptr &channel,int flag,int oldEvents) {
		
	int events = oldEvents;	

	if(flag & Poller::Read) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_ENABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		events |= Poller::Read;
	}

	if(flag & Poller::Write) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		events |= Poller::Write;		
	}

	return events;
}

int Kqueue::Disable(const Channel::Ptr &channel,int flag,int oldEvents) {

	int events = oldEvents;

	if(flag & Poller::Read) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_DISABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		events &= ~Poller::Read;

	}

	if(flag & Poller::Write) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
		events &= ~Poller::Write;
	}

	return events;
}


int Kqueue::RunOnce() {
	struct kevent *tmp;
	auto nfds = TEMP_FAILURE_RETRY(kevent(this->kfd, nullptr, 0, this->events,this->maxevents,nullptr));
	if(nfds > 0) {
		for(auto i=0; i < nfds ; ++i) {
			struct kevent *event = &this->events[i];
			auto ev = 0;
			if(event->filter == EVFILT_READ){
				ev |= readFlag;
				if(event->flags & EV_EOF) {
					ev |= errorFlag;
				}
			}
			if(event->filter == EVFILT_WRITE){
				ev |= writeFlag;				
			}
			((Channel*)event->udata)->OnActive(ev);			
		}		
		if(nfds == this->maxevents){
			this->maxevents <<= 2;
			tmp = (struct kevent*)realloc(this->events,sizeof(*this->events)*this->maxevents);
			if(nullptr == tmp) {
				//log error
				return -1;
			}
			this->events = tmp;
		}
		return 0;		
	} else {
		//log error
		return -1;
	}
}

}

#endif

