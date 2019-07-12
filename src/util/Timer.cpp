#include "util/Timer.h"

namespace hwnet { namespace util {

using namespace std::chrono_literals;


void Timer::operator() ()
{
    mCallback();   
}

int Timer::cancel() {

	uint expected1 = 0;
	uint expected2 = Timer::incallback;
	uint setv1 = Timer::canceled;
	uint setv2 = Timer::incallback | Timer::canceled;
	if(mStatus.compare_exchange_strong(expected1,setv1) || mStatus.compare_exchange_strong(expected2,setv2)) {
		if(mStatus.load() & Timer::incallback) {
			if(this->tid == std::this_thread::get_id()) {
				//在回调函数调用栈内,定时器不会再被执行
				return Timer::doing_callback_in_current_thread;
			} else {
				//定时器正在当前线程不同的线程中执行回调，定时器不会再被执行
				return Timer::doing_callback_in_other_thread;
			}
		} else {
			auto s = shared_from_this();
			mMgr->remove(s);
			return Timer::cancel_ok;
		}
	} else {
		return Timer::invaild_timer;
	} 
}

void TimerMgr::Schedule(const milliseconds &now) {
	this->mtx.lock();
    while (this->elements_size > 0) {
        auto tmp = top();
        if (tmp->mExpiredTime > now){
            break;
        }

       	tmp->tid = std::this_thread::get_id(); 
		uint expected = 0;
		uint setv = Timer::incallback;
		auto ok = tmp->mStatus.compare_exchange_strong(expected,setv);
        pop();

        this->mtx.unlock();
		
		if(ok) {
			(*tmp)();
		}

        this->mtx.lock();

        if(ok && !tmp->mOnce) {
        	expected = Timer::incallback;
        	setv = 0;
        	if(tmp->mStatus.compare_exchange_strong(expected,setv)) {
				if(this->policy == TimerMgr::normal) {
					tmp->mExpiredTime = now + tmp->mTimeout;
				} else {
	        		tmp->mExpiredTime += tmp->mTimeout;
	        	}
	        	this->insert(tmp);
	        	tmp->tid = std::thread::id();       
	        	continue; 		
        	}
        }
        tmp->mCallback = nullptr;
    }
    this->mtx.unlock();
}

void TimerRoutine::wait(milliseconds now) {
	std::lock_guard<std::mutex> guard(this->mgr.mtx);
	if(this->mgr.elements_size == 0) {
		this->waitting = true;
		this->waitTime = 0xFFFFFFFF;
		this->cv.wait_for(this->mgr.mtx,this->waitTime * 1ms);
	} else {
		auto top = this->mgr.top();
		if(top->mExpiredTime > now) {
			this->waitting = true;
			this->waitTime = top->mExpiredTime - now;
			this->cv.wait_for(this->mgr.mtx,this->waitTime * 1ms);			
		}
	}
	this->waitting = false;
}

void TimerRoutine::threadFunc(TimerRoutine *self) {
	while(!self->stoped) {
		self->wait(self->getMilliseconds());
		self->mgr.Schedule(self->getMilliseconds());
	}
}

void TimerRoutine::Stop() {
	this->mgr.mtx.lock();
	if(this->stoped){
		this->mgr.mtx.unlock();
		return;
	}
	this->stoped = true;
	if(this->waitting) {
		this->cv.notify_one();
	}
	this->mgr.mtx.unlock();
	this->thd.join();
}


}}	