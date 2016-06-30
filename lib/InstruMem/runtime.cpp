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
 	//int invoke = 0;
	

  public:
	void load(uint64_t id, char* ptr);
	void store(uint64_t id, char* ptr);
 	//void ProcessLoad(int invoke, uint64_t id, char* ptr, char* val, uint64_t size);
} W;











void __attribute__ ((noinline)) Wrapper::load(uint64_t id, char* ptr)
{
    //printf("id = %ld, addr = 0x%x", id, ptr);
    asm("");
#if 0
	//cout << "load instruction with id " << id << ", size of " << size << " and value of " << (uint32_t)val << " and address of " << (void*)ptr << "\n";
	char* arr = new char[8];
	memcpy((void*)arr, (void*)&val, 8);
	ProcessLoad(invoke, id, ptr, arr, size);
#endif	
}
void __attribute__ ((noinline))Wrapper::store(uint64_t id, char* ptr)
{
    //printf("id = %ld, addr = 0x%x", id, ptr);
    asm("");
#if 0
	//cout << "load instruction with id " << id << ", size of " << size << " and value of " << (uint32_t)val << " and address of " << (void*)ptr << "\n";
	char* arr = new char[8];
	memcpy((void*)arr, (void*)&val, 8);
	ProcessLoad(invoke, id, ptr, arr, size);
#endif	
}

#if 0
void Wrapper::ProcessLoad(int invoke, uint64_t id, char* ptr, char* val, uint64_t size) {

	cout << invoke << "\n";
}//ProcessLoad
#endif





extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. DEP(load) yields InstruMem_load
#define DEP(X)  __InstruMem_ ## X





void
DEP(load)(uint64_t id, char* ptr) {
    W.load(id, ptr);
//  W.FP2load(id, ptr, val, size);
}

void
DEP(store)(uint64_t id, char* ptr) {
    W.store(id, ptr);
//  W.FP2load(id, ptr, val, size);
}





}
