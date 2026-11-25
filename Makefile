CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

SRC = src/main.cpp \
      src/instance.cpp \
      src/solution.cpp \
      src/initializer.cpp \
      src/localsearch.cpp \
      src/acs.cpp

BIN = mdbirp

.PHONY: all clean

all: $(BIN)

$(BIN): $(SRC) $(wildcard src/*.hpp)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)
