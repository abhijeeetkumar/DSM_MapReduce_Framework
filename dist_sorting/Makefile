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

all: app1

app1: dataproto.pb.o dataproto.grpc.pb.o app1.o
	$(CXX) $^ $(LDFLAGS) -o $@ 

%.grpc.pb.cc: %.proto
	protoc -I$(INCLUDES) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) ../include/dataproto.proto

%.pb.cc: %.proto
	protoc -I$(INCLUDES) --cpp_out=. ../include/dataproto.proto

clean:
	rm -f *.o ../include/*.pb.cc *.pb.h app1 dbg_log*
