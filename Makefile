NAME=salara
CFLAGS=-std=gnu99 -D_REENTERANT -Os -g -pipe -Wall -D_GNU_SOURCE -fPIC
LFLAGS=-std=gnu99 -shared -fPIC -DPIC
INC_PREFIX=/usr
$(NAME).so: $(NAME).o
	gcc $(LFLAGS) -o $(NAME).so $(NAME).o -L/usr/lib -lpthread -ljansson -lcurl -ldl
	strip $(NAME).so
$(NAME).o: $(NAME).c
	gcc $(CFLAGS) -I$(INC_PREFIX)/include -c $(NAME).c 
clean:
	rm -f $(NAME).o $(NAME).so


