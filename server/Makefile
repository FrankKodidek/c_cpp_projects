CXX = g++
CXXFLAGS = -std=c++17 -Wall -pedantic -g -lpthread

all: server

server: server.o
	$(CXX) $(CXXFLAGS) -o server $^

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<