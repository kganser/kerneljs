kerneljs
========

Kerneljs is a minimalist javascript interpreter with a nonblocking API in the
style of the browser and [nodejs](http://nodejs.org/). Read more about it
[here](http://kganser.com/kerneljs.html).

Kerneljs is still in early development. To build it, you must first build and
install [V8](http://code.google.com/apis/v8/build.html),
[libev](http://software.schmorp.de/pkg/libev.html), and
[libeio](http://software.schmorp.de/pkg/libeio.html). Then, simply run `make
&& sudo make install` to generate and install `kernel.h`, `libkernel.a`, and
the executable, `kernel`, which takes a javascript file name argument or reads
javascript from stdin.

