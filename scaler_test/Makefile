CFLAGS = -Wall
INCLUDES := -I./
INCLUDES += -I../../sysroot/include
INCLUDES += -I../../sysroot/include/nexell
INCLUDES += -I../../sysroot/include/libdrm
INCLUDES += -I../../sysroot/include/libkms
INCLUDES += -I../../nx-renderer/include
LDFLAGS := -L../../sysroot/lib
# LIBS := -lnx-drm-allocator -lnx-renderer -lnx-v4l2 -lnx-scaler
LIBS := -lnx_drm_allocator -lnx_renderer -lnx_v4l2 -lnx_scaler
LIBS += -lkms -ldrm

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc

SRCS := scaler-test.cpp option.cpp
OBJS := $(SRCS:.cpp=.o)
#SRCS := $(wildcard *.c)
#OBJS := $(SRCS:.c=.o)

TARGET := nx-scaler-test

%.o: %.cpp
	$(CC) $(INCLUDES) $(CFLAGS) -c $^

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

install: $(TARGET)
	cp $^ ../../sysroot/bin

all: $(TARGET)

.PHONY: clean

clean:
	rm -f *.o
	rm -f $(TARGET)
