#ifndef _TIMER_H
#define _TIMER_H

#include <functional>
#include <queue>
#include <memory>
#include <vector>
#include <chrono>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace hwnet { namespace util {

typedef size_t milliseconds;


class TimerMgr;

class TimerRoutine;

class Timer final : public std::enable_shared_from_this<Timer> {

public:

	enum {
		cancel_ok = 0,
		doing_callback_in_current_thread = 1,
		doing_callback_in_other_thread = 2,
		invaild_timer = 3,
	};

	enum {
		canceled   = 1 << 1,
		incallback = 1 << 2,
	};

    using Ptr = std::shared_ptr<Timer>;
    using WeakPtr = std::weak_ptr<Timer>;
    using Callback = std::function<void(void)>;

    int cancel();

    /*~Timer(){
    	std::cout << "~Timer" << std::endl;
    }*/

private:


    Timer(const milliseconds &ExpiredTime, const milliseconds &timeout, bool once):
    		mExpiredTime(ExpiredTime),mTimeout(timeout),mOnce(once),mMgr(nullptr),mIndex(0),mStatus(0){
    	}

    milliseconds getLeftTime(const milliseconds &now) const {
    	if(now >= mExpiredTime) {
    		return  milliseconds(0);
    	} else {
    		return mExpiredTime - now;
    	}
    }


	Timer(const TimerMgr&) = delete;
	Timer& operator = (const TimerMgr&) = delete;	

    void operator() ();


    Callback                        mCallback;
    milliseconds                 	mExpiredTime;
    const milliseconds              mTimeout;
    const bool                      mOnce;       //一次性定时器
	TimerMgr 					   *mMgr;
	size_t   						mIndex;
	std::atomic_uint                mStatus;
    std::thread::id 	            tid;
    friend class TimerMgr;
    friend class TimerRoutine;
};

class TimerMgr final {
	friend class Timer;
	friend class TimerRoutine;
public:

	enum {
		normal = 1,
		compensate = 2,
	};

	explicit TimerMgr(int policy):policy(policy),elements_size(0){}

    template<typename F, typename ...TArgs>
    Timer::WeakPtr addTimer(milliseconds now,milliseconds timeout, F&& callback, TArgs&& ...args)
    {
        auto timer = Timer::Ptr(new Timer(now + timeout,timeout,false));
        timer->mCallback = std::bind(std::forward<F>(callback), timer, std::forward<TArgs>(args)...);
        this->insertLock(timer);
        return timer;
    }

    template<typename F, typename ...TArgs>
    Timer::WeakPtr addTimerOnce(milliseconds now,milliseconds timeout, F&& callback, TArgs&& ...args)
    {
        auto timer = Timer::Ptr(new Timer(now + timeout,timeout,true));
        timer->mCallback = std::bind(std::forward<F>(callback), timer, std::forward<TArgs>(args)...);
        this->insertLock(timer);
        return timer;
    }

    void Schedule(const milliseconds &now);

	bool Empty() {
		std::lock_guard<std::mutex> guard(this->mtx);
		return this->elements_size == 0;
	}

	~TimerMgr(){
		for(size_t i = 0; i < this->elements_size;i++) {
			this->elements[i]->mCallback = nullptr;
		}
	}

private:

	TimerMgr(const TimerMgr&) = delete;
	TimerMgr& operator = (const TimerMgr&) = delete;	

	Timer::Ptr empty_element_ptr;

	size_t getNextFreeOffset() {
		return elements_size;
	}

	size_t getOffset(size_t index) {
		return index - 1;
	}

	size_t parent(size_t idx) {
		return idx / 2;
	}

	size_t left(size_t idx) {
		return idx * 2;
	}

	size_t right(size_t idx) {
		return idx*2 + 1;
	}

	void change(Timer::Ptr &e) {
		auto idx = e->mIndex;
		this->down(idx);
		if(idx == e->mIndex){
			this->up(idx);
		}
	}

	void swap(size_t idx1, size_t idx2) {
		if(idx1 != idx2) {
			auto offset1 = getOffset(idx1);
			auto offset2 = getOffset(idx2);
			auto e = this->elements[offset1];
			this->elements[offset1] = this->elements[offset2];
			this->elements[offset2] = e;
			this->elements[offset1]->mIndex = idx1;
			this->elements[offset2]->mIndex = idx2;
		}
	}

	void down(size_t idx) {
		auto l = left(idx);
		auto r = right(idx);
		auto min = idx;

		auto offsetIdx = getOffset(idx);
		
		if(l <= this->elements_size) {
			auto offsetL = getOffset(l);
			if(this->elements[offsetL]->mExpiredTime < this->elements[offsetIdx]->mExpiredTime) {
				min = l;
			}
		}

		if(r <= this->elements_size) {
			auto offsetR = getOffset(r);
			auto offsetMin = getOffset(min);
			if(this->elements[offsetR]->mExpiredTime < this->elements[offsetMin]->mExpiredTime) {
				min = r;
			}
		}

		if(min != idx) {
			this->swap(idx,min);
			this->down(min);
		}	
	}

	void up(size_t idx) {
		auto p = parent(idx);
		while(p > 0) {
			auto offsetIdx = getOffset(idx);
			auto offsetP = getOffset(p);
			if(this->elements[offsetIdx]->mExpiredTime < this->elements[offsetP]->mExpiredTime) {
				this->swap(idx,p);
				idx = p;
				p = parent(idx);
			} else {
				break;
			}
		}
	}

	Timer::Ptr &top() {
		if(this->elements_size > 0) {
			return this->elements[0];
		} else {
			return empty_element_ptr;
		}
	}

	void pop() {
		if(this->elements_size > 0) {
			auto e = this->elements[0];
			this->swap(1,this->elements_size);
			e->mIndex = 0;
			e->mMgr  = nullptr;			
			this->elements[this->elements_size-1] = nullptr;
			--this->elements_size;
			this->down(1);
		}
	}

	bool insertLock(Timer::Ptr &e) {
		std::lock_guard<std::mutex> guard(this->mtx);
		return insert(e);
	}	

	bool insert(Timer::Ptr &e) {

		if(e->mMgr != nullptr && e->mMgr != this) {
			return false;
		}

		if(e->mIndex > 0) {
			if(e->mIndex > this->elements_size) {
				return false;
			}
			auto offsetIdx = getOffset(e->mIndex);
			if(this->elements[offsetIdx] != e) {
				return false;
			}
			change(e);
			return true;
		} else {
			e->mMgr = this;
			e->mIndex = this->elements_size+1;
			if(this->elements_size == this->elements.size()) {
				this->elements.push_back(e);
			} else {
				auto offset = getNextFreeOffset();
				this->elements[offset] = e;
			}
			++this->elements_size;
			this->up(e->mIndex);
			return true;
		}
	}

	bool remove(Timer::Ptr &e) {
		std::lock_guard<std::mutex> guard(this->mtx);
		if(this == e->mMgr) {
			if(e->mIndex == 0 || e->mIndex > this->elements_size) {
				return false;
			}

			auto offsetIdx = getOffset(e->mIndex);
			if(this->elements[offsetIdx] != e) {
				return false;
			}

			auto tail = this->elements[this->elements_size-1];
			
			if(tail == e) {
				this->elements_size--;
			} else {
				this->swap(e->mIndex,tail->mIndex);
				this->elements_size--;
				this->change(tail);
			}

			this->elements[this->elements_size] = nullptr;
			e->mIndex = 0;
			e->mCallback = nullptr;
			return true;
		} else {
			return false;
		} 
	}

	int        policy;
	std::mutex mtx;
	size_t     elements_size;
	std::vector<Timer::Ptr> elements;

};

class TimerRoutine {

public:

	explicit TimerRoutine(int policy):mgr(policy),thd(threadFunc,this),stoped(false),
		begTime(std::chrono::steady_clock::now()),waitTime(0xFFFFFFFF){
	}

    template<typename F, typename ...TArgs>
    Timer::WeakPtr addTimer(milliseconds timeout, F&& callback, TArgs&& ...args)
    {
        auto timer = Timer::Ptr(new Timer(getMilliseconds() + timeout,timeout,false));
        timer->mCallback = std::bind(std::forward<F>(callback), timer, std::forward<TArgs>(args)...);

        std::lock_guard<std::mutex> guard(this->mgr.mtx);
        this->mgr.insert(timer);
        if(waitting && timeout < waitTime) {
        	waitTime = timeout;
        	this->cv.notify_one();
        }        
        return timer;
    }

    template<typename F, typename ...TArgs>
    Timer::WeakPtr addTimerOnce(milliseconds timeout, F&& callback, TArgs&& ...args)
    {
        auto timer = Timer::Ptr(new Timer(getMilliseconds() + timeout,timeout,true));
        timer->mCallback = std::bind(std::forward<F>(callback), timer, std::forward<TArgs>(args)...);

        std::lock_guard<std::mutex> guard(this->mgr.mtx);
        this->mgr.insert(timer);
        if(waitting && timeout < waitTime) {
        	waitTime = timeout;
        	this->cv.notify_one();
        }

        return timer;
    }

    void Stop();

    ~TimerRoutine() {
    	Stop();
    }


private:

	void wait(milliseconds now);

	milliseconds getMilliseconds() const {
		return (std::chrono::steady_clock::now() - begTime).count()/1000000;
	} 

	TimerRoutine(const TimerRoutine&) = delete;
	TimerRoutine& operator = (const TimerRoutine&) = delete;

	static void threadFunc(TimerRoutine *self);

	TimerMgr mgr;
	std::condition_variable_any cv;
	std::thread thd;
	std::atomic_bool stoped;
	std::chrono::steady_clock::time_point begTime;
	milliseconds waitTime;
	bool waitting;
	
};


}}


#endif