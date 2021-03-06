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

	int et = 0;
	if(flag & Poller::ET){
		et = EV_CLEAR;
	}

	struct kevent ke[2] = {{0},{0}};

	if(flag & Poller::Read) {
		EV_SET(&ke[0], channel->Fd(), EVFILT_READ, EV_ADD | et , 0, 0, channel.get());
	} else {
		EV_SET(&ke[0], channel->Fd(), EVFILT_READ, EV_ADD | et | EV_DISABLE, 0, 0, channel.get());	
	}

	if(flag & Poller::Write) {
		EV_SET(&ke[1], channel->Fd(), EVFILT_WRITE, EV_ADD | et , 0, 0, channel.get());
	} else {
		EV_SET(&ke[1], channel->Fd(), EVFILT_WRITE, EV_ADD | et | EV_DISABLE, 0, 0, channel.get());
	}

	kevent(this->kfd, ke, 2, nullptr, 0, nullptr);	

	return 0;
}

void Kqueue::Remove(const Channel::Ptr &channel) {
	struct kevent ke[2] = {{0},{0}};
	::memset(&ke,0,sizeof(ke));
	EV_SET(&ke[0], channel->Fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	EV_SET(&ke[1], channel->Fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, ke, 2, nullptr, 0, nullptr);	
}

void Kqueue::Enable(const Channel::Ptr &channel,int flag) {
		
	if(flag & Poller::Read) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_ENABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
	}

	if(flag & Poller::Write) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);		
	}
}

void Kqueue::Disable(const Channel::Ptr &channel,int flag) {

	if(flag & Poller::Read) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_DISABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
	}

	if(flag & Poller::Write) {
		struct kevent ke = {0};
		EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, channel.get());
		kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
	}
}


int Kqueue::RunOnce() {
	struct kevent *tmp;
	auto nfds = TEMP_FAILURE_RETRY(kevent(this->kfd, nullptr, 0, this->events,this->maxevents,nullptr));
	if(nfds > 0) {
		for(auto i=0; i < nfds ; ++i) {
			struct kevent *event = &this->events[i];
			auto ev = 0;
			if(event->flags & EV_EOF || event->flags & EV_ERROR) {
				ev |= errorFlag;
			}
			if(event->filter == EVFILT_READ){
				ev |= readFlag;
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

