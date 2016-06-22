#include <cstdio>
#include <cstdint>
#include <stack>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <ctime>
#include <cstring>

using namespace std;

class Wrapper
{
  private:
 	int invoke = 0;
	

  public:
	void load(uint64_t id, char* ptr, uint64_t val, uint64_t size);
	void ProcessLoad(int invoke, uint64_t id, char* ptr, char* val, uint64_t size);
} W;











void Wrapper::load(uint64_t id, char* ptr, uint64_t val, uint64_t size)
{
	//cout << "load instruction with id " << id << ", size of " << size << " and value of " << (uint32_t)val << " and address of " << (void*)ptr << "\n";
	char* arr = new char[8];
	memcpy((void*)arr, (void*)&val, 8);
	ProcessLoad(invoke, id, ptr, arr, size);
	
}


void Wrapper::ProcessLoad(int invoke, uint64_t id, char* ptr, char* val, uint64_t size) {

	cout << invoke << "\n";
}//ProcessLoad






extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. DEP(load) yields InstruMem_load
#define DEP(X)  InstruMem_ ## X





void
DEP(load)(uint64_t id, char* ptr, uint64_t val, uint64_t size) {
  W.FP2load(id, ptr, val, size);
}






}
