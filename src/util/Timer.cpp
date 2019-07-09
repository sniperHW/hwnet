#include "util/Timer.h"

namespace hwnet { namespace util {


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
		if(mStatus.load() | Timer::incallback) {
			if(this->tid == std::this_thread::get_id()) {
				//在回调函数调用栈内,定时器不会再被执行
				return Timer::doing_callback_in_current_thread;
			} else {
				//定时器正在当前线程不同的线程中执行回调，定时器不会再被执行
				return Timer::doing_callback_in_other_thread;
			}
		} else {
			auto s = shared_from_this();
			if(!mMgr || mIndex == 0 || !mMgr->remove(s)) {
				return Timer::invaild_timer;
			} else {
				//定时器尚未开始执行,取消成功
				return Timer::cancel_ok;
			}
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
        pop();
        this->mtx.unlock();

		uint expected = 0;
		uint setv = Timer::incallback;
		if(tmp->mStatus.compare_exchange_strong(expected,setv)) {
			tmp->tid = std::this_thread::get_id(); 
			(*tmp)();
			tmp->tid = std::thread::id(); 
			if(!tmp->mOnce && !(tmp->mStatus.load() & Timer::canceled)) {
				tmp->mStatus.store(tmp->mStatus ^ Timer::incallback);
				if(this->policy == TimerMgr::normal) {
					tmp->mExpiredTime = now + tmp->mTimeout;
				} else {
	        		tmp->mExpiredTime += tmp->mTimeout;
	        	}
	        	this->insert(tmp);
			} else {
				tmp->mStatus.store((tmp->mStatus ^ (Timer::incallback)) | Timer::canceled);
			}
		}
        this->mtx.lock();
    }
    this->mtx.unlock();
}


}}	