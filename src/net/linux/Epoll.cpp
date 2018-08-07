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

void Epoll::Add(const Channel::Ptr &channel) {
	epoll_event ev = {0};
	ev.data.ptr = channel.get();
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLOUT | EPOLLET,
	epoll_ctl(this->epfd,EPOLL_CTL_ADD,channel->Fd(),&ev);
}

void Epoll::Remove(const Channel::Ptr &channel) {
	epoll_event ev = {0};
	epoll_ctl(epfd,EPOLL_CTL_DEL,channel->Fd(),&ev);
}

void Epoll::Run() {
	epoll_event *tmp;
	for( ; ; ) {
		auto nfds = TEMP_FAILURE_RETRY(epoll_wait(this->epfd,this->events,this->maxevents,-1));
		if(nfds > 0) {
			this->poller_->processBegin();
			for(auto i=0; i < nfds ; ++i) {
				epoll_event *event = &events[i];
				((Channel*)event->data.ptr)->OnActive(event->events);
			}
			this->poller_->processFinish();
			if(nfds == this->maxevents){
				this->maxevents <<= 2;
				tmp = (epoll_event*)realloc(this->events,sizeof(*this->events)*this->maxevents);
				if(nullptr == tmp) {
					//log error
					return;
				}
				this->events = tmp;
			}			
		} else {
			//log error
			return;
		}
	}
}

}

#endif