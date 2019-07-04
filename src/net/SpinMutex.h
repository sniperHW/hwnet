#ifndef _SPIN_MUTEX_H
#define _SPIN_MUTEX_H 

namespace hwnet {

class spin_mutex {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
  spin_mutex() = default;
  spin_mutex(const spin_mutex&) = delete;
  spin_mutex& operator= (const spin_mutex&) = delete;
  void lock() {
    while(flag.test_and_set(std::memory_order_acquire))
      ;
  }
  void unlock() {
    flag.clear(std::memory_order_release);
  }
};

}


#endif