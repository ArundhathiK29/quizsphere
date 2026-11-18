#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
static void closeSocket(Socket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket invalid_socket = -1;
static void closeSocket(Socket socket) { close(socket); }
#endif

#include <algorithm>
#include <charconv>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "web_application.hpp"
#include "web_assets.hpp"

namespace {
volatile std::sig_atomic_t running = 1;
void stopServer(int) { running = 0; }
std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;
}
std::string trim(const std::string& value) {
    const auto first=value.find_first_not_of(" \t\r");
    return first==std::string::npos?"":value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
void respond(Socket socket,int status,const std::string& body,const std::string& type,const std::string& cookie="") {
    std::string data="HTTP/1.1 "+std::to_string(status)+" Response\r\nContent-Type: "+type+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n";
    if(!cookie.empty())data+="Set-Cookie: "+cookie+"\r\n";
    data+="\r\n"+body;
    std::size_t sent=0;
    while(sent<data.size()){
        const int count=send(socket,data.data()+sent,static_cast<int>(std::min<std::size_t>(data.size()-sent,32768)),0);
        if(count<=0)break;
        sent+=static_cast<std::size_t>(count);
    }
}
bool receive(Socket socket,std::string& data) {
    char buffer[4096];const int count=recv(socket,buffer,sizeof(buffer),0);
    if(count<=0)return false;
    data.append(buffer,static_cast<std::size_t>(count));return true;
}
void serve(Socket socket,quizsphere::WebApplication& app,int port) {
#ifdef _WIN32
    DWORD timeout=1000;
#else
    timeval timeout{1,0};
#endif
    setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    const auto reject=[&](int code,const std::string& reason){respond(socket,code,"{\"error\":\""+reason+"\"}","application/json; charset=utf-8");};
    std::string data;
    while(data.find("\r\n\r\n")==std::string::npos){
        if(data.size()>8192){reject(431,"Headers too large.");return;}
        if(!receive(socket,data))return;
    }
    const auto end=data.find("\r\n\r\n");if(end>8192){reject(431,"Headers too large.");return;}
    std::istringstream input(data.substr(0,end));std::string line,method,path,version,extra;
    if(!std::getline(input,line)){reject(400,"Malformed request.");return;}
    std::istringstream first(line);
    if(!(first>>method>>path>>version)||first>>extra||version!="HTTP/1.1"||path.empty()||path.front()!='/'){
        reject(400,"Malformed request line.");return;
    }
    std::map<std::string,std::string> headers;
    while(std::getline(input,line)){
        const auto colon=line.find(':');
        if(colon==std::string::npos){reject(400,"Malformed header.");return;}
        const auto key=lower(line.substr(0,colon));
        if(headers.count(key)){reject(400,"Duplicate header.");return;}
        headers.emplace(key,trim(line.substr(colon+1)));
    }
    const auto host=headers["host"];
    const auto suffix=":"+std::to_string(port);
    if(host!="127.0.0.1"+suffix&&host!="localhost"+suffix){reject(403,"Local host required.");return;}
    if(headers.count("transfer-encoding")){reject(400,"Transfer encoding is unsupported.");return;}
    std::size_t length=0;
    if(headers.count("content-length")){
        const auto& raw=headers["content-length"];const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),length);
        if(raw.empty()||parsed.ec!=std::errc{}||parsed.ptr!=raw.data()+raw.size()||length>65536){reject(413,"Invalid body length.");return;}
    }
    if(method=="POST"){
        if(headers["origin"]!="http://"+host){reject(403,"Same-origin request required.");return;}
        if(headers["content-type"].rfind("application/x-www-form-urlencoded",0)!=0){reject(415,"Use form encoding.");return;}
    }
    std::string body=data.substr(end+4);
    while(body.size()<length){if(!receive(socket,body)){reject(400,"Incomplete request body.");return;}}
    body.resize(length);
    if(method=="GET"&&path=="/"){respond(socket,200,quizsphere::web_html,"text/html; charset=utf-8");return;}
    if(method=="GET"&&path=="/app.css"){respond(socket,200,quizsphere::web_css,"text/css; charset=utf-8");return;}
    if(method=="GET"&&path=="/app.js"){respond(socket,200,quizsphere::web_js,"text/javascript; charset=utf-8");return;}
    if(path.rfind("/api/",0)!=0){reject(404,"Not found.");return;}
    const auto result=app.handle({method,path,body,headers["cookie"],headers["x-csrf-token"]});
    respond(socket,result.status,result.body,"application/json; charset=utf-8",result.cookie);
}
}  // namespace

int main(int argc,char** argv) {
    int port=8765;const char* env=std::getenv("QUIZSPHERE_DB");std::string database=env?env:"quizsphere.db";
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--db"&&i+1<argc)database=argv[++i];
        else if(arg=="--port"&&i+1<argc){const std::string raw=argv[++i];const auto p=std::from_chars(raw.data(),raw.data()+raw.size(),port);if(p.ec!=std::errc{}||p.ptr!=raw.data()+raw.size()||port<1024||port>65535){std::cerr<<"Port must be between 1024 and 65535.\n";return 1;}}
        else{std::cerr<<"Usage: quizsphere_web [--port 8765] [--db quizsphere.db]\n";return 1;}
    }
#ifdef _WIN32
    WSADATA network{};if(WSAStartup(MAKEWORD(2,2),&network)!=0){std::cerr<<"Network initialization failed.\n";return 1;}
#else
    std::signal(SIGPIPE,SIG_IGN);
#endif
    std::signal(SIGINT,stopServer);
    std::signal(SIGTERM,stopServer);
    try{
        quizsphere::WebApplication app(database);
        const Socket listener=socket(AF_INET,SOCK_STREAM,0);
        if(listener==invalid_socket)throw std::runtime_error("Cannot create local listener.");
#ifdef _WIN32
        const int exclusive=1;
        if(setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0){closeSocket(listener);throw std::runtime_error("Cannot reserve the local listener.");}
#else
        const int reuse=1;
        if(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))!=0){closeSocket(listener);throw std::runtime_error("Cannot configure listener restart.");}
#endif
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<unsigned short>(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0||listen(listener,16)!=0){closeSocket(listener);throw std::runtime_error("Cannot listen on this port. Choose another --port.");}
        std::cout<<"QuizSphere browser UI: http://127.0.0.1:"<<port<<"\nDatabase: "<<database<<"\nLocal access only. Stop with Ctrl+C.\n"<<std::flush;
        while(running){
            app.tick();fd_set ready;FD_ZERO(&ready);FD_SET(listener,&ready);timeval wait{0,200000};
            const int count=select(static_cast<int>(listener)+1,&ready,nullptr,nullptr,&wait);
            if(count<0)break;
            if(count==0)continue;
            const Socket client=accept(listener,nullptr,nullptr);if(client==invalid_socket)continue;
            serve(client,app,port);closeSocket(client);
        }
        closeSocket(listener);
    }catch(const std::exception& error){std::cerr<<"Startup failure: "<<error.what()<<'\n';return 1;}
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
