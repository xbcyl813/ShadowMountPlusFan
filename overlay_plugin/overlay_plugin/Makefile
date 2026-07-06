PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS := -O3 -fPIC -shared -Wall -Wextra -std=gnu++11
LDFLAGS := -shared -Wl,-soname=smp_overlay.sprx
LIBS := -lSceVideoOut

all: smp_overlay.sprx

smp_overlay.sprx: smp_overlay.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip --strip-all $@

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $<

clean:
	rm -f smp_overlay.sprx *.o
