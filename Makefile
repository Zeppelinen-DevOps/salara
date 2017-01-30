NAME=salara
CFLAGS=-std=gnu99 -D_REENTERANT -Os -g -pipe -Wall -D_GNU_SOURCE -fPIC
LFLAGS=-std=gnu99 -shared -fPIC -DPIC
PREFIX=${SYSROOT}/usr
GRP=wheel

MACHINE := $(shell uname -m)

ifeq ($(MACHINE), x86_64)
	libdir = $(PREFIX)/lib64
else
	libdir = $(PREFIX)/lib
endif


$(NAME).so: $(NAME).o
	${CROSS_COMPILE}gcc $(LFLAGS) -o $(NAME).so $(NAME).o -L$(libdir) -lpthread -ljansson -lcurl -ldl
	${CROSS_COMPILE}strip $(NAME).so
	chown :$(GRP) $(NAME).so $(NAME).o
$(NAME).o: $(NAME).c
	${CROSS_COMPILE}gcc $(CFLAGS) -I$(PREFIX)/include -c $(NAME).c 
clean:
	rm -f $(NAME).o $(NAME).so


