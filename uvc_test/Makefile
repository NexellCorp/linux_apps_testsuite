CFLAGS = -Wall
INCLUDES := -I../../sysroot/include
LDFLAGS := -L../../sysroot/lib
LIBS += -lm -ldl -lpthread -lrt -lv4l2 -lv4lconvert -ljpeg

CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc

SRCS_C := $(wildcard *.c)
OBJS_C := $(SRCS_C:.c=.o)

TARGET := uvc-test

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $^

$(TARGET): $(OBJS_C)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) $(LFLAGS)

install: $(TARGET)
	cp $^ ../../sysroot/bin

all: $(TARGET)

.PHONY: clean

clean:
	rm -f *.o
	rm -f $(TARGET)
