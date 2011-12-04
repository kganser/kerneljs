#include "kernel.h"

using namespace kernel;

Persistent<ObjectTemplate> Timer::obj_template;

Handle<Value> Timer::New(const Arguments& args) {
  if (!args.Length() || !args[0]->IsFunction()) return Undefined();
  double delay = (args.Length() == 1 || !args[1]->IsNumber()) ? 0. : args[1]->NumberValue()/1000.;
  return (new Timer(Handle<Function>::Cast(args[0]), delay))->object;
}

Handle<Value> Timer::Clear(const Arguments& args) {
  Timer *timer = static_cast<Timer *>(args.Holder()->GetPointerFromInternalField(0));
  if (ev_is_active(&timer->watcher)) {
    ev_timer_stop(loop, &timer->watcher);
    timer->callback.Dispose(); // cannot delete timer because it is not orphaned
  }
  return Undefined();
}

Timer::Timer(Handle<Function> cb, double time) {
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
    obj_template->Set(String::New("clear"), FunctionTemplate::New(Clear));
  }
  object = Persistent<Object>::New(obj_template->NewInstance());
  callback = Persistent<Function>::New(cb);
  orphan = false;
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  watcher.data = this;
  
  ev_timer_init(&watcher, Timeout, time, 0.);
  ev_now_update(EV_DEFAULT_UC);
  ev_timer_start(loop, &watcher);
}

void Timer::Timeout(EV_P_ ev_timer *watcher, int revents) {
  Timer *timer = static_cast<Timer *>(watcher->data);
  TryCatch try_catch;
  Handle<Value> result = timer->callback->Call(Context::GetCurrent()->Global(), 0, NULL);
  if (try_catch.HasCaught()) Error(try_catch);
  timer->callback.Dispose();
  if (timer->orphan) delete timer;
}

void Timer::Dispose(Persistent<Value> object, void *parameter) {
  Timer *timer = static_cast<Timer *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  timer->orphan = true;
  timer->object.Dispose();
  if (!ev_is_active(&timer->watcher)) delete timer;
}

Persistent<ObjectTemplate> Server::obj_template;
Persistent<ObjectTemplate> Client::obj_template;
Persistent<ObjectTemplate> Agent::Connection::obj_template;

Agent::Connection::Connection(int fd, const struct sockaddr_storage& addr) {
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
    obj_template->SetAccessor(String::New("reader"), GetReader, SetReader);
    obj_template->Set(String::New("write"), FunctionTemplate::New(Write));
    obj_template->Set(String::New("close"), FunctionTemplate::New(Close));
  }
  object = Persistent<Object>::New(obj_template->NewInstance());
  
  struct sockaddr *sa = (struct sockaddr *) &addr;
  inet_ntop(addr.ss_family, sa->sa_family == AF_INET ? (void *) &((struct sockaddr_in*)sa)->sin_addr : &((struct sockaddr_in6*)sa)->sin6_addr, address, sizeof(address));
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  reader.data = writer.data = this;
  orphan = false;
  read_buffer = "";
  write_buffer = "";
  
  ev_io_init(&reader, Read, fd, EV_READ);
  ev_io_init(&writer, Write, fd, EV_WRITE);
  ev_io_start(loop, &reader); // will this let client register reader callback in time?
}

Handle<Value> Agent::Connection::GetReader(Local<String> property, const AccessorInfo &info) {
  Connection *conn = static_cast<Connection *>(info.Holder()->GetPointerFromInternalField(0));
  return conn->callback;
}

void Agent::Connection::SetReader(Local<String> property, Local<Value> value, const AccessorInfo& info) {
  if (value->IsFunction()) {
    Connection *conn = static_cast<Connection *>(info.Holder()->GetPointerFromInternalField(0));
    if (!conn->callback.IsEmpty()) conn->callback.Dispose();
    conn->callback = Persistent<Function>::New(Handle<Function>::Cast(value));
  }
}

Handle<Value> Agent::Connection::Write(const Arguments &args) {
  Connection *conn = static_cast<Connection *>(args.Holder()->GetPointerFromInternalField(0));
  if (!ev_is_active(&conn->reader)) return Boolean::New(false);
  if (args.Length()) {
    conn->write_buffer += *String::Utf8Value(args[0]);
    if (!ev_is_active(&conn->writer))
      ev_io_start(loop, &conn->writer);
  }
  return conn->object;
}

Handle<Value> Agent::Connection::Close(const Arguments &args) {
  Connection *conn = static_cast<Connection *>(args.Holder()->GetPointerFromInternalField(0));
  if (ev_is_active(&conn->reader)) {
    if (ev_is_active(&conn->writer)) // stop only when write buffer empty?
      ev_io_stop(loop, &conn->writer);
    ev_io_stop(loop, &conn->reader);
    close(conn->reader.fd);
    if (conn->orphan) delete conn;
  }
  return Undefined();
}

void Agent::Connection::Read(EV_P_ ev_io *watcher, int revents) {
  Connection *conn = static_cast<Connection *>(watcher->data);
  char buffer[1024];
  int bytes, flag = conn->callback.IsEmpty() ? MSG_PEEK : 0;
  
  while ((bytes = recv(watcher->fd, buffer, sizeof buffer, flag)) > 0 && !flag)
    conn->read_buffer.append(buffer, bytes);
  
  if (!bytes) {
    ev_io_stop(loop, watcher);
    close(watcher->fd);
    if (conn->orphan) delete conn;
  } else if (!flag && conn->read_buffer.length()) {
    Handle<Value> arg[] = { String::New(conn->read_buffer.c_str()) };
    conn->callback->Call(conn->object, 1, arg);
    conn->read_buffer = "";
  }
}

void Agent::Connection::Write(EV_P_ ev_io *watcher, int revents) {
  Connection *conn = static_cast<Connection *>(watcher->data);
  const char *buffer = conn->write_buffer.c_str();
  int n, sent = 0, left = conn->write_buffer.length();

  while (left && (n = send(watcher->fd, buffer+sent, left, MSG_NOSIGNAL)) >= 0) {
    sent += n;
    left -= n;
  }
  
  if (left) {
    conn->write_buffer = conn->write_buffer.substr(sent);
  } else {
    conn->write_buffer = "";
    ev_io_stop(loop, watcher);
  }
}

void Agent::Connection::Dispose(Persistent<Value> object, void *parameter) {
  Connection *conn = static_cast<Connection *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  conn->orphan = true;
  conn->object.Dispose();
  conn->callback.Dispose(); // check isEmpty?
  if (!ev_is_active(&conn->reader)) delete conn;
}

Handle<Value> Server::New(const Arguments &args) { // callback, port=80, ip address=localhost
  if (args.Length() < 2 || !args[0]->IsFunction()) return Undefined();
  return (new Server(Handle<Function>::Cast(args[0]), *String::Utf8Value(args[1])))->object;
}

Server::Server(Handle<Function> cb, const char *port) {
  int yes = 1, sock, client;
  struct addrinfo hints, *address, *a;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, port, &hints, &address))
    throw "getaddrinfo";

  for (a = address; a; a = a->ai_next) {
    if ((sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) == -1) continue;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (bind(sock, a->ai_addr, a->ai_addrlen) == -1) { close(sock); continue; }
    break;
  }

  freeaddrinfo(address);
  if (!a) throw "bind";
  if (listen(sock, -1) == -1) throw "listen";
  
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
    obj_template->Set(String::New("close"), FunctionTemplate::New(Close));
  }
  object = Persistent<Object>::New(obj_template->NewInstance());
  callback = Persistent<Function>::New(cb);
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  watcher.data = this;
  
  ev_io_init(&watcher, Listen, sock, EV_READ);
  ev_io_start(loop, &watcher);
}

void Server::Listen(EV_P_ ev_io *watcher, int revents) {
  Server *server = static_cast<Server *>(watcher->data);
  
  int sock;
  struct sockaddr_storage address;
  socklen_t sock_size = sizeof address;
  
  if ((sock = accept(watcher->fd, (struct sockaddr *)&address, &sock_size)) >= 0) {
    fcntl(sock, F_SETFL, O_NONBLOCK);
    TryCatch try_catch;
    Handle<Value> arg[] = { (new Connection(sock, address))->object };
    Handle<Value> result = server->callback->Call(Context::GetCurrent()->Global(), 1, arg);
    if (try_catch.HasCaught()) Error(try_catch);
  }
}

Handle<Value> Server::Close(const Arguments &args) {
  Server *server = static_cast<Server *>(args.Holder()->GetPointerFromInternalField(0));
  if (ev_is_active(&server->watcher)) {
    ev_io_stop(loop, &server->watcher);
    close(server->watcher.fd);
    server->callback.Dispose();
  }
  return Undefined();
}

void Server::Dispose(Persistent<Value> object, void *parameter) {
  Server *server = static_cast<Server *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  server->object.Dispose();
  if (!ev_is_active(&server->watcher)) delete server;
}

Handle<Value> Client::New(const Arguments &args) {
  if (args.Length() < 2 || !args[0]->IsFunction()) return Undefined();
  const char *host = args.Length() > 2 ? *String::Utf8Value(args[2]) : "localhost";
  return (new Client(Handle<Function>::Cast(args[0]), *String::Utf8Value(args[1]), host))->object;
}

Client::Client(Handle<Function> cb, const char *port, const char *host) {
  int sock;
  struct addrinfo hints, *address, *a;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(NULL, port, &hints, &address))
    throw "getaddrinfo";

  for (a = address; a; a = a->ai_next) {
    if ((sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) == -1) continue;
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, a->ai_addr, a->ai_addrlen) == -1) { close(sock); continue; }
    break;
  }
  
  if (!a) throw "failed to connect";
  
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
  }
  object = Persistent<Object>::New(obj_template->NewInstance());
  callback = Persistent<Function>::New(cb);
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  
  TryCatch try_catch;
  Handle<Value> arg[] = { (new Connection(sock, *(struct sockaddr_storage*)a->ai_addr))->object };
  Handle<Value> result = callback->Call(Context::GetCurrent()->Global(), 1, arg);
  if (try_catch.HasCaught()) Error(try_catch);
  
  freeaddrinfo(address);
}

void Client::Dispose(Persistent<Value> object, void *parameter) {
  Client *client = static_cast<Client *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  client->object.Dispose();
  //TODO
  //if (!ev_is_active(&client->watcher)) delete client;
}

Handle<Value> Purge(const Arguments& args) {
  while (!V8::IdleNotification());
  return Undefined();
}

Handle<Value> Print(const Arguments& args) {
  String::Utf8Value str(args[0]->ToString());
  fputs(*str, stdout);
  return Undefined();
}

Handle<String> Read(istream &input) {
  string line, output = "";
  while (getline(input, line))
    output += line+'\n';
  return String::New(output.c_str(), output.size());
}

int main(int argc, char* argv[]) {
  
  HandleScope scope;
  const char *source;
  Handle<String> code;
  
  if (argc > 1) {
    ifstream file(source = argv[1]);
    if (!file.is_open()) {
      fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
      return 1;
    }
    code = Read(file);
    file.close();
  } else {
    source = "stdin";
    code = Read(cin);
  }
  
  if (!code.IsEmpty()) {
  
    Handle<ObjectTemplate> global = ObjectTemplate::New();
    global->Set(String::New("setTimeout"), FunctionTemplate::New(Timer::New));
    global->Set(String::New("listen"), FunctionTemplate::New(Server::New));
    global->Set(String::New("connect"), FunctionTemplate::New(Client::New));
    global->Set(String::New("print"), FunctionTemplate::New(Print));

    Persistent<Context> context = Context::New(NULL, global);
    Context::Scope context_scope(context);
    
    TryCatch try_catch;
    Handle<Script> script = Script::Compile(code, String::New(source));
    
    if (script.IsEmpty() || script->Run().IsEmpty())
      return Error(try_catch);
    
    ev_run(loop, 0);
    context.Dispose();
  }
  
  return 0;
}

