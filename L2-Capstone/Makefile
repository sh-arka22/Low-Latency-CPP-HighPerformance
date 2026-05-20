CXX      = g++
CXXFLAGS = -O2 -std=c++20 -Wall -Wextra -march=native
LDFLAGS  =

SRCS = bench/main.cpp bench/gen_data.cpp src/naive.cpp src/fast.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = bench_run

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c $< -o $@

asm-fast:
	$(CXX) $(CXXFLAGS) -Iinclude -S src/fast.cpp -o fast.s

clean:
	rm -f $(OBJS) $(BIN) fast.s

.PHONY: all clean asm-fast
