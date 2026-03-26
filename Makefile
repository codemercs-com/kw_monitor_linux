CXX      := g++
CC       := gcc
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Ideps
CFLAGS   := -std=c11  -Wall -Wextra -O2 -Ideps -D_GNU_SOURCE
LDFLAGS  := -ludev

TARGET   := kw_monitor
SRCS_CPP := main.cpp
SRCS_C   := deps/hidapi/hid.c

OBJ_CPP  := $(SRCS_CPP:.cpp=.o)
OBJ_C    := $(SRCS_C:.c=.o)
OBJS     := $(OBJ_CPP) $(OBJ_C)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
