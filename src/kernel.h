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
  static void Timeout(struct ev_loop *loop, ev_timer *watcher, int revents);
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
    static Handle<Value> Close(const Arguments &args);
    
    Persistent<Object> object;
    Persistent<Function> callback;
    ev_io watcher;
    string host;
    string port;
    struct addrinfo *address;
    
    class Connection {
      public:
        Connection(int fd, const struct sockaddr_storage& addr);
        static Handle<Value> GetReader(Local<String> property, const AccessorInfo &info);
        static void SetReader(Local<String> property, Local<Value> value, const AccessorInfo& info);
        static void Read(struct ev_loop *loop, ev_io *watcher, int revents);
        static void Write(struct ev_loop *loop, ev_io *watcher, int revents);
        static Handle<Value> Write(const Arguments &args);
        static Handle<Value> Close(const Arguments &args);
        static void Dispose(Persistent<Value> object, void *parameter);
        
        static Persistent<ObjectTemplate> obj_template;
        Persistent<Object> object;
        Persistent<Function> callback;
        char address[INET6_ADDRSTRLEN];
        string read_buffer;
        string write_buffer;
        ev_io reader;
        ev_io writer;
    };
};

class Server: public Agent {
  Server(const Handle<Function> &cb, const Handle<Value> &port);
  static int OnResolve(eio_req *req);
  static void Listen(struct ev_loop *loop, ev_io *watcher, int revents);
  static void Dispose(Persistent<Value> object, void *parameter);
  
  static Persistent<ObjectTemplate> obj_template;
  
  public:
    static Handle<Value> New(const Arguments &args);
};

class Client: public Agent {
  Client(const Handle<Function> &cb, const Handle<Value> &port, const Handle<Value> &host);
  static int OnResolve(eio_req *req);
  static void Connect(struct ev_loop *loop, ev_io *watcher, int revents);
  static void Dispose(Persistent<Value> object, void *parameter);
  
  static Persistent<ObjectTemplate> obj_template;
  
  public:
    static Handle<Value> New(const Arguments &args);
};

class Kernel {
  static ev_idle idle_watcher;
  static ev_async async_watcher;
  Handle<ObjectTemplate> global;
  
  static void OnIdle(struct ev_loop *loop, ev_idle *w, int revents);
  static void OnReady(struct ev_loop *loop, ev_async *w, int revents);
  static void Poll();
  
  static string Read(istream &input);
  
  public:
    static struct ev_loop *loop;
    
    static Handle<Value> Print(const Arguments &args);
    static Handle<Value> Purge(const Arguments &args);
    static void RunAsync(void (*execute)(eio_req *), int pri, eio_cb cb, void *data);
    static int Error(const TryCatch &try_catch);
    
    Kernel(): global(ObjectTemplate::New()) {}
    ObjectTemplate *operator->() const { return *global; }
    ObjectTemplate *operator *() const { return *global; }
    int Run(int argc, char *argv[]);
    int Run(string code, string source);
};

}

#endif
