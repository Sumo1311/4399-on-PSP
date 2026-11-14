#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <stdio.h>
#include <string.h>

// These POD globals need no C++ constructors. The CRT calls the wrapped
// libc hook before _init(), so startup is observable before game globals.
static char boot_path[256] __attribute__((aligned(64)));
static char boot_line[1024] __attribute__((aligned(64)));
static int libc_ready=0,screen_ready=0,screen_active=1;
static unsigned heap_capacity=0;
extern int sce_newlib_heap_kb_size;
unsigned boot10_heap_capacity(void){return heap_capacity;}
static void write_hex(char* out,unsigned value){const char* h="0123456789abcdef";for(int i=0;i<8;++i)out[i]=h[(value>>(28-i*4))&15];}
extern void __real___libcglue_init(int argc,char** argv);
extern void __real__init(void);
extern void boot10_install_handlers(void);

void boot10_record(const char* stage){
 int length=0;while(stage[length]&&length<(int)sizeof(boot_line)-1){boot_line[length]=stage[length];++length;}boot_line[length]=0;
 int fd=boot_path[0]?sceIoOpen(boot_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666):-1;
 if(fd<0)fd=sceIoOpen("ms0:/BOOT10.LOG",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
 if(fd>=0){sceIoWrite(fd,boot_line,length);sceIoWrite(fd,"\n",1);sceIoClose(fd);}
 else if(libc_ready){FILE* f=fopen("BOOT10.LOG","ab");if(f){fwrite(stage,1,length,f);fwrite("\n",1,1,f);fclose(f);}}
 if(screen_ready&&screen_active){pspDebugScreenSetXY(0,3);pspDebugScreenPuts("                                                            ");pspDebugScreenSetXY(0,3);pspDebugScreenPuts(stage);}
}
void boot10_release_display(void){screen_active=0;}
void __wrap___libcglue_init(int argc,char** argv){
 if(argc>0&&argv&&argv[0]){
  const char* in=argv[0];int i=0,last=-1;
  while(in[i]&&i<220){boot_path[i]=in[i];if(in[i]=='/')last=i;++i;}
  if(last>=0){const char* name="BOOT10.LOG";int n=0;while(name[n]){boot_path[last+1+n]=name[n];++n;}boot_path[last+1+n]=0;}else boot_path[0]=0;
 }
 pspDebugScreenInit();screen_ready=1;
 pspDebugScreenPuts("WAR EVOLUTION 2 - STARTUP MEMORY ADAPTATION 10\n\n");
 unsigned maximum=sceKernelMaxFreeMemSize();
 heap_capacity=maximum>1024u*1024?((maximum-1024u*1024)&~1023u):128u*1024;
 sce_newlib_heap_kb_size=(int)(heap_capacity/1024);
 char limits[]="heap maximum=0x00000000 selected=0x00000000";
 write_hex(limits+15,maximum);write_hex(limits+35,heap_capacity);
 boot10_record(limits);
 boot10_record("01 libc setup");
 __real___libcglue_init(argc,argv);libc_ready=1;
 boot10_record("02 libc ready");boot10_install_handlers();
}
void __wrap__init(void){
 boot10_record("03 C++ initialization");
 __real__init();
 boot10_record("04 C++ ready");
}
