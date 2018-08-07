#ifdef _MACOS

#include "net/Poller.h"
#include "Kqueue.h"
#include <unistd.h>
#include "net/temp_failure_retry.h"

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

	maxevents = 64;
	events = (struct kevent*)calloc(1,(sizeof(*this->events)*this->maxevents));
	if(!events) {
		return false;
	}

	return true;	
}

void Kqueue::Add(const Channel::Ptr &channel) {
	struct kevent ke;
	EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, channel.get());
	kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
	EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, channel.get());
	kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
}

void Kqueue::Remove(const Channel::Ptr &channel) {
	struct kevent ke;
	EV_SET(&ke, channel->Fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);
	EV_SET(&ke, channel->Fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
	kevent(this->kfd, &ke, 1, nullptr, 0, nullptr);	
}


void Kqueue::Run() {
	struct kevent *tmp;
	for( ; ; ) {	
		auto nfds = TEMP_FAILURE_RETRY(kevent(this->kfd, nullptr, 0, this->events,this->maxevents,nullptr));
		if(nfds > 0) {
			this->poller_->processBegin();
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
			this->poller_->processFinish();			
			if(nfds == this->maxevents){
				this->maxevents <<= 2;
				tmp = (struct kevent*)realloc(this->events,sizeof(*this->events)*this->maxevents);
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

