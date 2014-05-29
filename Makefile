APP=bqlmon
all: $(APP)

CFLAGS?=-Wall -Werror
LIBS=-lncurses
LDFLAGS?=
CC=$(CROSS)gcc

SRCS:=bqlmon.c
OBJS:=bqlmon.o
HEADERS:=bqlmon.h

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(APP): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS) $(LDFLAGS)

clean:
	-rm -f $(OBJS) $(APP)
