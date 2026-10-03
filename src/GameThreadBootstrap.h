// A thread-specific message hook marshals one-time setup to Unity's window thread.
// The worker never invokes Unity, attaches to IL2CPP, patches code, or loads other mods.
static void* p78_messageHook=nullptr;
static i64 (*p78_nextHook)(void*,int,uptr,i64)=nullptr;
static bool p78_bootReady=false,p78_bootBusy=false,p78_bootDone=false;
static i64 p78_on_message(int code,uptr w,i64 l){
    i64 result=p78_nextHook?p78_nextHook(nullptr,code,w,l):0;
    if(code<0||!__atomic_load_n(&p78_bootReady,__ATOMIC_ACQUIRE)||!p78_main())return result;
    bool expected=false;
    if(!__atomic_compare_exchange_n(&p78_bootBusy,&expected,true,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return result;
    static u64 last=0,lastUi=0;u64 now=p78_clock();
    if(__atomic_load_n(&p78_bootDone,__ATOMIC_ACQUIRE)){
        // A private Day & Night Cycle message-tick route. No game method is
        // detoured and no external mod hook is used.
        if(now-lastUi>=33){lastUi=now;P78WorkScope work;p93_calendar_control_tick();p91_hold_lighting_tick();}
        if(now-last>=150){last=now;P78WorkScope work;p91_exterior_light_tick();}
        __atomic_store_n(&p78_bootBusy,false,__ATOMIC_RELEASE);return result;
    }
    if(now-last<1000){__atomic_store_n(&p78_bootBusy,false,__ATOMIC_RELEASE);return result;}last=now;
    if(init_il2cpp()&&il2cpp_domain_get()){
        if(init_unity_methods()){
            P78WorkScope work;
            // Day & Night Cycle deliberately avoids game-method detours and
            // uses its private message tick above.
            p93_calendar_control_tick();p91_exterior_light_tick();
            log_raw("DAY & NIGHT CYCLE: independent message-tick runtime active.\r\n");
            __atomic_store_n(&p78_bootDone,true,__ATOMIC_RELEASE);
        }
    }
    __atomic_store_n(&p78_bootBusy,false,__ATOMIC_RELEASE);return result;
}
static HWND p78_window=nullptr;
static BOOL p78_find_window(HWND window,i64){
    DWORD process=0;char name[80]{};DWORD thread=p70_windowProcess(window,&process);
    if(thread&&process==p70_processId()&&p70_className(window,name,sizeof(name))&&streq(name,"UnityWndClass")){
        p78_window=window;p78_mainThread=thread;return 0;
    }return 1;
}
static DWORD worker(void*){
    if(!init_winapi())return 0;
    init_log_path();p78_init_logging();
    void* k=find_loaded_module("kernel32.dll");void* u=find_loaded_module("user32.dll");
    p78_threadId=(decltype(p78_threadId))resolve_export(k,"GetCurrentThreadId");
    p78_clock=(decltype(p78_clock))resolve_export(k,"GetTickCount64");
    auto enumerate=(BOOL(*)(BOOL(*)(HWND,i64),i64))resolve_export(u,"EnumWindows");
    auto hook=(void*(*)(int,i64(*)(int,uptr,i64),HMODULE,DWORD))resolve_export(u,"SetWindowsHookExA");
    auto post=(BOOL(*)(HWND,unsigned,uptr,i64))resolve_export(u,"PostMessageA");
    p78_nextHook=(decltype(p78_nextHook))resolve_export(u,"CallNextHookEx");
    if(!p78_threadId||!p78_clock||!enumerate||!hook||!post||!p78_nextHook||!p70_windowProcess||!p70_processId||!p70_className){log_raw("DAY & NIGHT CYCLE ERROR: message-dispatch API missing.\r\n");return 0;}
    log_raw("Day & Night Cycle v1.0 runtime starting.\r\n");
    pSleep(15000);
    for(int i=0;i<120&&!p78_window;++i){enumerate(p78_find_window,0);if(!p78_window)pSleep(1000);}
    if(!p78_window){log_raw("DAY & NIGHT CYCLE ERROR: Unity window not found within timeout.\r\n");return 0;}
    p78_messageHook=hook(3,p78_on_message,nullptr,p78_mainThread); // WH_GETMESSAGE
    if(!p78_messageHook){log_raw("DAY & NIGHT CYCLE ERROR: thread message hook failed.\r\n");return 0;}
    __atomic_store_n(&p78_bootReady,true,__ATOMIC_RELEASE);
    for(int i=0;i<600&&!__atomic_load_n(&p78_bootDone,__ATOMIC_ACQUIRE);++i){post(p78_window,0,0,0);pSleep(100);}
    if(!__atomic_load_n(&p78_bootDone,__ATOMIC_ACQUIRE)){__atomic_store_n(&p78_bootReady,false,__ATOMIC_RELEASE);log_raw("DAY & NIGHT CYCLE ERROR: setup timeout; message tick disabled.\r\n");return 0;}
    log_raw("DAY & NIGHT CYCLE: independent runtime ready.\r\n");
    // Keep posting harmless WM_NULL messages so Unity's own window thread
    // performs the lighting tick.  The DLL is process-lifetime loaded.
    for(;;){post(p78_window,0,0,0);pSleep(33);}
    return 0;
}
extern "C" BOOL DllMain(HMODULE,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){void* k=find_loaded_module("kernel32.dll");
        auto create=k?(CreateThread_t)resolve_export(k,"CreateThread"):nullptr;
        auto close=k?(CloseHandle_t)resolve_export(k,"CloseHandle"):nullptr;
        if(create&&close){DWORD tid=0;HANDLE h=create(nullptr,0,worker,nullptr,0,&tid);if(h)close(h);}
    }return 1;
}
