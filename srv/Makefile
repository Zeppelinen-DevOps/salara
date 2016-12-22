srv: srv.o
	${CROSS_COMPILE}gcc -o srv srv.o -L${SYSROOT}/usr/lib -lpthread -ldl
	${CROSS_COMPILE}strip srv
srv.o: srv.c
	${CROSS_COMPILE}gcc -c srv.c -O0 -g -pipe -Wall -D_GNU_SOURCE -I${SYSROOT}/usr/include
clean:
	rm -f srv.o srv


