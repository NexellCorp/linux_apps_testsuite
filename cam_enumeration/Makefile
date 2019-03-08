CFLAGS = -Wall
INCLUDES := -I../../sysroot/include \
LDFLAGS := -L../../sysroot/lib
LIBS += -lnx_v4l2

SRCS += cam_enumeration.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET += cam_enumeration

%.o: %.cpp
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
