#include    <iostream>
#include	<stdio.h>
#include    <stdint.h>
#include    <atomic>
#include    <chrono>
#include    "net/Buffer.h"
#include 	<time.h>
#include 	<unistd.h>

using namespace hwnet;


int main() {

	Buffer::Ptr b1 = Buffer::New("1234567890123456");

	std::cout << b1->Cap() << "," << b1->Len() << std::endl;

	Buffer::Ptr b2 = Buffer::New(b1,10,b1->Len());

	std::cout << b2->Cap() << "," << b2->Len() << "," << b2->ToString() << std::endl;

	b1->Copy(10,"hhhhhh",6);

	//b1,b2共享底层bytes所以b1的改变导致b2也改变

	std::cout << b1->ToString() << std::endl;

	std::cout << b2->ToString() << std::endl;

	//b1扩容,重新分配底层bytes,b1与b2分别指向不同的bytes,所以b2并未改变

	b1->Copy(0,"aaaaaaaaaaaaaaaaaaaa",20);

	std::cout << b1->ToString() << std::endl;

	std::cout << b2->ToString() << std::endl;

	return 0;
}