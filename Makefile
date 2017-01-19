NAME=salara
CFLAGS=-std=gnu99 -D_REENTERANT -Os -g -pipe -Wall -D_GNU_SOURCE -fPIC
LFLAGS=-std=gnu99 -shared -fPIC -DPIC
INC_PREFIX=${SYSROOT}/usr
GRP=wheel
$(NAME).so: $(NAME).o
	${CROSS_COMPILE}gcc $(LFLAGS) -o $(NAME).so $(NAME).o -L${SYSROOT}/usr/lib -lpthread -ljansson -lcurl -ldl
	${CROSS_COMPILE}strip $(NAME).so
	chown :$(GRP) $(NAME).so $(NAME).o
$(NAME).o: $(NAME).c
	${CROSS_COMPILE}gcc $(CFLAGS) -I$(INC_PREFIX)/include -c $(NAME).c 
clean:
	rm -f $(NAME).o $(NAME).so


