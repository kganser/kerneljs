#include "kernel.h"

using namespace kernel;

/* Main loop and eio watchers */
struct ev_loop *loop;
ev_idle repeat_watcher;
ev_async ready_watcher;

/* Callbacks for eio */
void repeat(struct ev_loop *loop, ev_idle *w, int revents) {
  if (eio_poll() != -1) ev_idle_stop(loop, w);
}
void ready(struct ev_loop *loop, ev_async *w, int revents) {
  if (eio_poll() == -1) ev_idle_start(loop, &repeat_watcher);
  else if (!eio_nreqs()) ev_async_stop(loop, &ready_watcher);
}
void want_poll() {
  ev_async_send(loop, &ready_watcher);
}

int Error(const TryCatch& try_catch) {
  Handle<Message> msg = try_catch.Message();
  fprintf(stderr, "%s:%i: %s\n", *String::Utf8Value(msg->GetScriptResourceName()), msg->GetLineNumber(), *String::Utf8Value(msg->Get()));
  return 1;
}

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
    timer->callback.Dispose();
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
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  watcher.data = this;
  
  ev_timer_init(&watcher, Timeout, time, 0.);
  ev_now_update(EV_DEFAULT_UC);
  ev_timer_start(loop, &watcher);
}

void Timer::Timeout(struct ev_loop *loop, ev_timer *watcher, int revents) {
  Timer *timer = static_cast<Timer *>(watcher->data);
  TryCatch try_catch;
  Handle<Value> result = timer->callback->Call(Context::GetCurrent()->Global(), 0, NULL);
  if (try_catch.HasCaught()) Error(try_catch);
  timer->callback.Dispose();
  if (timer->object.IsEmpty()) delete timer;
}

void Timer::Dispose(Persistent<Value> object, void *parameter) {
  Timer *timer = static_cast<Timer *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  timer->object.Dispose();
  if (!ev_is_active(&timer->watcher)) delete timer;
}

Persistent<ObjectTemplate> Server::obj_template;
Persistent<ObjectTemplate> Client::obj_template;
Persistent<ObjectTemplate> Agent::Connection::obj_template;

void Agent::Resolve(eio_req *req) {
  Agent *agent = static_cast<Agent *>(req->data);
  struct addrinfo hints;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (agent->host.empty()) hints.ai_flags = AI_PASSIVE;

  req->result = getaddrinfo(agent->host.empty() ? NULL : agent->host.c_str(), agent->port.c_str(), &hints, &agent->address);
}

Handle<Value> Agent::Close(const Arguments &args) {
  Agent *agent = static_cast<Agent *>(args.Holder()->GetPointerFromInternalField(0));
  if (ev_is_active(&agent->watcher)) {
    ev_io_stop(loop, &agent->watcher);
    close(agent->watcher.fd);
    agent->callback.Dispose();
  }
  return Undefined();
}

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
  inet_ntop(addr.ss_family, sa->sa_family == AF_INET ? (void *) &((struct sockaddr_in*)sa)->sin_addr : (void *) &((struct sockaddr_in6*)sa)->sin6_addr, address, sizeof(address));
  
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  reader.data = writer.data = this;
  read_buffer = "";
  write_buffer = "";
  
  ev_io_init(&reader, Read, fd, EV_READ);
  ev_io_init(&writer, Write, fd, EV_WRITE);
  ev_io_start(loop, &reader);
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
    // TODO: Stop writer after write buffer is empty
    if (ev_is_active(&conn->writer))
      ev_io_stop(loop, &conn->writer);
    ev_io_stop(loop, &conn->reader);
    close(conn->reader.fd);
    if (conn->object.IsEmpty()) delete conn;
  }
  return Undefined();
}

void Agent::Connection::Read(struct ev_loop *loop, ev_io *watcher, int revents) {
  Connection *conn = static_cast<Connection *>(watcher->data);
  char buffer[1024];
  int bytes, peek = conn->callback.IsEmpty() ? MSG_PEEK : 0;
  
  while ((bytes = recv(watcher->fd, buffer, sizeof buffer, peek)) > 0 && !peek)
    conn->read_buffer.append(buffer, bytes);
  
  if (!bytes) {
    // Connection closed by peer
    ev_io_stop(loop, watcher);
    close(watcher->fd);
    if (conn->object.IsEmpty()) delete conn;
  } else if (!peek && conn->read_buffer.length()) {
    // Call back to javascript
    // TODO: Make UTF-8 safe
    Handle<Value> arg[] = { String::New(conn->read_buffer.c_str()) };
    conn->callback->Call(conn->object, 1, arg);
    conn->read_buffer = "";
  }
}

void Agent::Connection::Write(struct ev_loop *loop, ev_io *watcher, int revents) {
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
  conn->object.Dispose();
  conn->callback.Dispose(); // check isEmpty?
  if (!ev_is_active(&conn->reader)) delete conn;
}

Handle<Value> Server::New(const Arguments &args) {
  if (args.Length() < 2 || !args[0]->IsFunction()) return Undefined();
  const char *port = strdup(*String::Utf8Value(args[1]));
  return (new Server(Handle<Function>::Cast(args[0]), port))->object;
}

Server::Server(Handle<Function> cb, const char *port) {
  this->port = port;
  
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
    obj_template->Set(String::New("close"), FunctionTemplate::New(Close));
  }
  
  object = Persistent<Object>::New(obj_template->NewInstance());
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  callback = Persistent<Function>::New(cb);
  
  if (!ev_is_active(&ready_watcher)) ev_async_start(loop, &ready_watcher);
  eio_custom(Resolve, 0, OnResolve, this);
}

int Server::OnResolve(eio_req *req) {
  if (req->result) fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(req->result));
  Server *server = static_cast<Server *>(req->data);
  struct addrinfo *a;
  int yes = 1, sock;
  
  for (a = server->address; a; a = a->ai_next) {
    if ((sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) == -1) continue;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (bind(sock, a->ai_addr, a->ai_addrlen) == -1) { close(sock); continue; }
    break;
  }
  
  freeaddrinfo(server->address);
  if (!a) perror("bind");
  if (listen(sock, -1) == -1) perror("listen");
  
  server->watcher.data = server;
  ev_io_init(&server->watcher, Listen, sock, EV_READ);
  ev_io_start(loop, &server->watcher);
  
  return 0;
}

void Server::Listen(struct ev_loop *loop, ev_io *watcher, int revents) {
  Server *server = static_cast<Server *>(watcher->data);
  
  int sock;
  struct sockaddr_storage address;
  socklen_t sock_size = sizeof address;
  
  if ((sock = accept(watcher->fd, (struct sockaddr *)&address, &sock_size)) >= 0) {
    fcntl(sock, F_SETFL, O_NONBLOCK);
    TryCatch try_catch;
    Handle<Value> result = server->callback->Call((new Connection(sock, address))->object, 0, NULL);
    if (try_catch.HasCaught()) Error(try_catch);
  }
}

void Server::Dispose(Persistent<Value> object, void *parameter) {
  Server *server = static_cast<Server *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  server->object.Dispose();
  if (!ev_is_active(&server->watcher)) delete server;
}

Handle<Value> Client::New(const Arguments &args) {
  if (args.Length() < 2 || !args[0]->IsFunction()) return Undefined();
  const char *host = args.Length() > 2 ? strdup(*String::Utf8Value(args[2])) : "127.0.0.1";
  const char *port = strdup(*String::Utf8Value(args[1]));
  return (new Client(Handle<Function>::Cast(args[0]), port, host))->object;
}

Client::Client(Handle<Function> cb, const char *port, const char *host) {
  this->host = host;
  this->port = port;
  
  if (obj_template.IsEmpty()) {
    obj_template = Persistent<ObjectTemplate>::New(ObjectTemplate::New());
    obj_template->SetInternalFieldCount(1);
    obj_template->Set(String::New("close"), FunctionTemplate::New(Close));
  }
  
  object = Persistent<Object>::New(obj_template->NewInstance());
  object.MakeWeak(NULL, Dispose);
  object->SetPointerInInternalField(0, this);
  callback = Persistent<Function>::New(cb);
  
  if (!ev_is_active(&ready_watcher)) ev_async_start(loop, &ready_watcher);
  eio_custom(Resolve, 0, OnResolve, this);
}

int Client::OnResolve(eio_req *req) {
  if (req->result) fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(req->result));
  Client *client = static_cast<Client *>(req->data);
  struct addrinfo *a;
  int sock;
  
  for (a = client->address; a; a = a->ai_next) {
    if ((sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) == -1) continue;
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, a->ai_addr, a->ai_addrlen) == -1 && errno != EINPROGRESS) { close(sock); continue; }
    break;
  }
  
  if (!(client->address = a)) perror("connect");
  
  client->watcher.data = client;
  ev_io_init(&client->watcher, Connect, sock, EV_WRITE);
  ev_io_start(loop, &client->watcher);
  
  return 0;
}

void Client::Connect(struct ev_loop *loop, ev_io *watcher, int revents) {
  Client *client = static_cast<Client *>(watcher->data);
  
  if (!connect(watcher->fd, client->address->ai_addr, client->address->ai_addrlen) || errno == EISCONN) {
    TryCatch try_catch;
    Handle<Value> result = client->callback->Call((new Connection(watcher->fd, *(struct sockaddr_storage*)client->address->ai_addr))->object, 0, NULL);
    if (try_catch.HasCaught()) Error(try_catch);
  }
  
  freeaddrinfo(client->address);
  ev_io_stop(loop, watcher);
}

void Client::Dispose(Persistent<Value> object, void *parameter) {
  Client *client = static_cast<Client *>(Object::Cast(*object)->GetPointerFromInternalField(0));
  client->object.Dispose();
  if (!ev_is_active(&client->watcher)) delete client;
}

Handle<Value> Purge(const Arguments& args) {
  while (!V8::IdleNotification());
  return Undefined();
}

Handle<Value> Print(const Arguments& args) {
  fputs(*String::Utf8Value(args[0]), stdout);
  return Undefined();
}

Handle<String> ReadScript(istream &input) {
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
    code = ReadScript(file);
    file.close();
  } else {
    source = "stdin";
    code = ReadScript(cin);
  }
  
  if (!code.IsEmpty()) {
  
    loop = EV_DEFAULT;
    
    /* Initialize eio */
    ev_idle_init(&repeat_watcher, repeat);
    ev_async_init(&ready_watcher, ready);
    eio_init(want_poll, 0);
    
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
