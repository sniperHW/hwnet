#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include 	<time.h>
#include 	<unistd.h>
#include    <signal.h>


#include    "util/Timer.h"


void testTimerRoutine() {
	hwnet::util::TimerRoutine routine(hwnet::util::TimerMgr::normal);


	routine.addTimer(100,[](hwnet::util::Timer::Ptr t){
		std::cout << "timer 100" << std::endl;
	});

	routine.addTimer(200,[](hwnet::util::Timer::Ptr t){
		std::cout << "timer 200" << std::endl;
	});	

	std::this_thread::sleep_for(std::chrono::seconds(1));

}

int main() {


	{
		hwnet::util::TimerMgr mgr(hwnet::util::TimerMgr::normal);

		mgr.addTimer(0,10,[](hwnet::util::Timer::Ptr t){
			std::cout << "timer 10" << std::endl;
		});

		mgr.addTimer(0,20,[](hwnet::util::Timer::Ptr t){
			std::cout << "timer 20" << std::endl;
		});

		mgr.addTimer(0,5,[](hwnet::util::Timer::Ptr t){
			std::cout << "timer 5" << std::endl;
			std::cout << "timer 5 cancel ret:" << t->cancel() << std::endl;
		});

		mgr.Schedule(20);
		std::cout << "schedule(20)" << std::endl;

		mgr.Schedule(40);
		std::cout << "schedule(40)" << std::endl;

	}


	{

		/*
		 *  普通模式 重新注册定时器时expired = now + timeout 
         *  所以20时间时定时器只执行一次
         */
		
		hwnet::util::TimerMgr mgr(hwnet::util::TimerMgr::normal);

		mgr.addTimer(0,10,[](hwnet::util::Timer::Ptr t){
			std::cout << "timer 10" << std::endl;
		});

		mgr.Schedule(20);
		std::cout << "schedule(20)" << std::endl;

	}


	{

		/*
		 *  补偿模式 重新注册定时器时expired = expired + timeout 
         *  所以20时间时定时器只执行2次(第一次的expired=10,执行完后重新注册expired=10+10=20)
         */


		hwnet::util::TimerMgr mgr(hwnet::util::TimerMgr::compensate);

		mgr.addTimer(0,10,[](hwnet::util::Timer::Ptr t){
			std::cout << "timer 10" << std::endl;
		});

		mgr.Schedule(20);
		std::cout << "schedule(20)" << std::endl;

	}


	testTimerRoutine();


	return 0;
}