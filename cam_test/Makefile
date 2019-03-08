CFLAGS = -Wall
INCLUDES := -I../../sysroot/include \
		-I../../sysroot/include/nexell \
		-I../../sysroot/include/libdrm
LDFLAGS := -L../../sysroot/lib

#LIBS := -lnx_v4l2 -lnx_allocator -lpthread
#SRCS := cam_test_thread.cpp

LIBS += -lkms -ldrm -ldrm_nexell -lnx_v4l2 -lnx_scaler -lnx_renderer -lnx_drm_allocator -lpthread
SRCS := cam_test.cpp option.cpp memory.cpp display.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := cam_test

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
