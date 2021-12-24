#ifndef DBG_H
#define DBG_H
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

std::string node_name = std::getenv("HOSTNAME");
auto filename = "dbg_log_"+node_name+".txt";
std::ofstream out(filename, std::ios_base::out);
auto coutbuf = std::cout.rdbuf(out.rdbuf()); //save and redirect
#endif
