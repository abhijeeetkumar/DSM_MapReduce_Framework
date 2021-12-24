LDFLAGS = -L/usr/local/lib `pkg-config --libs protobuf grpc++`\
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed\
           -ldl

INCLUDES = ../include/
VPATH = ../include/

CXX = g++
CPPFLAGS += `pkg-config --cflags protobuf grpc `
CXXFLAGS += -std=c++11 -I../include/ -I.

GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

all: p1 p2 p3

p1: dataproto.pb.o dataproto.grpc.pb.o p1.o
	$(CXX) $^ $(LDFLAGS) -o $@ 

p2: dataproto.pb.o dataproto.grpc.pb.o p2.o
	$(CXX) $^ $(LDFLAGS) -o $@

p3: dataproto.pb.o dataproto.grpc.pb.o p3.o
	$(CXX) $^ $(LDFLAGS) -o $@

%.grpc.pb.cc: %.proto
	protoc -I$(INCLUDES) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) ../include/dataproto.proto

%.pb.cc: %.proto
	protoc -I$(INCLUDES) --cpp_out=. ../include/dataproto.proto

clean:
	rm -f *.o ../include/*.pb.cc *.pb.h p1 p2 p3 dbg_log*
