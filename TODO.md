* preserve utf-8 character boundaries in buffers when calling back
  to javascript
* expose peer address as member of the connection object
* `ondisconnect` handler for connection object
* write test cases; provide better error handling
* provide hooks for debugging and testing proper garbage collection,
  resource utilization using javascript
* wrap core components (i.e., access to event loop, thread pool, and
  interpreter-specific utility methods) in a separate c++ api
* add a proper build system
