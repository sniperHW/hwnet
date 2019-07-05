#ifndef _HTTP_H
#define _HTTP_H

#include "net/TCPSocket.h"
#include "net/http/http_parser.h"
#include <map>


namespace hwnet { namespace http {

struct HttpPacket {
	typedef std::function<void (const char *at, size_t length)> bodyCb;
	std::string url;
	std::string status;
	std::map<std::string,std::string> headers;
	std::string field;
	std::string value;
	bodyCb onBody;
};

class HttpSession;

class HttpRequest;

class HttpResponse{

	friend class HttpRequest;

public:

	HttpResponse(const std::string &httpversion,const std::string &code,const std::string &status) {
		resp.append(httpversion).append(" ").append(code).append(" ").append(status).append("\r\n");
	}

	void appendHeader(const std::string &field,const std::string &value) {
		resp.append(field).append(":").append(value).append("\r\n");
	}

	void setBody(const std::string &body) {
		this->body = body;
	}


private:	

	void Send(std::shared_ptr<HttpSession> &session,bool closedOnFlush = false) const;

	std::string resp;
	std::string body;
};


class HttpRequest : public std::enable_shared_from_this<HttpRequest> {

public:

	static const std::string emptyValue;

	typedef std::shared_ptr<HttpRequest> Ptr;

	const std::string& Url() const {
		return this->packet->url;
	}

	const std::string& Status() const {
		return this->packet->status;
	}

	const std::string& Field(const std::string &field) {
		auto it = this->packet->headers.find(field);
		if(it != this->packet->headers.end()) {
			return it->second;
		} else {
			return emptyValue;
		}
	}

	void OnBody(const HttpPacket::bodyCb &onbody) {
		this->packet->onBody = onbody;
	}

	void SendResp(const HttpResponse &resp,bool closedOnFlush = false);

	HttpRequest(const std::shared_ptr<HttpSession> &session,std::shared_ptr<HttpPacket> &packet):session(session),packet(packet) {

	}

private:

	
	std::shared_ptr<HttpSession> session;
	std::shared_ptr<HttpPacket>  packet;

};	


class HttpSession : public std::enable_shared_from_this<HttpSession> {

	friend class HttpRequest;
	friend class HttpResponse;

public:

	typedef std::shared_ptr<HttpSession> Ptr;

	static const int ClientSide = 1;
	static const int ServerSide = 2;

	typedef std::function<void (HttpRequest::Ptr &)> OnRequest;

	static HttpSession::Ptr New(TCPSocket::Ptr &s,int side){
		return Ptr(new HttpSession(s,side));
	}	

	void Start(const OnRequest &onRequest) {
		if(!this->onRequest && onRequest) {
			this->onRequest = onRequest;
			s->Start();
			this->Recv();
		}
	}

	~HttpSession(){		
	}

private:

	static int on_message_begin(http_parser *parser);

	static int on_url(http_parser *parser, const char *at, size_t length);

	static int on_status(http_parser *parser, const char *at, size_t length);

	static int on_header_field(http_parser *parser, const char *at, size_t length);

	static int on_header_value(http_parser *parser, const char *at, size_t length);

	static int on_headers_complete(http_parser *parser);

	static int on_body(http_parser *parser, const char *at, size_t length);

	static int on_message_complete(http_parser *parser);

	static void onData(TCPSocket::Ptr &ss,const Buffer::Ptr &buff,size_t n);
	static void onClose(TCPSocket::Ptr &ss);
	static void onError(TCPSocket::Ptr &ss,int err);


	HttpSession(TCPSocket::Ptr &s,int side):s(s) {
		::memset(&this->parser,0,sizeof(this->parser));
		::memset(&this->settings,0,sizeof(this->settings));
		this->parser.data = this;
		this->settings.on_message_begin = on_message_begin;
		this->settings.on_url = on_url;
		this->settings.on_status = on_status;
		this->settings.on_header_field = on_header_field;
		this->settings.on_header_value = on_header_value;
		this->settings.on_headers_complete = on_headers_complete;
		this->settings.on_body = on_body;
		this->settings.on_message_complete = on_message_complete;

		if(side == ServerSide) {
			side = ServerSide;
			http_parser_init(&this->parser,HTTP_REQUEST);
		} else {
			side = ClientSide;
			http_parser_init(&this->parser,HTTP_RESPONSE);
		}

		this->recvbuff = Buffer::New(65535,65535);
		s->SetUserData(const_cast<HttpSession*>(this));
		s->SetRecvCallback(onData)->SetCloseCallback(onClose)->SetErrorCallback(onError);

	}

	void Recv();

	void Close();
		
	void Send(const std::string &str,bool closedOnFlush = false);

	HttpSession::Ptr my_shared_from_this() {
		return shared_from_this();
	}

	HttpSession(const HttpSession&);
	HttpSession(HttpSession&&);
	HttpSession& operator = (const HttpSession&);

private:
	http_parser           		parser;
	http_parser_settings  		settings;
	TCPSocket::Ptr        		s;
	Buffer::Ptr           		recvbuff;
	int                   		side;
	std::shared_ptr<HttpPacket> packet;
	OnRequest                   onRequest;
};


}}

#endif