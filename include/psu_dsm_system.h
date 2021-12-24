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

#include <sys/mman.h>
#include <ucontext.h>
#include <signal.h>
#include <errno.h>

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

using dataproto::DSM;
using dataproto::DSMRequest;
using dataproto::DSMReply;
using dataproto::DTRequest;
using dataproto::DTReply;
 
using namespace std;

//global variables
namespace dsm {
	constexpr int LISTENING_PORT = 46000;
	constexpr int ALTSTACK_SIZE  = 262144L; //64 pages
	bool connection_done = false;
	string gMyName;

	typedef enum operations{
		READ,
		WRITE
	} operations;

	typedef enum State{
        	INVALID,
        	READ_ONLY,
        	READ_WRITE
	}State;

	typedef enum transaction{
        	NA,
        	P_READ,
        	P_WRITE,
        	N_READ,
        	N_INVALID,
        	N_RdEx
	}trans;

	typedef struct entry{
        	vector<pair<string,State>> presence;
        	bool _lock;

        	entry(string name){
                	presence.push_back(make_pair(name,READ_WRITE));
                	_lock = false;
        	}

        	trans update_state (State &state, trans tx){ //update FSM based on slide 9
			cout<<" : Directory Table";
                	if(state == INVALID){
                        	if(tx == P_READ){
                                	state = READ_ONLY;
					cout<<"; changing state from I to RO"<<endl;
                                	return N_READ;
                        	}else if(tx == P_WRITE){
                                	state = READ_WRITE;
                                        cout<<": changing state from I to RW"<<endl;
                                	return N_INVALID; //same as N_RdEx to all;
                        	}
                	}else if(state == READ_ONLY){
                        	if(tx == N_INVALID){
                                	state = INVALID;
                                        cout<<": changing state from RO to I"<<endl;
                                	return NA;
                        	}else if(tx == P_WRITE){
                                	state = READ_WRITE;
                                        cout<<": changing state from RO to RW"<<endl;
                                	return N_INVALID;
                        	}
                	}else if(state == READ_WRITE){
                        	if(tx == N_READ){
                                	state = READ_ONLY;
                                        cout<<": changing state from RW to RO"<<endl;
                                	return NA;
                        	}else if(tx == N_INVALID){
                                	state = INVALID;
                                        cout<<": changing state from RW to I"<<endl;
                                	return NA;
                        	}
                	}
			cout<<": no change in state"<<endl;
        	}

        	bool isLock(){
                	return _lock;
        	}

        	bool lock(){
                	while(isLock()){
                        	cout<<"DT entry is locked..retrying in 5secs"<<endl;
                        	sleep(5);
                	}
                	_lock = true;
			cout<<"page locked"<<endl;
        	}

        	void unlock(){
                	_lock = false;
			cout<<"page unlocked"<<endl;
        	}

		string service_node(operations ops, string req_nodeid){
			State state_required;
			vector<pair<string,State>>::iterator found_nodeid;

			auto find_state = [&](const std::pair<std::string, State>& element)
			{
				return element.second == state_required;
			};

			auto return_search = [&](){
				auto it = std::find_if( presence.begin(), presence.end(), find_state);
		
				return it;
			};

                        //Find if any node is RW
                        state_required = READ_WRITE;
                        found_nodeid = return_search();

			if(found_nodeid == presence.end()){ 
				state_required = READ_ONLY; //Now try with READ_ONLY
				//cout<<"no RW sate node going with RO or any"<<endl;
				found_nodeid = return_search();
			}

			if(found_nodeid == presence.end()){
				//cout<<"going with any node at front()"<<endl;
				found_nodeid = presence.begin();
			}

			//cout<<found_nodeid->second<<endl;

			trans tx = (ops == READ) ? P_READ : P_WRITE;
                        update_entry(tx, req_nodeid); 			

			//cout<<found_nodeid->second<<endl;
			cout<<"directory node is sending request to "<<found_nodeid->first<<endl;
			return found_nodeid->first; 
		}	

        	void update_entry(trans tx, string name){
			//Update state of the node which caused the transition
			trans effect = NA;
			bool found = false;
                	for(auto node = presence.begin(); node != presence.end(); node++){
				if(name.compare(node->first) == 0){
					cout<<node->first;
					effect = update_state(node->second, tx);
					found = true;
					break;
				}
			}

			if(found == false)
				presence.push_back(make_pair(name, INVALID));

			//update state of the network nodes because of the effect from transition 
			for(auto node = presence.begin(); node != presence.end(); node++){
				if(name.compare(node->first) != 0){
					cout<<node->first;
					trans temp = update_state(node->second, effect);
				}
        		}
		}
	}entry;

	map<unsigned int, entry> directorytable; //IMPORTANT: should only be accessed by directory node
						 //(k,v) : (VirtualPage#, Entry)

	unsigned int extract_VPNum(unsigned int address)
	{
        	return address >> 12;
	}

        void set_mprotect(void* addr, size_t size, int protect_flag){
	    printf("setting %d as protect_flag for %p size %d\n", protect_flag, (char*)addr, size); 

	    int total_pages = ceil((float)size / 4096);

            //mprotect needs it page alligned so making it aligned 
	    unsigned long addr1 = (unsigned long)((unsigned long)((unsigned long)(char*)addr >> 12) << 12);

	    for(int i = 0; i < total_pages; i++){
		char* address = (char*)addr1 + i*4096;
		//printf("%p \n",address);
            	if(mprotect((void*)address, 4096, protect_flag)){
                	printf("mprotect failed %d", &protect_flag);
                	exit(-1);
            	}
	    }	   
        }

	class DSMImplementation final : public DSM::Service {
        	Status dsm_recv(ServerContext *context,
                           const DSMRequest* req,
                           DSMReply*  re) override {
                                //cout<<req->nodeid()<< "called "<<__func__<<"() grpc function at" <<gMyName<<endl;

				unsigned int VPnum = extract_VPNum(req->address());
				//cout<<"vpnum is "<<VPnum<<endl;
				auto it =directorytable.find(VPnum);
				if(it == directorytable.end()){
					perror("page not init");
					exit(-1);
				}

				cout<<__func__<<"() "<<req->nodeid()<<" trying to lock page number "<<VPnum<<endl;
				it->second.lock();
				re->add_data(it->second.service_node((operations)req->reqtype(), req->nodeid()));
                                return Status::OK;
                        }

		void update_mprotect(const DSMRequest* req){
			if(req->reqtype() == READ){
				set_mprotect((char*)req->address(), 4096, PROT_READ);
			}else if(req->reqtype() == WRITE){
				set_mprotect((char*)req->address(), 4096, PROT_NONE);
			}
		}

		Status  dsm_ninvalid_recv(ServerContext *context,
			const DSMRequest *req,
			DSMReply* re) override {

			update_mprotect(req);
			return Status::OK;
		}

		Status dsm_ack_recv(ServerContext *context,
                         const DSMRequest* req,
                           DSMReply*  re) override {
                                //cout<<req->nodeid()<< "called grpc function at" <<gMyName<<endl;

                                unsigned int VPnum = extract_VPNum(req->address());
                                //cout<<"vpnum is "<<VPnum<<endl;
                                auto it =directorytable.find(VPnum);
                                if(it == directorytable.end()){
                                        perror("page not init");
                                        exit(-1);
                                }

                                it->second.unlock();
				return Status::OK;
		}

                Status dsm_data_recv(ServerContext *context,
                        const DSMRequest* req,
                        DSMReply* re) override {

			//copy from start of page
			unsigned long addr1 = ((req->address() >> 12) << 12);

			//string data(req->address(), req->address() + 1);
			cout<<"sending data to "<<req->nodeid()<<endl;
			re->add_data((char*)addr1, 4096);

                        update_mprotect(req);
                        return Status::OK;
                }

		Status  dt_recv(ServerContext *context,
			const DTRequest* req,
			DTReply* re) override {

        			unsigned int vpnum = extract_VPNum(req->address());
        			unsigned int total_page = ceil((float)req->size() / 4096);

        			for(int i = 0; i < total_page; i++){
					entry temp(req->nodeid());
					cout<<__func__<<"() for "<<req->nodeid()<<" vp page: "<<vpnum+i<<endl;
					//entry doesn't exists
					if(directorytable.find((vpnum+i)) == directorytable.end()){
                				directorytable.insert({(vpnum+i), temp});
						re->set_state(READ_WRITE);
						cout<<" setting DT entry to RW state"<<endl;
					}
					else{
						auto it = directorytable.find(vpnum+i);
						it->second.update_entry(NA, req->nodeid());
						re->set_state(INVALID);
						cout<<" setting DT entry to Invalid state"<<endl;
					}
				}

				return Status::OK;
			}
	};

	class DSMClient {
        	public:
          	DSMClient(std::shared_ptr<Channel> channel, string name) : stub_(DSM::NewStub(channel)), node_name(name)  {}

          	DSMClient(const DSMClient &Client){
                	node_name = Client.node_name;
                	stub_ = Client.stub_;
          	}

		DSMClient(){} //Empty constructor for global variable

          	string send(operations ops, char* address){
                	DSMRequest req;
                	req.set_reqtype(ops);
                	req.set_nodeid(gMyName);
                	req.set_address((long int)address);

                	DSMReply re;

                	ClientContext context;

                	cout<<__func__<<"() "<<gMyName<<" is grpc calling "<<getName()<<" "<<std::hex<<req.address()<<endl;

                	while(1){
				//cout<<"calling stub"<<endl;
				sleep(5);
				//printf("stb address %p\n", &stub_);
                        	Status status = stub_->dsm_recv(&context, req, &re);

                        	if(status.ok()){
					return re.data()[0];
                        	}else{
                                	std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                        	}
				//sleep(5);
                	}
          	}

		//get 1page from the remote machine
		string get_data(operations ops, char* address){
			DSMRequest req;
			req.set_nodeid(gMyName);
			req.set_address((long int)address);
			req.set_reqtype(ops);
			DSMReply re;

			ClientContext context;
			cout<<__func__<<"() "<<gMyName<<"is grpc calling "<<getName()<<" "<<std::hex<<req.address()<<endl;

			while(1){
				Status status = stub_->dsm_data_recv(&context, req, &re);

				if(status.ok()){
					return re.data()[0];
				}else{
					std::cout << status.error_code() << ": " << status.error_message() << std::endl;
				}
				sleep(5);
			}
		}

		//incase of write network invalidate everyone
		void send_n_invalid(char * address){
                       DSMRequest req;
                        req.set_nodeid(gMyName);
                        req.set_address((long int)address);
                        req.set_reqtype(WRITE);
                        DSMReply re;

                        ClientContext context;
                        cout<<__func__<<"() "<<gMyName<<"is grpc calling"<<getName()<<" "<<std::hex<<req.address()<<endl;

                        while(1){
                                Status status = stub_->dsm_ninvalid_recv(&context, req, &re);

                                if(status.ok()){
                                        return;
                                }else{
                                        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                                }
				sleep(5);
                        }
		}

		//Send unlock to unlock the entry
		void send_ack(char *address){
                       DSMRequest req;
                       req.set_nodeid(gMyName);
                       req.set_address((long int)address);
                       req.set_reqtype(READ);
                       DSMReply re;

                       ClientContext context;
                       cout<<__func__<<"() "<<gMyName<<"is grpc calling"<<getName()<<" "<<std::hex<<req.address()<<endl;

                       while(1){
                               Status status = stub_->dsm_ack_recv(&context, req, &re);

                               if(status.ok()){
                                       return;
                               }else{
                                       std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                               }
				sleep(5);
                       }
		}

		State send_DTentry(unsigned long address, size_t size){
			DTRequest req;
			req.set_address(address);
			req.set_size(size);
			req.set_nodeid(gMyName);

			DTReply re;
			ClientContext context;

			cout<<__func__<<"() "<<gMyName<<" is grpc calling directory: "<<getName()<<" "<<std::hex<<req.address()<<endl;

                        while(1){
				sleep(5);
                                //printf("stb address %p\n", &stub_);
                                Status status = stub_->dt_recv(&context, req, &re);

                                if(status.ok()){
                                        return (State)re.state();
                                }else{
                                        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
                                }
				//sleep(5);
                        }
		} 

          	string getName(){
                	return node_name;
          	}

		//private:
          	std::shared_ptr<DSM::Stub> stub_;
          	string node_name;
	};

	vector<DSMClient> gNodeList;
	DSMClient directorynode;

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

                	in_addr* address = (in_addr* )record->h_addr;
                	string ip_address = inet_ntoa(* address);

	                ip_address += ":" + to_string(LISTENING_PORT);
        	        return ip_address;
        	};

        	auto init_grpcserver = [&] (){
                	DSMImplementation service;
                	auto address = getHostAddress(gMyName);

                	ServerBuilder builder;
                	builder.AddListeningPort(address, grpc::InsecureServerCredentials());
                	builder.RegisterService(&service);

                	std::unique_ptr<Server> server(builder.BuildAndStart());

                	while(1){ //server thread run should run forever?
                  		cout<<"server init thread...listening at "<<address<<endl;
                  		server->Wait();
                	}
        	};

       	 	auto itNodelist = [&] (){
                	std::ifstream ifs("node_list.txt");
                	std::string line, prev_line, address;

                	while(std::getline(ifs, line)){
				prev_line = line;
				address = getHostAddress(line);
				
                        	if(gMyName.compare(line) == 0)
                         		continue;

                        	DSMClient client(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()), line);
                        	cout<<"created channel for address "<<address<<endl;
                        	gNodeList.push_back(client);
                	}

			//last line as directory node
			cout<<"Directory node: "<<prev_line<<" "<<address<<endl;
			directorynode = DSMClient(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()), prev_line);
        	};

         	gMyName = getHostname();

        	//instantiate grpc server builder
        	thread init_thread(init_grpcserver);
        	init_thread.detach();

        	//iterate through node_list.txt and connect to other nodes 
        	itNodelist();
	}

};

using namespace dsm;

static void signal_handler(int signum, siginfo_t *info, void *contextptr)
{
    if (info) {
        ucontext_t *const ctx = (ucontext_t *const)contextptr;
	string found_nodeid;
	operations ops;
	char recv_data[4096];
	//cout<<sysconf(_SC_PAGESIZE)<<endl;
	//set_mprotect((void*)0x7ce000, 8192, PROT_READ|PROT_WRITE); //HACK: getting segv when allocating more than 1 page 

	auto copydata = [&](){
	    set_mprotect(info->si_addr, 4096, PROT_READ|PROT_WRITE);

            for(auto node : dsm::gNodeList){ //recieved node_name from DSM manager, getdata now!
                if(node.getName().compare(found_nodeid) == 0){
			//printf("recv_buffer : %p\n", recv_data);
                        memcpy(recv_data, node.get_data(ops, (char*)info->si_addr).c_str(), 4096);
			break;
		}
            }

            //mprotect needs it page alligned so making it aligned 
            unsigned long addr1 = (unsigned long)((unsigned long)((unsigned long)(char*)info->si_addr >> 12) << 12);

            memcpy((char*)addr1,recv_data,4096); 
	};	

        if (ctx->uc_mcontext.gregs[REG_ERR] & 2) {
            /* Write access */
            printf(": Invalid write attempt to %p\n", info->si_addr);

	    //send WRrequest to DSM manager; update P& N node state in DT; memcpy from RW node & set mprotect to RW 
	    //memcpy(recv_data, directorynode.send(WRITE, (char*)info->si_addr).c_str(), 4096);
	    ops = WRITE;
            found_nodeid = directorynode.send(ops, (char*)info->si_addr);

	    copydata();
 
	    //network invalidate everyone: set mprotect(PROT_NONE)
	    for(auto node : dsm::gNodeList)
			node.send_n_invalid((char*) info->si_addr);
        } else {
            /* Read access */
            printf(": Invalid read attempt from %p\n", info->si_addr);

	    //send RDrequest to DSM manager; update P& N node state in DT; memcpy from RW node & set mprotect to RO
	    //memcpy(recv_data, directorynode.send(READ, (char*)info->si_addr).c_str(), 4096);
	    ops = READ;	
	    found_nodeid = directorynode.send(ops, (char*)info->si_addr);

	    copydata();
            set_mprotect(info->si_addr, 4096, PROT_READ);
        }

	directorynode.send_ack((char*)info->si_addr);	
    }
}

static int install_signalhandler(){
    stack_t           altstack;
    struct sigaction  act;

    altstack.ss_size = ALTSTACK_SIZE;
    altstack.ss_flags = 0;
    altstack.ss_sp = mmap(NULL/*(void*)(1<<30)*/, altstack.ss_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, -1, 0);
    if (altstack.ss_sp == MAP_FAILED) {
        const int retval = errno;
        fprintf(stderr, "Cannot map memory for alternate stack: %s.\n", strerror(retval));
        return retval;
    }
    if (sigaltstack(&altstack, NULL)) {
        const int retval = errno;
        fprintf(stderr, "Cannot use alternate signal stack: %s.\n", strerror(retval));
        return retval;
    }

    memset(&act, 0, sizeof act);
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    act.sa_sigaction = signal_handler;
    if (sigaction(SIGSEGV, &act, NULL) == -1 ||
        sigaction(SIGBUS,  &act, NULL) == -1 ||
        sigaction(SIGILL,  &act, NULL) == -1 ||
        sigaction(SIGFPE,  &act, NULL) == -1) {
        const int retval = errno;
        fprintf(stderr, "Cannot install crash signal handlers: %s.\n", strerror(retval));
        return retval;
    }

    return 0;
}

void psu_dsm_malloc(char* VA, size_t size){
	//grpc model init
        if(dsm::connection_done == false){
                dsm::setup_grpc();
                dsm::connection_done = true;
        }

	//add entry to directory server
	State protect = directorynode.send_DTentry((unsigned long)(VA), size); 
	//Setting up the correct mprotect flag based on dsm node state for particular page(s)
	int protect_flag = -1;
	switch(protect){
		case INVALID:
			protect_flag = PROT_NONE;
			//cout<<"setting mprotect as None"<<endl;
			break;
		case READ_ONLY:
			protect_flag = PROT_READ;
			//cout<<"setting mprotect as RD"<<endl;
			break;
		case READ_WRITE:
			protect_flag = PROT_READ | PROT_WRITE;
			//cout<<"setting mprotect as RW"<<endl;
			break;
	};

	set_mprotect(VA, size, protect_flag);
 

	//register userdefined sigaction for invalid read/write address 
	if(install_signalhandler())
		exit(-1);
}

void psu_dsm_register_datasegment(void* psu_ds_start, size_t psu_ds_size){
	psu_dsm_malloc((char *) psu_ds_start, psu_ds_size);
}
