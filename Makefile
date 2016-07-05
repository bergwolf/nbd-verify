VERSION=0.4

DEBUG_FLAGS=-g
CXXFLAGS+=-O3 -pedantic -Wall -Wno-long-long -DVERSION=\"$(VERSION)\" $(DEBUG_FLAGS)
LDFLAGS=$(DEBUG_FLAGS)

OBJS=nbd.o utils-data.o utils-net.o utils-str.o main.o utils-time.o

all: nbd-verify

nbd-verify: $(OBJS)
	$(CXX) -Wall -W $(OBJS) $(LDFLAGS) -o nbd-verify

clean:
	rm -f $(OBJS) nbd-verify

package: clean
	# source package
	rm -rf nbd-verify-$(VERSION)
	mkdir nbd-verify-$(VERSION)
	cp *.cpp *.h Makefile readme.txt license.txt nbd-verify-$(VERSION)
	tar czf nbd-verify-$(VERSION).tgz nbd-verify-$(VERSION)
	rm -rf nbd-verify-$(VERSION)

check:
	cppcheck -v --enable=all --std=c++11 --inconclusive -I. . 2> err.txt
