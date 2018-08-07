#ifndef _BUFFER_H
#define _BUFFER_H

#include <stdint.h>
#include <memory>
#include <sys/uio.h>
#include <string.h>
#include <string>


namespace hwnet {

class Buffer {

public:

	typedef std::shared_ptr<Buffer> Ptr;
	
public:

	static Buffer::Ptr New(size_t cap) {
		Buffer *buff_ = new Buffer(cap);
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

	std::string ToString() {
		return std::string(this->buff,this->len);
	}

	void Append(char *ptr,size_t len) {
		if(ptr && len > 0) {
			auto need = this->len + len;
			if(need < this->len) {
				//overflow
				return;
			}

			if(need > cap) {
				char *newbuff = (char*)realloc((void*)this->buff,need);

				if(!newbuff) {
					return;
				}

				if(this->buff != newbuff){
					this->buff = newbuff;
				}
				this->cap = need;
			}

			memcpy(this->buff+this->len,ptr,len);
			this->len += len;
		}
	}	

	size_t Cap() const {
		return this->cap;
	}

	size_t Len() const {
		return this->len;
	}

	char* BuffPtr() const {
		return this->buff;
	}

	//internal user only
	void PrepareRecv(iovec &vec) {
		vec.iov_len  = this->cap;
		vec.iov_base = this->buff;
	}

	void RecvFinish(size_t len) {
		this->len = len;
	}

	~Buffer() {
		if(this->buff) {
			free(this->buff);
			this->buff = nullptr;
		}
	}

private:

	Buffer(size_t cap):len(0){
		if(cap < 64) {
			cap = 64;
		}
		this->buff = (char*)malloc(cap);
		this->cap  = cap;
	}

	Buffer(const Buffer&);
	Buffer& operator = (const Buffer&); 	

private:
	char  			*buff;
	size_t 			 len;
	size_t 			 cap;
};

}

#endif