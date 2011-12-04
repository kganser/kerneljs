#ifndef KERNEL_H_
#define KERNEL_H_

#define EV_STANDALONE 1
#include "libev/ev.c"

#include "v8/include/v8.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;
using namespace v8;

namespace kernel {

struct ev_loop *loop = EV_DEFAULT;

int Error(TryCatch try_catch) {
  Handle<Message> msg = try_catch.Message();
  fprintf(stderr, "%s:%i: %s\n", *String::Utf8Value(msg->GetScriptResourceName()), msg->GetLineNumber(), *String::Utf8Value(msg->Get()));
  return 1;
}

class Timer {
  public:
    static Handle<Value> New(const Arguments& args);
  private:
    Timer(Handle<Function> cb, double time);
    static Handle<Value> Clear(const Arguments& args);
    static void Timeout(EV_P_ ev_timer *watcher, int revents);
    static void Dispose(Persistent<Value> object, void *parameter);
    static Persistent<ObjectTemplate> obj_template;
    Persistent<Object> object;
    Persistent<Function> callback;
    ev_timer watcher;
    bool orphan;
};

class Agent {
  protected:
    Persistent<Object> object;
    Persistent<Function> callback;
    ev_io watcher;
    class Connection {
      public:
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
        bool orphan;
        Connection(int fd, const struct sockaddr_storage& addr);
    };
};

class Server: public Agent {
  Server(Handle<Function> cb, const char *port);
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
  static void Dispose(Persistent<Value> object, void *parameter);
  static Persistent<ObjectTemplate> obj_template;
  public:
    static Handle<Value> New(const Arguments &args);
};

}

#endif
