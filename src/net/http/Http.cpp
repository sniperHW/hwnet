#include "Http.h"
#include <iostream>

namespace hwnet { namespace http {

void HttpPacket::MakeHeader(const int type,std::string &out) {
	if(type == HttpPacket::request) {
		out.append(method_strings[this->method]).append(" ").append(this->url).append(" ");
		char buff[128];
		snprintf(buff,128,"%u.%u",this->http_major,this->http_minor);
		out.append("HTTP/").append(buff).append("\r\n");		
	} else {
		char buff[128];
		snprintf(buff,128,"%u.%u",this->http_major,this->http_minor);
		out.append("HTTP/").append(buff).append(" ");
		snprintf(buff,128,"%u",this->status_code);
		out.append(buff).append(" ").append(status).append("\r\n");
	}

	auto it = this->headers.begin();
	auto end = this->headers.end();
	for(; it != end; ++it) {
		out.append(it->first).append(":").append(it->second).append("\r\n");
	}
	out.append("\r\n");
}

void HttpResponse::WriteHeader(const std::function<void ()> &writeOk) {
	std::string header;
	this->packet->MakeHeader(HttpPacket::response,header);
	if(writeOk){
		this->session->s->SetFlushCallback([writeOk](TCPSocket::Ptr &_){
			(void)_;
			writeOk();
		});
	}
	this->session->Send(header);
}

void HttpResponse::WriteBody(const std::string &body,const std::function<void ()> &writeOk) {
	if(writeOk){
		this->session->s->SetFlushCallback([writeOk](TCPSocket::Ptr &_){
			(void)_;
			writeOk();
		});
	}
	this->session->Send(body);	
}


void HttpRequest::WriteHeader(const std::function<void ()> &writeOk) {
	std::string header;
	this->packet->MakeHeader(HttpPacket::request,header);
	if(writeOk){
		this->session->s->SetFlushCallback([writeOk](TCPSocket::Ptr &_){
			(void)_;	
			writeOk();
		});
	}
	this->session->Send(header);
}

void HttpRequest::WriteBody(const std::string &body,const std::function<void ()> &writeOk) {
	if(writeOk){
		this->session->s->SetFlushCallback([writeOk](TCPSocket::Ptr &_){
			(void)_;
			writeOk();
		});
	}
	this->session->Send(body);	
}


void HttpSession::Recv() {
	this->s->Recv(this->recvbuff);
}

void HttpSession::Close() {
	this->s->Close();
}

void HttpSession::Send(const std::string &str,bool closedOnFlush){
	this->s->Send(str,closedOnFlush);
}

void HttpSession::onData(TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n) {
	auto session = any_cast<HttpSession*>(ss->GetUserData());
	auto ptr = buff->BuffPtr();
	std::cout << ptr << std::endl;
	auto nparsed = http_parser_execute(&session->parser,&session->settings,ptr,n);
	if(nparsed != n) {
		onError(ss,0);
	}
	session->Recv();
}	

void HttpSession::onClose(TCPSocket::Ptr &ss) {
	auto session = any_cast<HttpSession*>(ss->GetUserData());
	session->onRequest = nullptr;
}

void HttpSession::onError(TCPSocket::Ptr &ss,int err) {
	ss->Close();
}


int HttpSession::on_message_begin(http_parser *parser) {
	auto session = (HttpSession*)parser->data;
	session->packet = std::shared_ptr<HttpPacket>(new HttpPacket);
	return 0;
}

int HttpSession::on_url(http_parser *parser, const char *at, size_t length) {
	auto session = (HttpSession*)parser->data;
	session->packet->url.append(at,length);
	return 0;
}

int HttpSession::on_status(http_parser *parser, const char *at, size_t length) {
	auto session = (HttpSession*)parser->data;
	session->packet->status.append(at,length);
	session->packet->status_code = parser->status_code;
	return 0;
}

int HttpSession::on_header_field(http_parser *parser, const char *at, size_t length) {
	auto session = (HttpSession*)parser->data;
	if(!session->packet->field.empty()) {
		session->packet->headers[session->packet->field] = session->packet->value;
		session->packet->field.clear();
		session->packet->value.clear();
	}
	session->packet->field.append(at,length);
	return 0;
}

int HttpSession::on_header_value(http_parser *parser, const char *at, size_t length) {
	auto session = (HttpSession*)parser->data;
	session->packet->value.append(at,length);
	return 0;
}

int HttpSession::on_headers_complete(http_parser *parser) {
	auto session = (HttpSession*)parser->data;
	if(!session->packet->field.empty()) {
		session->packet->headers[session->packet->field] = session->packet->value;
		session->packet->field.clear();
		session->packet->value.clear();
	}

	session->packet->http_major = parser->http_major;
	session->packet->http_minor = parser->http_minor;

	if(session->side == ServerSide) {
		auto req = HttpRequest::Ptr(new HttpRequest(nullptr,session->packet));
		auto resp = HttpResponse::Ptr(new HttpResponse(session->my_shared_from_this(),std::shared_ptr<HttpPacket>(new HttpPacket)));
		session->onRequest(req,resp);
	} else {
		auto resp = HttpResponse::Ptr(new HttpResponse(session->my_shared_from_this(),session->packet));
		session->onResponse(resp);		
	}
	
	return 0;
}

int HttpSession::on_body(http_parser *parser, const char *at, size_t length) {
	auto session = (HttpSession*)parser->data;
	if(session->packet->onBody) {
		session->packet->onBody(at,length);
	}
	return 0;
}

int HttpSession::on_message_complete(http_parser *parser) {
	auto session = (HttpSession*)parser->data;
	if(session->packet->onBody) {
		session->packet->onBody(nullptr,0);
	}
	session->packet = nullptr;
	return 0;
}


}}