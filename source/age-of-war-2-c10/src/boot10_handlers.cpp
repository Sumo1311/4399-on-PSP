#include <new>
#include <exception>
#include <pspkernel.h>
extern "C" void boot10_record(const char*);
namespace {
[[noreturn]] void noMemory(){boot10_record("ERROR allocation failed");sceKernelExitGame();for(;;)sceKernelDelayThread(100000);}
[[noreturn]] void terminated(){boot10_record("ERROR C++ termination");sceKernelExitGame();for(;;)sceKernelDelayThread(100000);}
}
extern "C" void boot10_install_handlers(){std::set_new_handler(noMemory);std::set_terminate(terminated);}
