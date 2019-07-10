#ifdef _LINUX

#include "net/Poller.h"
#include "Epoll.h"
#include <unistd.h>
#include "net/temp_failure_retry.h"

namespace hwnet {

Epoll::~Epoll() {
	if(epfd >= 0) {
		::close(epfd);
	}

	if(events) {
		free(events);
	}
}

bool Epoll::Init() {

	epfd  = epoll_create1(EPOLL_CLOEXEC);
	if(epfd < 0) {
		return false;
	}

	maxevents = 64;
	events = (epoll_event*)calloc(1,(sizeof(*this->events)*this->maxevents));
	if(!events) {
		return false;
	}	

	return true;
}

int Epoll::Add(const Channel::Ptr &channel,int flag) {
	epoll_event ev = {0};
	ev.data.ptr = channel.get();

	if(flag & Poller::Read) {
		ev.events |= EPOLLIN;
	}

	if(flag & Poller::Write) {
		ev.events |= EPOLLOUT;
	}

	int et = 0;
	if(flag & Poller::ET) {
		et = EPOLLET;
	}

	ev.events |= EPOLLERR | EPOLLHUP | EPOLLRDHUP | et;

	epoll_ctl(this->epfd,EPOLL_CTL_ADD,channel->Fd(),&ev);

	return ev.events;
}

void Epoll::Remove(const Channel::Ptr &channel) {
	epoll_event ev = {0};
	epoll_ctl(epfd,EPOLL_CTL_DEL,channel->Fd(),&ev);
}

int Epoll::Enable(const Channel::Ptr &channel,int flag,int oldEvents) {

	epoll_event ev = {0};
	ev.data.ptr = channel.get();
	ev.events = oldEvents;
		
	if(flag & Poller::Read) {
		ev.events |= EPOLLIN;
	}

	if(flag & Poller::Write) {
		ev.events |= EPOLLOUT;
	}

	epoll_ctl(this->epfd,EPOLL_CTL_MOD,channel->Fd(),&ev);

	return ev.events;

}

int Epoll::Disable(const Channel::Ptr &channel,int flag,int oldEvents) {

	epoll_event ev = {0};
	ev.data.ptr = channel.get();
	ev.events = oldEvents;
		
	if(flag & Poller::Read) {
		ev.events &= ~EPOLLIN;
	}

	if(flag & Poller::Write) {
		ev.events &= ~EPOLLOUT;
	}

	epoll_ctl(this->epfd,EPOLL_CTL_MOD,channel->Fd(),&ev);

	return ev.events;
}	

int Epoll::RunOnce() {
	epoll_event *tmp;
	auto nfds = TEMP_FAILURE_RETRY(epoll_wait(this->epfd,this->events,this->maxevents,-1));
	if(nfds > 0) {
		for(auto i=0; i < nfds ; ++i) {
			epoll_event *event = &events[i];
			((Channel*)event->data.ptr)->OnActive(event->events);
		}	
		if(nfds == this->maxevents){
			this->maxevents <<= 2;
			tmp = (epoll_event*)realloc(this->events,sizeof(*this->events)*this->maxevents);
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
