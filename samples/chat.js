var port = 1357;
var count = 3;

var server = listen(function(conn) {
  var write = function(i) {
    print("server: sent "+i+"\n");
    conn.write(i+"\n");
    if (i < count)
      setTimeout(function() {
        write(i+1);
      }, 1000);
  };
  conn.reader = function(x) {
    print("server: received "+x);
    if (x == "ack "+count+"\n") {
      conn.close();
      server.close();
    }
  };
  write(1);
}, port);

connect(function(conn) {
  conn.reader = function(x) {
    print("client: received "+x+"client: sent ack "+x);
    conn.write("ack "+x);
  };
}, port);
