all: kernel libkernel.a

install: kernel libkernel.a
	cp kernel /usr/local/bin/
	cp libkernel.a /usr/local/lib/
	cp src/kernel.h /usr/local/include/

kernel: src/kernel.cc src/kernel.h
	g++ -o kernel src/kernel.cc -lev -leio -lv8

libkernel.a: src/kernel.cc src/kernel.h
	g++ -c -o libkernel.a src/kernel.cc -DLIBKERNEL

clean:
	rm -f kernel libkernel.a
