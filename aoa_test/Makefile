CFLAGS = -Wall
INCLUDES := -I../../sysroot/include \
	-I../../sysroot/include/libusb-1.0
LDFLAGS := -L../../sysroot/lib

LIBS += -lpthread -lusb-1.0 -lasound
SRCS := linux-adk.c hid.c accessory.c audio.c buffer.c
OBJS := $(SRCS:.c=.o)
TARGET := aoa_test

%.o: %.c
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) -c $^

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

install: $(TARGET)
	cp $^ $(DESTDIR)

all: $(TARGET)

.PHONY: clean

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)
