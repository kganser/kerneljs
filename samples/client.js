var client = connect(function(conn) {
  conn.reader = function(text) { print(text); };
  conn.write("GET / HTTP/1.1\r\nHost: www.yahoo.com\r\n\r\n");
}, 80, "yahoo.com");
