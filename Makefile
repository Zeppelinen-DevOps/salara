NAME=salara
CFLAGS=-std=gnu99 -D_REENTERANT -Os -g -pipe -Wall -D_GNU_SOURCE -fPIC
LFLAGS=-std=gnu99 -shared -fPIC -DPIC
$(NAME).so: $(NAME).o
	${CROSS_COMPILE}gcc $(LFLAGS) -o $(NAME).so $(NAME).o -L${SYSROOT}/usr/lib -lpthread -ljansson -ldl
	${CROSS_COMPILE}strip $(NAME).so
$(NAME).o: $(NAME).c
	${CROSS_COMPILE}gcc $(CFLAGS) -c $(NAME).c -I${SYSROOT}/usr/include
clean:
	rm -f $(NAME).o $(NAME).so


