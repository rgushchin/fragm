EXEC=fragm
CC=gcc
CFLAGS=-Wall

SRCS=$(shell find ./ -name *.c)

OBJS := $(SRCS:%=%.o)
DEPS := $(OBJS:.o=.d)

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.c.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -r *.o

-include $(DEPS)
