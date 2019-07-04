#ifndef _BUFFER_H
#define _BUFFER_H

#include <stdint.h>
#include <memory>
#include <string.h>
#include <string>

namespace hwnet {

class TCPSocket;

class Buffer {

public:

	friend class TCPSocket;

	typedef std::shared_ptr<Buffer> Ptr;


private:

	class bytes {
	public:
		typedef std::shared_ptr<bytes> Ptr;
		char *ptr;
		explicit bytes(size_t cap) {
			this->ptr = new char[cap];
		}

		bytes(bytes &&) = delete;
		bytes(const bytes&) = delete;
		bytes& operator = (const bytes&) = delete;

		~bytes() {
			delete [] this->ptr;
		}
	};

public:

	static Buffer::Ptr New(size_t cap,size_t len = 0) {
		Buffer *buff_ = new Buffer(cap,len);
		if(buff_) {
			return Ptr(buff_);
		} else {
			return nullptr;
		}		
	}

	static Buffer::Ptr New(const char *str, size_t len) {
		if(!str) {
			return nullptr;
		}
		auto buff = New(len);
		if(buff){
			buff->Append(const_cast<char*>(str),len);
			return buff;
		} else {
			return nullptr;
		}
	}

	static Buffer::Ptr New(const std::string &str) {
		return New(str.c_str(),str.size());
	}

	//类似go的[n:m]操作
	static Buffer::Ptr New(const Buffer::Ptr &o,size_t s,size_t e) {

		if(s >= o->len) return nullptr;
		if(s >= e) return nullptr;
		if(e > o->len) return nullptr;

		Buffer *buff_ = new Buffer;
		buff_->buff = o->buff;
		buff_->b = s;
		buff_->cap = o->cap - s;
		buff_->len = e - s;
		return Ptr(buff_);
	}

	std::string ToString() {
		return std::string(this->buff->ptr + this->b,this->len);
	}

	void Copy(size_t s,const char *ptr,size_t l) {
		if(!ptr || l == 0) {
			return;
		}

		if(s > this->len) {
			return;
		}

		size_t newcap = s + l;
		if(newcap < this->len) {
			//overflow
			return;
		}

		if(newcap != this->cap) {
			//扩容
			bytes::Ptr newbuff = bytes::Ptr(new bytes(newcap));
			if(s > 0){
				memcpy(newbuff->ptr,this->buff->ptr + this->b,s);
			}
			this->cap = newcap;
			this->buff = newbuff;
			this->b = 0;			
		}

		memcpy(this->buff->ptr + this->b + s , ptr , l);
		this->len = s + l;			

	}

	void Append(const std::string &s) {
		Append(s.c_str(),s.size());
	}

	void Append(const Buffer::Ptr &o) {
		Append(o->BuffPtr(),o->Len());
	}

	void Append(const char *ptr,size_t l) {
		if(ptr && l > 0) {
			auto need = this->len + l;
			if(need < this->len) {
				//overflow
				return;
			}

			if(need > this->cap) {
				//扩容
				size_t cap_ = need;
				bytes::Ptr newbuff = bytes::Ptr(new bytes(cap_)); 
				memcpy(newbuff->ptr,this->buff->ptr + this->b,this->len);
				this->cap = cap_;
				this->buff = newbuff;
				this->b = 0;
			}
			memcpy(this->buff->ptr + this->len + this->b , ptr , l);
			this->len += l;			
		}
	}	

	size_t Cap() const {
		return this->cap;
	}

	size_t Len() const {
		return this->len;
	}

	char* BuffPtr() const {
		return this->buff->ptr + this->b;
	}

	~Buffer() {
		this->buff = nullptr;
	}

private:

	Buffer(size_t cap_,size_t l):b(0),len(0){
		if(cap_ < 16) {
			cap_ = 16;
		}
		
		if(l > cap_) {
			l = cap_;
		}

		this->buff = bytes::Ptr(new bytes(cap_));
		this->cap  = cap_;
		this->len  = l;
	}

	Buffer():b(0),len(0),cap(0){}

	Buffer(const Buffer&) = delete;
	Buffer& operator = (const Buffer&) = delete;	

	bytes::Ptr       buff;
	size_t           b;
	size_t 			 len;
	size_t 			 cap;
};

}

#endif