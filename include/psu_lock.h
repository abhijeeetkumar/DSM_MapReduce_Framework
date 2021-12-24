#include <string>
#include <thread> 
#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <utility>

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <netdb.h>
#include <arpa/inet.h>

#include <grpcpp/grpcpp.h> 
#include "dataproto.grpc.pb.h"
#include "dbg_log.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using dataproto::DistMutex;
using dataproto::MessageRequest;
using dataproto::MessageReply;

using namespace std;

//global variables
constexpr int LISTENING_PORT = 45000;
bool connection_done = false;
map<unsigned int, bool> gLock;  //(k,v) : (lockno, isAcquired)
map<unsigned int, int> gMySeqNum,  gHighestSeqNum; //(k,v) : (lockno, #)
string gMyName;
map<unsigned int, vector<string>> gDeferredReply; //(lockno, deferred_node_list)
map<unsigned int, vector<string>> gRecievedReply; //(lockno, replied_node_list)

typedef enum Message
{
        CS_REQUEST,
        CS_REPLY,
        CS_COMPLETION
}Message;


class DistMutexImplementation final : public DistMutex::Service {
	Status recv(ServerContext *context,
			   const MessageRequest* req,
			   MessageReply*  re) override {
                                //cout<<__func__<<"()  "<<req->nodeid()<< "called grpc function at" <<::gMyName<<endl;
				if(req->msgtype() == CS_REPLY){
					gRecievedReply[req->lockno()].push_back(req->nodeid());
					return Status::OK;
				}
				gHighestSeqNum[req->lockno()] = max(req->seqid(), gHighestSeqNum[req->lockno()]);

				if((gLock[req->lockno()]) &&
				   ((req->seqid() > gMySeqNum[req->lockno()]) ||
				   ((req->seqid() == gMySeqNum[req->lockno()]) && (req->nodeid().compare(::gMyName) > 0))))
					gDeferredReply[req->lockno()].push_back(req->nodeid());
				else
					re->set_result(CS_REPLY);

				return Status::OK;
			}
};

class DistMutexClient {
	public:
	  DistMutexClient(std::shared_ptr<Channel> channel, string name) : stub_(DistMutex::NewStub(channel)), node_name(name)  {}

	  DistMutexClient(const DistMutexClient &Client){
		node_name = Client.node_name;
		stub_ = Client.stub_;
	  }

	  Message send(Message msg, unsigned int lockno){
		MessageRequest req;
		req.set_seqid(gMySeqNum[lockno]);
		req.set_msgtype(msg);
		req.set_nodeid(::gMyName);
		req.set_lockno(lockno);

		MessageReply re;

		ClientContext context;    

		cout<<__func__<<"() "<<::gMyName<<" is calling grpc at "<<getName()<<endl;

		while(1){
			sleep(5);
			Status status = stub_->recv(&context, req, &re);

			if(status.ok()){
				return (Message)re.result();
			}else{
				std::cout << status.error_code() << ": " << status.error_message() << std::endl;
			} 
	  	}	
	  }

	  string getName(){
		return node_name;
	  }

	//private:
	  std::shared_ptr<DistMutex::Stub> stub_;
	  string node_name;
};

vector<DistMutexClient> gNodeList;

void setup_grpc(){
	auto getHostname = [&] (){
		char name[256];
		auto result = gethostname(name, sizeof(name));
		if (result) {
			perror("gethostname error");
			exit(1);
		}
		cout<<"Running on :"<<name<<endl; 
		return std::string(name);
	};

	auto getHostAddress = [&] (string name){
		hostent * record = gethostbyname(name.c_str());
		if(record == NULL)
		{
			printf("%s is unavailable\n", name.c_str());
			exit(1);
		}

		in_addr * address = (in_addr * )record->h_addr;
		string ip_address = inet_ntoa(* address);

                ip_address += ":" + to_string(::LISTENING_PORT);
		return ip_address;
	};

	auto init_grpcserver = [&] (){
		DistMutexImplementation service;
		auto address = getHostAddress(::gMyName);

		ServerBuilder builder;
		builder.AddListeningPort(address, grpc::InsecureServerCredentials());
                builder.RegisterService(&service);

		std::unique_ptr<Server> server(builder.BuildAndStart());

		while(1){ //server thread run should run forever?
		  cout<<"server init thread...listening"<<endl;
		  server->Wait();
		}
	}; 

	auto itNodelist = [&] (){
		std::ifstream ifs("node_list.txt");
		std::string line;

		while(std::getline(ifs, line)){
			if(::gMyName.compare(line) == 0)
			 continue;

			auto address = getHostAddress(line);
			DistMutexClient client(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()), line);
			cout<<"created channel for address "<<line.c_str()<<endl;
			::gNodeList.push_back(client);
		}
	};

	 ::gMyName = getHostname();

        //instantiate grpc server builder
        thread init_thread(init_grpcserver); 
	init_thread.detach();

        //iterate through node_list.txt and connect to other nodes 
        itNodelist();
}

void psu_mutex_init(unsigned int lockno){
	//If grpc server and client is not running then setup in each node
	if(::connection_done == false){
		::setup_grpc();
		::connection_done = true;
	}

	//Create lock
	gLock.insert(pair<unsigned int, bool>(lockno, false));
	cout<<"lock " <<lockno<<" created"<<endl;
}

void psu_mutex_lock(unsigned int lockno){
	gLock[lockno] = true;
	gMySeqNum[lockno] = gHighestSeqNum[lockno] + 1;

	//send CS_REQUEST to everyone
	for(auto client : ::gNodeList){
		Message response = client.send(CS_REQUEST, lockno);
		
		if(response == CS_REPLY)
			gRecievedReply[lockno].push_back(client.getName());	
	}

	//wait for replies from everyone
	cout<<"Waiting for replies from everyone"<<endl;
	while(1){
		usleep(1000); //wait for 1s
		if(::gNodeList.size() == gRecievedReply[lockno].size())
			break;
	}

	gRecievedReply[lockno].clear(); //lock acquired clear all entries 
}

void psu_mutex_unlock(unsigned int lockno){
	gLock[lockno] = false; //TODO: shouldnt be at the eof ?

	//Send deferred replies
	for(auto node : gDeferredReply[lockno]){
		cout<<"sending deferred replies"<<endl;
		for(auto client : ::gNodeList){
			if(client.getName() == node){
				Message response = client.send(CS_REPLY, lockno);
			}
		}
	}

	gDeferredReply[lockno].clear(); //lock released clear all entries
}
