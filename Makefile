salara.so: salara.o
	${CROSS_COMPILE}gcc -shared -fPIC -DPIC -o salara.so salara.o -L${SYSROOT}/usr/lib -lpthread -ldl
	${CROSS_COMPILE}strip salara.so
salara.o: salara.c
	${CROSS_COMPILE}gcc -c -fPIC salara.c -D_REENTERANT -O0 -g -pipe -Wall -D_GNU_SOURCE -I${SYSROOT}/usr/include
clean:
	rm -f salara.o salara.so


