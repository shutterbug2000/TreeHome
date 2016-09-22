#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#define hangmacro() (\
{\
    puts("Press a key to exit...");\
    while(aptMainLoop())\
    {\
        hidScanInput();\
        if(hidKeysDown())\
        {\
            goto killswitch;\
        }\
        gspWaitForVBlank();\
    }\
})


#define ded(wat) *((u32*)0x00100000)=wat;
#define die() ded(0xDEADBEEF);



vu32 doit = 1;

static RecursiveLock srvLockHandle;

typedef void (*SignalHandler)(u32);
typedef struct
{
    u32 notificaton;
    SignalHandler func;
    void* next;
} SignalHook;

typedef struct
{
    u32 arg[0x10];
} SysMenuArg;

SysMenuArg menuarg;

static Handle srvSemaphore = 0;
static Thread srvThread = 0;
static SignalHook srvRootHook;

static SignalHook aptRootHook;

static Handle nwmHandle = 0;

void srvLock()
{
    Handle currhandle = *(((u32*)getThreadLocalStorage()) + 1);
    if(currhandle == srvLockHandle.thread_tag) return;
    RecursiveLock_Lock(&srvLockHandle);
}

void srvUnlock()
{
    Handle currhandle = *(((u32*)getThreadLocalStorage()) + 1);
    if(currhandle == srvLockHandle.thread_tag) return;
    RecursiveLock_Unlock(&srvLockHandle);
}

void srvHookSignal(u32 nid, SignalHandler func)
{
    if(!func) return;
    SignalHook* curr = &srvRootHook;
    while(curr->next) curr = curr->next;
    curr->next = malloc(sizeof(SignalHook));
    curr = curr->next;
    curr->next = 0;
    curr->notificaton = nid;
    curr->func = func;
}

void srvMainLoop(void* param)
{
    vu32* running = param;
    Result ret = 0;
    u32 NotificationID = 0;
    while(*running)
    {
        ret = svcWaitSynchronization(srvSemaphore, -1ULL);
        if(ret < 0) break;
        ret = srvReceiveNotification(&NotificationID);
        if(ret < 0) break;
        
        srvLock();
        SignalHook* curr = srvRootHook.next;
        while(curr)
        {
            if(curr->notificaton == NotificationID) curr->func(NotificationID);
            curr = curr->next;
        }
        srvUnlock();
    }
    if(*running) *(u32*)0x00100100 = ret;
}

Result nwmInit()
{
    return srvGetServiceHandle(&nwmHandle, "nwm::EXT");
}

Result nwmDisable(u8 flag)
{
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x80040;
    ipc[1] = flag != 0;
    Result ret = svcSendSyncRequest(nwmHandle);
    if(ret < 0) return ret;
    return ipc[1];
}

Result nwmExit()
{
    return svcCloseHandle(nwmHandle);
}

void aptHookSignal(u32 nid, SignalHandler func)
{
    if(!func) return;
    SignalHook* curr = &aptRootHook;
    while(curr->next) curr = curr->next;
    curr->next = malloc(sizeof(SignalHook));
    curr = curr->next;
    curr->next = 0;
    curr->notificaton = nid;
    curr->func = func;
}

int aptCallEvent(APT_Signal sig)
{
    SignalHook* curr = aptRootHook.next;
    while(curr)
    {
        if(curr->notificaton == sig) curr->func(sig);
        curr = curr->next;
    }
    return 0;
}

Result APT_CancelLibraryApplet(u8 exit)
{
    u32 ipc[16];
    ipc[0] = 0x3B0040;
    ipc[1] = exit;
    return aptSendCommand(ipc);
}

Result APT_GetProgramIdOnApplicationJump(u64* current, FS_MediaType* currtype, u64* target, FS_MediaType* targettype)
{
    u32 ipc[16];
    ipc[0] = 0x330000;
    Result ret = aptSendCommand(ipc);
    if(ret < 0) return ret;
    if(current) *current = *(u64*)(&ipc[2]);
    if(currtype) *currtype = ipc[4];
    if(target) *target = *(u64*)(&ipc[5]);
    if(targettype) *targettype = ipc[7];
    return ret;
}

Result APT_PrepareToStartApplication(u64 titleid, FS_MediaType media, u32 flags)
{
    u32 ipc[16];
    ipc[0] = 0x150140;
    *(u64*)(&ipc[1]) = titleid;
    ipc[3] = media;
    ipc[4] = 0;
    ipc[5] = flags;
    return aptSendCommand(ipc);
}

Result APT_StartApplication(u8* param, size_t sizeofparam, u8* hmac, size_t sizeofhmac, u8 paused)
{
    u32 ipc[16];
    ipc[0] = 0x1B00C4;
    ipc[1] = sizeofparam;
    ipc[2] = sizeofhmac;
    ipc[3] = paused;
    ipc[4] = (sizeofparam << 14) | 2;
    ipc[5] = param;
    ipc[6] = (sizeofhmac << 14) | 0x802;
    ipc[7] = hmac;
    return aptSendCommand(ipc);
}

Result APT_LoadSysMenuArg(SysMenuArg* buf)
{
    u32 size = 0x40;//sizeof(SysMenuArg);
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x360040;
    ipc[1] = size;
    
    ipc[0x40] = size << 14 | 2;
    ipc[0x41] = buf;
    
    Result ret = aptSendSyncRequest();
    if(ret < 0) return ret;
    return ipc[1];
}

Result APT_StoreSysMenuArg(SysMenuArg* buf)
{
    u32 size = 0x40;
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x370040;
    ipc[1] = size;
    ipc[2] = size << 14 | 2;
    ipc[3] = buf;
    
    Result ret = aptSendSyncRequest();
    if(ret < 0) return ret;
    return ipc[1];
}

void __appInit(void)
{
    Result res = 0;
    if((res = srvInit()) < 0) ded(res);
    RecursiveLock_Init(&srvLockHandle);
    //if((res = srvEnableNotification(&srvSemaphore)) < 0) ded(res);
    //srvThread = threadCreate(srvMainLoop, &doit, 0x1000, 0x18, -2, 0);
    
    if((res = nsInit()) < 0) ded(res);
    if((res = ptmSysmInit()) < 0) ded(res);
    if((res = psInit()) < 0) ded(res);
    if((res = aptInit(0x300, 1, 0, 0)) < 0) ded(res);
    aptExit();
    if((res = aptInit(0x103, 0, 3, 2)) < 0) ded(res);

    if((res = NS_LaunchTitle(0x0004013000001C02, 0, NULL)) < 0) ded(res);//== 0xC8A12402) die();

    //if((res = gspInit()) < 0) ded(res);
    //GSPGPU_SetLcdForceBlack(0);
    //GSPGPU_AcquireRight(0);

    if((res = NS_LaunchTitle(0x0004013000001802, 0, NULL)) < 0) ded(res);//== 0xC8A12402) die();
    if((res = NS_LaunchTitle(0x0004013000001D02, 0, NULL)) < 0) ded(res);//== 0xC8A12402) die();
    if((res = NS_LaunchTitle(0x0004013000001A02, 0, NULL)) < 0) ded(res);//== 0xC8A12402) die();
    if((res = NS_LaunchTitle(0x0004013000001502, 0, NULL)) < 0) ded(res);//== 0xC8A12402) die();

    hidInit();
    
    fsInit();
    sdmcInit();
}

void __appExit(void)
{
    sdmcExit();
    fsExit();

    hidExit();

    //GSPGPU_ReleaseRight();
    //gspExit();

    aptExit();
    psExit();
    nsExit();
    srvExit();
}



int main()
{
  // =====[PROGINIT]=====
  
  gfxInit(GSP_RGBA8_OES, GSP_RGBA8_OES, false);
  //die();
  //extern u32 __ctru_linear_heap;
  //*(u32*)0x00100099 =  __ctru_linear_heap;
  consoleInit(GFX_BOTTOM, NULL);
  
  puts("Initializing SysMenu stuff");
  
  memset(&menuarg, 0, sizeof(menuarg));
  
  /*
  if(APT_LoadSysMenuArg(&menuarg) < 0)
  {
      
  }*/
  
  //puts("Storing SysArg");
  //APT_StoreSysMenuArg(&menuarg);
  
  puts("wat");

  // =====[VARS]=====
  
  u32 kDown;
  u32 kHeld;
  u32* fbBottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
  u16 seed = 0;
  
  // =====[PREINIT]=====
  
  gspWaitForVBlank();
  gspWaitForVBlank();
  gspWaitForVBlank();

  
  //puts("Initializing nwm");
  //Result ret = nwmInit();
  //printf("NWM::Init %08X\n", ret);
  //puts("Initializing WiFi");
  //ret = nwmDisable(0);
  //printf("NWM::Disable %08X\n", ret);
  
  // =====[RUN]=====
  
  while (aptMainLoop())
  {
    hidScanInput();
    kDown = hidKeysDown();
    kHeld = hidKeysHeld();
    
    if (kHeld & KEY_SELECT)
    {
        *(u32*)0x00106800 = 0xDEADCAFE;
        break;
    }
    
    if(kDown & KEY_START)
    {
        Result res = 0;
        //u8 tst = 0;
        
        //puts("GetProgramIdOnAppJump");
        
        //u64 launchwat = 0;
        //FS_MediaType wattype = 0;
        
        //res = APT_GetProgramIdOnApplicationJump(NULL, NULL, &launchwat, &wattype);
        //printf("Result %08X %016LLX %i\n", res, launchwat, wattype);
        
        /*
        puts("CheckApp");
        res = APT_IsRegistered(0x400, &tst);
        printf("CheckApp result %08X %c\n", res, tst ? '+' : '-');
        if(tst)
        {
            puts("CheckApplet");
            res = APT_IsRegistered(0x200, &tst);
            printf("CheckApplet result %08X %c\n", res, tst ? '+' : '-');
            if(tst)
            {
                puts("CancelLibraryApplet");
                res = APT_CancelLibraryApplet(0);
                printf("CancelLibraryApplet result %08X\n", res);
            }
        }*/
        
        puts("PrepareToStartApp");
        //res = APT_PrepareToStartApplication(0x000400000F800100L, MEDIATYPE_SD, 1);
        res = APT_PrepareToStartApplication(0x0004000000030700L, MEDIATYPE_SD, 0);
        if(res < 0)
        {
            printf("Fail %08X\n", res);
        }
        else
        {
            u8 hmac[0x20];
            u8 param[0x300];
            
            //memset(hmac, 0, sizeof(hmac));
            //memset(param, 0, sizeof(param));
            
            puts("Looping");
            do
            {
                res = APT_StartApplication(/*param*/ 0, sizeof(param), /*hmac*/ 0, sizeof(hmac), 0);
                printf("Result %08X\n", res);
            }
            while(res == 0xC8A0CFF0 || res == 0xE0A0CC08 || res == 0xC8A0CC02);
            puts("Loop ended");
        }
    }
    
    if(kDown & KEY_B)
    {
        NS_LaunchTitle(0, 0, NULL);
    }
    
    fbBottom[seed++] = 0xF00FCACE;
    
    //TODO implement
    
    gfxFlushBuffers();
    //gfxSwapBuffers();
    gspWaitForVBlank();
  }

  // =====[END]=====
  
  killswitch:
  
  gfxExit();

  return 0;
}
