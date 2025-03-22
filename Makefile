obj-m += at24.o
CFLAGS_EXTRA += -DDEBUG
KERNEL_SRC = /lib/modules/$(shell uname -r)/source

all:
	make -C $(KERNEL_SRC) M=$(shell pwd) modules

clean:
	make -C $(KERNEL_SRC) M=$(shell pwd) clean
	rm app

format:
	clang-format -i -style=file *.c

app: app.c
	$(CC) app.c -o app
