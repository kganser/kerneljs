#ifndef KERNEL_H_
#define KERNEL_H_

#include <eio.h>
#include <ev.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <v8.h>

using namespace std;
using namespace v8;

namespace kernel {

class Timer {
  Timer(Handle<Function> cb, double time);
  static Handle<Value> Clear(const Arguments& args);
  static void Timeout(EV_P_ ev_timer *watcher, int revents);
  static void Dispose(Persistent<Value> object, void *parameter);
  
  static Persistent<ObjectTemplate> obj_template;
  Persistent<Object> object;
  Persistent<Function> callback;
  ev_timer watcher;
  
  public:
    static Handle<Value> New(const Arguments& args);
};

class Agent {
  protected:
    static void Resolve(eio_req *req);
    
    Persistent<Object> object;
    Persistent<Function> callback;
    ev_io watcher;
    const char *host;
    const char *port;
    struct addrinfo *address;
    
    class Connection {
      public:
        Connection(int fd, const struct sockaddr_storage& addr);
        static Handle<Value> GetReader(Local<String> property, const AccessorInfo &info);
        static void SetReader(Local<String> property, Local<Value> value, const AccessorInfo& info);
        static void Read(EV_P_ ev_io *watcher, int revents);
        static void Write(EV_P_ ev_io *watcher, int revents);
        static Handle<Value> Write(const Arguments &args);
        static Handle<Value> Close(const Arguments &args);
        static void Dispose(Persistent<Value> object, void *parameter);
        
        static Persistent<ObjectTemplate> obj_template;
        char *address;
        Persistent<Object> object;
        Persistent<Function> callback;
        string read_buffer;
        string write_buffer;
        ev_io reader;
        ev_io writer;
    };
};

class Server: public Agent {
  Server(Handle<Function> cb, const char *port);
  static int OnResolve(eio_req *req);
  static void Listen(EV_P_ ev_io *watcher, int revents);
  static void Close();
  static Handle<Value> Close(const Arguments &args);
  static void Dispose(Persistent<Value> object, void *parameter);
  
  static Persistent<ObjectTemplate> obj_template;
  
  public:
    static Handle<Value> New(const Arguments &args);
};

class Client: public Agent {
  Client(Handle<Function> cb, const char *port, const char *host);
  static int OnResolve(eio_req *req);
  static void Connect(EV_P_ ev_io *watcher, int revents);
  static void Dispose(Persistent<Value> object, void *parameter);
  
  static Persistent<ObjectTemplate> obj_template;
  
  public:
    static Handle<Value> New(const Arguments &args);
};

}

#endif
