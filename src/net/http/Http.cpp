#include "Http.h"
#include <iostream>

namespace hwnet { namespace http {

void HttpResponse::Send(std::shared_ptr<HttpSession> &session,bool closedOnFlush) const {
	session->Send(resp);
	char content_length[128];
	snprintf(content_length,128,"Content-Length:%lu\r\n\r\n",body.size());

	if(!body.empty()){
		session->Send(std::string(content_length));
		session->Send(body,closedOnFlush);
	} else {
		session->Send(std::string(content_length),closedOnFlush);
	}
}

void HttpRequest::SendResp(const HttpResponse &resp,bool closedOnFlush) {
	resp.Send(this->session,closedOnFlush);
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
	auto req = HttpRequest::Ptr(new HttpRequest(session->my_shared_from_this(),session->packet));
	session->onRequest(req);
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