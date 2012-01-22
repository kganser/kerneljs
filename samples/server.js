var port = 1357;

listen(function(conn) {
  print("client connected\n");
  conn.reader = function(x) { print("client said "+x); };
  conn.write("hello!\n");
}, port);

print("server listening on port "+port+"\n");
