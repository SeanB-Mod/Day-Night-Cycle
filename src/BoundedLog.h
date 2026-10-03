// Bounded per-process log with one previous file; logging never spins or holds a file open.
static unsigned p78_logLock=0;
static DWORD p78_logBytes=0;
static DWORD (*p78_fileSize)(HANDLE,DWORD*)=nullptr;
static BOOL (*p78_moveLog)(LPCWSTR,LPCWSTR,DWORD)=nullptr;
static void p78_init_logging(){
    void* k=find_loaded_module("kernel32.dll");
    p78_fileSize=(decltype(p78_fileSize))resolve_export(k,"GetFileSize");
    p78_moveLog=(decltype(p78_moveLog))resolve_export(k,"MoveFileExW");
    HANDLE h=pCreateFileW(g_logPath,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h&&h!=INVALID_HANDLE_VALUE){if(p78_fileSize){DWORD size=p78_fileSize(h,nullptr);if(size!=0xffffffffu)p78_logBytes=size;}pCloseHandle(h);}
}
static void log_raw(const char* s){
    if(!s||!pCreateFileW)return;
    unsigned expected=0;if(!__atomic_compare_exchange_n(&p78_logLock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
    DWORD bytes=(DWORD)slen(s);
    if(p78_logBytes+bytes>2u*1024u*1024u){
        wchar_t previous[640];wcopy(previous,sizeof(previous)/sizeof(previous[0]),g_logPath);wappend(previous,sizeof(previous)/sizeof(previous[0]),L".previous");
        if(!p78_moveLog||!p78_moveLog(g_logPath,previous,1)){__atomic_store_n(&p78_logLock,0,__ATOMIC_RELEASE);return;}
        p78_logBytes=0;
    }
    HANDLE h=pCreateFileW(g_logPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h&&h!=INVALID_HANDLE_VALUE){pSetFilePointer(h,0,nullptr,FILE_END);DWORD written=0;pWriteFile(h,s,bytes,&written,nullptr);p78_logBytes+=written;pCloseHandle(h);}
    __atomic_store_n(&p78_logLock,0,__ATOMIC_RELEASE);
}
