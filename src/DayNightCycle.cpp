// Day & Night Cycle for Two Point Museum.
// Unity work is dispatched to the game thread; preferences live under Mods/Saves.
// This DLL is self-contained and does not call into any other mod.

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using i32 = int;
using i64 = long long;
using uptr = unsigned long long;
using usize = unsigned long long;
using BOOL = int;
using DWORD = unsigned long;
using HANDLE = void*;
using HMODULE = void*;
using FARPROC = void*;
using LPVOID = void*;
using LPCSTR = const char*;
using LPSTR = char*;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using HWND = void*;

struct POINT_X { long x; long y; };
struct RECT_X { long left; long top; long right; long bottom; };

extern "C" int _fltused = 0;

extern "C" void* memcpy(void* dst, const void* src, usize n){ u8* d=(u8*)dst; const u8* q=(const u8*)src; for(usize i=0;i<n;++i)d[i]=q[i]; return dst; }
extern "C" void* memset(void* dst, int v, usize n){ u8* d=(u8*)dst; for(usize i=0;i<n;++i)d[i]=(u8)v; return dst; }

#define DLL_PROCESS_ATTACH 1
#define GENERIC_WRITE 0x40000000u
#define GENERIC_READ  0x80000000u
#define FILE_SHARE_READ 0x00000001u
#define CREATE_ALWAYS 2u
#define OPEN_ALWAYS 4u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_END 2u
#define INVALID_HANDLE_VALUE ((HANDLE)(uptr)-1)

static usize slen(const char* s){ usize n=0; if(!s) return 0; while(s[n]) ++n; return n; }
static char lowerc(char c){ return (c>='A'&&c<='Z') ? (char)(c+('a'-'A')) : c; }
static bool streq(const char* a,const char* b){ if(!a||!b) return false; while(*a&&*b){ if(*a!=*b) return false; ++a;++b;} return *a==0&&*b==0; }
static bool contains_i(const char* h,const char* n){
    if(!h||!n||!*n) return false;
    for(usize i=0;h[i];++i){ usize j=0; while(n[j]&&h[i+j]&&lowerc(h[i+j])==lowerc(n[j])) ++j; if(!n[j]) return true; }
    return false;
}
static void scopy(char* d, usize cap, const char* s){ if(!d||cap==0) return; usize i=0; if(s){ for(;i+1<cap&&s[i];++i)d[i]=s[i]; } d[i]=0; }
static void sappend(char* d, usize cap, const char* s){ usize n=slen(d); if(n>=cap) return; usize i=0; if(s){ for(;n+i+1<cap&&s[i];++i)d[n+i]=s[i]; } d[n+i]=0; }
static usize wlen(const wchar_t* s){usize n=0;if(!s)return 0;while(s[n])++n;return n;}
static void wcopy(wchar_t* d,usize cap,const wchar_t* s){if(!d||!cap)return;usize i=0;if(s)for(;i+1<cap&&s[i];++i)d[i]=s[i];d[i]=0;}
static void wappend(wchar_t* d,usize cap,const wchar_t* s){usize n=wlen(d);if(n>=cap)return;usize i=0;if(s)for(;n+i+1<cap&&s[i];++i)d[n+i]=s[i];d[n+i]=0;}
static void wappend_ascii(wchar_t* d,usize cap,const char* s){usize n=wlen(d);if(n>=cap)return;usize i=0;if(s)for(;n+i+1<cap&&s[i];++i)d[n+i]=(u8)s[i];d[n+i]=0;}

static void* get_peb(){ void* p; __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(p)); return p; }

struct LIST_ENTRY_X { void* Flink; void* Blink; };
struct UNICODE_STRING_X { u16 Length; u16 MaximumLength; wchar_t* Buffer; };

static bool wide_base_eq(const UNICODE_STRING_X* us, const char* ascii){
    if(!us||!us->Buffer||!ascii) return false;
    usize an=slen(ascii), wn=(usize)us->Length/2; if(an!=wn) return false;
    for(usize i=0;i<an;++i){ wchar_t wc=us->Buffer[i]; char ac=ascii[i]; wchar_t wl=(wc>='A'&&wc<='Z')?wc+32:wc; char al=lowerc(ac); if((wchar_t)al!=wl) return false; }
    return true;
}

static void* find_loaded_module(const char* name){
    u8* peb=(u8*)get_peb(); if(!peb) return nullptr;
    u8* ldr=*(u8**)(peb+0x18); if(!ldr) return nullptr;
    LIST_ENTRY_X* head=(LIST_ENTRY_X*)(ldr+0x20); LIST_ENTRY_X* cur=(LIST_ENTRY_X*)head->Flink;
    for(int guard=0;cur && cur!=head && guard<256; ++guard){
        u8* ent=(u8*)cur-0x10; void* base=*(void**)(ent+0x30); UNICODE_STRING_X* bn=(UNICODE_STRING_X*)(ent+0x58);
        if(base && wide_base_eq(bn,name)) return base;
        cur=(LIST_ENTRY_X*)cur->Flink;
    }
    return nullptr;
}

static void* resolve_export_raw(void* mod, const char* name, u32* outRva=nullptr, u32* outExpRva=nullptr, u32* outExpSize=nullptr){
    if(!mod||!name) return nullptr; u8* b=(u8*)mod;
    u32 pe=*(u32*)(b+0x3c); if(*(u32*)(b+pe)!=0x00004550u) return nullptr;
    u16 magic=*(u16*)(b+pe+24); if(magic!=0x20b) return nullptr;
    u32 expRva=*(u32*)(b+pe+24+0x70); u32 expSize=*(u32*)(b+pe+24+0x74); if(!expRva) return nullptr;
    u8* e=b+expRva; u32 nNames=*(u32*)(e+0x18); u32 funcsRva=*(u32*)(e+0x1c); u32 namesRva=*(u32*)(e+0x20); u32 ordsRva=*(u32*)(e+0x24);
    u32* funcs=(u32*)(b+funcsRva); u32* names=(u32*)(b+namesRva); u16* ords=(u16*)(b+ordsRva);
    for(u32 i=0;i<nNames;++i){ const char* nm=(const char*)(b+names[i]); if(streq(nm,name)){ u32 rva=funcs[ords[i]]; if(outRva)*outRva=rva; if(outExpRva)*outExpRva=expRva; if(outExpSize)*outExpSize=expSize; return b+rva; } }
    return nullptr;
}

static void split_forwarder(const char* fwd,char* mod,usize mcap,char* fn,usize fcap){
    mod[0]=0; fn[0]=0; if(!fwd)return; usize i=0; while(fwd[i]&&fwd[i]!='.'&&i+1<mcap){mod[i]=fwd[i];++i;} mod[i]=0; if(fwd[i]=='.')++i; usize j=0; while(fwd[i]&&j+1<fcap) fn[j++]=fwd[i++]; fn[j]=0; sappend(mod,mcap,".dll");
}

static void* resolve_export(void* mod,const char* name,int depth=0){
    if(depth>6) return nullptr; u32 r=0,er=0,es=0; u8* p=(u8*)resolve_export_raw(mod,name,&r,&er,&es); if(!p) return nullptr;
    if(r>=er && r<er+es){ char mn[80],fn[128]; split_forwarder((const char*)p,mn,sizeof(mn),fn,sizeof(fn)); void* m=find_loaded_module(mn); if(!m) return nullptr; return resolve_export(m,fn,depth+1); }
    return p;
}

typedef FARPROC (*GetProcAddress_t)(HMODULE,LPCSTR);
typedef HMODULE (*LoadLibraryA_t)(LPCSTR);
typedef HANDLE (*CreateThread_t)(void*,usize,DWORD(*)(void*),void*,DWORD,DWORD*);
typedef void (*Sleep_t)(DWORD);
typedef DWORD (*GetModuleFileNameW_t)(HMODULE,LPWSTR,DWORD);
typedef HANDLE (*CreateFileW_t)(LPCWSTR,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
typedef BOOL (*CreateDirectoryW_t)(LPCWSTR,void*);
typedef BOOL (*WriteFile_t)(HANDLE,const void*,DWORD,DWORD*,void*);
typedef BOOL (*ReadFile_t)(HANDLE,void*,DWORD,DWORD*,void*);
typedef DWORD (*SetFilePointer_t)(HANDLE,long,long*,DWORD);
typedef BOOL (*CloseHandle_t)(HANDLE);
typedef short (*GetAsyncKeyState_t)(int);
typedef BOOL (*GetCursorPos_t)(POINT_X*);
typedef HWND (*GetForegroundWindow_t)();
typedef BOOL (*ScreenToClient_t)(HWND,POINT_X*);
typedef BOOL (*GetClientRect_t)(HWND,RECT_X*);
typedef int (*sprintf_t)(char*,const char*,...);

static GetProcAddress_t pGetProcAddress=nullptr; static LoadLibraryA_t pLoadLibraryA=nullptr; static Sleep_t pSleep=nullptr;
static GetModuleFileNameW_t pGetModuleFileNameW=nullptr; static CreateFileW_t pCreateFileW=nullptr; static CreateDirectoryW_t pCreateDirectoryW=nullptr; static WriteFile_t pWriteFile=nullptr; static ReadFile_t pReadFile=nullptr; static SetFilePointer_t pSetFilePointer=nullptr; static CloseHandle_t pCloseHandle=nullptr;
static GetAsyncKeyState_t pGetAsyncKeyState=nullptr;
static DWORD (*p70_processId)()=nullptr;
static DWORD (*p70_windowProcess)(HWND,DWORD*)=nullptr;
static int (*p70_className)(HWND,LPSTR,int)=nullptr;
static GetCursorPos_t pGetCursorPos=nullptr; static GetForegroundWindow_t pGetForegroundWindow=nullptr; static ScreenToClient_t pScreenToClient=nullptr; static GetClientRect_t pGetClientRect=nullptr;
static sprintf_t psprintf=nullptr;

static bool init_winapi(){
    void* k=find_loaded_module("kernel32.dll"); if(!k) return false;
    pGetProcAddress=(GetProcAddress_t)resolve_export(k,"GetProcAddress"); pLoadLibraryA=(LoadLibraryA_t)resolve_export(k,"LoadLibraryA"); pSleep=(Sleep_t)resolve_export(k,"Sleep");
    pGetModuleFileNameW=(GetModuleFileNameW_t)resolve_export(k,"GetModuleFileNameW");pCreateFileW=(CreateFileW_t)resolve_export(k,"CreateFileW");pCreateDirectoryW=(CreateDirectoryW_t)resolve_export(k,"CreateDirectoryW");pWriteFile=(WriteFile_t)resolve_export(k,"WriteFile");pReadFile=(ReadFile_t)resolve_export(k,"ReadFile");pSetFilePointer=(SetFilePointer_t)resolve_export(k,"SetFilePointer");pCloseHandle=(CloseHandle_t)resolve_export(k,"CloseHandle");
    if(!pLoadLibraryA||!pGetProcAddress||!pSleep||!pGetModuleFileNameW||!pCreateFileW||!pCreateDirectoryW||!pWriteFile||!pReadFile||!pSetFilePointer||!pCloseHandle)return false;
    p70_processId=(decltype(p70_processId))resolve_export(k,"GetCurrentProcessId");
    HMODULE u=pLoadLibraryA("user32.dll"); if(u){
        p70_windowProcess=(decltype(p70_windowProcess))pGetProcAddress(u,"GetWindowThreadProcessId");
        p70_className=(decltype(p70_className))pGetProcAddress(u,"GetClassNameA");
        pGetAsyncKeyState=(GetAsyncKeyState_t)pGetProcAddress(u,"GetAsyncKeyState");
        pGetCursorPos=(GetCursorPos_t)pGetProcAddress(u,"GetCursorPos");
        pGetForegroundWindow=(GetForegroundWindow_t)pGetProcAddress(u,"GetForegroundWindow");
        pScreenToClient=(ScreenToClient_t)pGetProcAddress(u,"ScreenToClient");
        pGetClientRect=(GetClientRect_t)pGetProcAddress(u,"GetClientRect");
    }
    HMODULE c=pLoadLibraryA("msvcrt.dll"); if(c)psprintf=(sprintf_t)pGetProcAddress(c,"sprintf");
    return pGetAsyncKeyState&&pGetCursorPos&&pGetForegroundWindow&&pScreenToClient&&pGetClientRect&&psprintf;
}

#include "RuntimeWork.h"
static HWND p70_game_foreground(){
    if(!pGetForegroundWindow||!p70_processId||!p70_windowProcess||!p70_className)return nullptr;
    HWND window=pGetForegroundWindow();DWORD owner=0;char name[80]{};
    if(!window||!p70_windowProcess(window,&owner)||owner!=p70_processId())return nullptr;
    if(!p70_className(window,name,sizeof(name))||!streq(name,"UnityWndClass"))return nullptr;
    return window;
}

static wchar_t g_logPath[600];
static wchar_t g_dncPreferenceFolder[600];
static wchar_t g_dncPreferenceBasePath[600];
static void init_log_path(){
    g_logPath[0]=0;g_dncPreferenceFolder[0]=0;g_dncPreferenceBasePath[0]=0;
    // All mod output remains below TPM.exe/Mods. Never fall back to CWD/AppData.
    wchar_t exe[520]{};
    DWORD count=pGetModuleFileNameW?pGetModuleFileNameW(nullptr,exe,(DWORD)(sizeof(exe)/sizeof(exe[0]))):0;
    if(!count||count>=sizeof(exe)/sizeof(exe[0])-80)return;
    usize n=wlen(exe);while(n&&exe[n-1]!=L'\\'&&exe[n-1]!=L'/')--n;
    if(!n)return;exe[n]=0;
    wchar_t logFolder[600]{};wcopy(logFolder,sizeof(logFolder)/sizeof(logFolder[0]),exe);wappend(logFolder,sizeof(logFolder)/sizeof(logFolder[0]),L"Mods\\Logs");pCreateDirectoryW(logFolder,nullptr);
    wcopy(g_logPath,sizeof(g_logPath)/sizeof(g_logPath[0]),logFolder);wappend(g_logPath,sizeof(g_logPath)/sizeof(g_logPath[0]),L"\\DayNightCycle.log");
    wcopy(g_dncPreferenceFolder,sizeof(g_dncPreferenceFolder)/sizeof(g_dncPreferenceFolder[0]),exe);wappend(g_dncPreferenceFolder,sizeof(g_dncPreferenceFolder)/sizeof(g_dncPreferenceFolder[0]),L"Mods\\Saves");
    wcopy(g_dncPreferenceBasePath,sizeof(g_dncPreferenceBasePath)/sizeof(g_dncPreferenceBasePath[0]),g_dncPreferenceFolder);wappend(g_dncPreferenceBasePath,sizeof(g_dncPreferenceBasePath)/sizeof(g_dncPreferenceBasePath[0]),L"\\DayNightCycle-Preferences");
}
#include "BoundedLog.h"

// ---------- IL2CPP ----------
struct Il2CppObject { void* klass; void* monitor; };
struct Il2CppArray { Il2CppObject obj; void* bounds; uptr max_length; void* vector[1]; };
using Il2CppDomain=void; using Il2CppAssembly=void; using Il2CppImage=void; using Il2CppClass=void; using MethodInfo=void; using Il2CppType=void; using Il2CppThread=void; using Il2CppString=void;

typedef Il2CppDomain* (*il2cpp_domain_get_t)();
typedef const Il2CppAssembly** (*il2cpp_domain_get_assemblies_t)(Il2CppDomain*, usize*);
typedef const Il2CppImage* (*il2cpp_assembly_get_image_t)(const Il2CppAssembly*);
typedef usize (*il2cpp_image_get_class_count_t)(const Il2CppImage*);
typedef Il2CppClass* (*il2cpp_image_get_class_t)(const Il2CppImage*,usize);
typedef const char* (*il2cpp_class_get_name_t)(Il2CppClass*);
typedef const char* (*il2cpp_class_get_namespace_t)(Il2CppClass*);
typedef const MethodInfo* (*il2cpp_class_get_method_from_name_t)(Il2CppClass*,const char*,int);
typedef const MethodInfo* (*il2cpp_class_get_methods_t)(Il2CppClass*,void**);
typedef const char* (*il2cpp_method_get_name_t)(const MethodInfo*);
typedef u32 (*il2cpp_method_get_param_count_t)(const MethodInfo*);
typedef const Il2CppType* (*il2cpp_method_get_param_t)(const MethodInfo*,u32);
typedef char* (*il2cpp_type_get_name_t)(const Il2CppType*);
typedef void (*il2cpp_free_t)(void*);
typedef Il2CppObject* (*il2cpp_runtime_invoke_t)(const MethodInfo*,void*,void**,Il2CppObject**);
typedef const Il2CppType* (*il2cpp_class_get_type_t)(Il2CppClass*);
typedef Il2CppObject* (*il2cpp_type_get_object_t)(const Il2CppType*);
typedef Il2CppString* (*il2cpp_string_new_t)(const char*);
typedef int (*il2cpp_string_length_t)(Il2CppString*);
typedef const u16* (*il2cpp_string_chars_t)(Il2CppString*);
typedef void* (*il2cpp_object_unbox_t)(Il2CppObject*);
typedef Il2CppObject* (*il2cpp_object_new_t)(Il2CppClass*);
typedef Il2CppArray* (*il2cpp_array_new_t)(Il2CppClass*, uptr);
typedef Il2CppClass* (*il2cpp_object_get_class_t)(Il2CppObject*);
typedef Il2CppClass* (*il2cpp_class_get_parent_t)(Il2CppClass*);
typedef void* Il2CppFieldInfo; using Il2CppPropertyInfo=void;
typedef Il2CppFieldInfo* (*il2cpp_class_get_fields_t)(Il2CppClass*,void**);
typedef const char* (*il2cpp_field_get_name_t)(Il2CppFieldInfo*);
typedef Il2CppObject* (*il2cpp_field_get_value_object_t)(Il2CppFieldInfo*,void*);
typedef void (*il2cpp_field_static_get_value_t)(Il2CppFieldInfo*,void*);
typedef void (*il2cpp_field_set_value_t)(Il2CppObject*,Il2CppFieldInfo*,void*);
using DNCGCHandle=uptr;
static_assert(sizeof(DNCGCHandle)==sizeof(void*),"GC handles must retain pointer width");
typedef DNCGCHandle (*il2cpp_gchandle_new_t)(Il2CppObject*, bool);

static il2cpp_domain_get_t il2cpp_domain_get=nullptr;
static il2cpp_domain_get_assemblies_t il2cpp_domain_get_assemblies=nullptr; static il2cpp_assembly_get_image_t il2cpp_assembly_get_image=nullptr; static il2cpp_image_get_class_count_t il2cpp_image_get_class_count=nullptr; static il2cpp_image_get_class_t il2cpp_image_get_class=nullptr;
static il2cpp_class_get_name_t il2cpp_class_get_name=nullptr; static il2cpp_class_get_namespace_t il2cpp_class_get_namespace=nullptr; static il2cpp_class_get_method_from_name_t il2cpp_class_get_method_from_name=nullptr; static il2cpp_class_get_methods_t il2cpp_class_get_methods=nullptr;
static il2cpp_method_get_name_t il2cpp_method_get_name=nullptr; static il2cpp_method_get_param_count_t il2cpp_method_get_param_count=nullptr; static il2cpp_method_get_param_t il2cpp_method_get_param=nullptr; static il2cpp_type_get_name_t il2cpp_type_get_name=nullptr; static il2cpp_free_t il2cpp_free=nullptr;
static il2cpp_runtime_invoke_t il2cpp_runtime_invoke=nullptr; static il2cpp_class_get_type_t il2cpp_class_get_type=nullptr; static il2cpp_type_get_object_t il2cpp_type_get_object=nullptr; static il2cpp_string_new_t il2cpp_string_new=nullptr; static il2cpp_string_length_t il2cpp_string_length=nullptr; static il2cpp_string_chars_t il2cpp_string_chars=nullptr; static il2cpp_object_unbox_t il2cpp_object_unbox=nullptr; static il2cpp_object_new_t il2cpp_object_new=nullptr; static il2cpp_array_new_t il2cpp_array_new=nullptr;
static il2cpp_object_get_class_t il2cpp_object_get_class=nullptr; static il2cpp_class_get_parent_t il2cpp_class_get_parent=nullptr; static il2cpp_class_get_fields_t il2cpp_class_get_fields=nullptr; static il2cpp_field_get_name_t il2cpp_field_get_name=nullptr; static il2cpp_field_get_value_object_t il2cpp_field_get_value_object=nullptr; static il2cpp_field_static_get_value_t il2cpp_field_static_get_value=nullptr; static il2cpp_field_set_value_t il2cpp_field_set_value=nullptr;
static il2cpp_gchandle_new_t il2cpp_gchandle_new=nullptr;

static bool init_il2cpp(){
    HMODULE g=(HMODULE)find_loaded_module("GameAssembly.dll"); if(!g) return false;
    #define RESOLVE(N) N=(N##_t)resolve_export(g,#N); if(!N) return false
    RESOLVE(il2cpp_domain_get); RESOLVE(il2cpp_domain_get_assemblies); RESOLVE(il2cpp_assembly_get_image); RESOLVE(il2cpp_image_get_class_count); RESOLVE(il2cpp_image_get_class); RESOLVE(il2cpp_class_get_name); RESOLVE(il2cpp_class_get_namespace); RESOLVE(il2cpp_class_get_method_from_name); RESOLVE(il2cpp_class_get_methods); RESOLVE(il2cpp_method_get_name); RESOLVE(il2cpp_method_get_param_count); RESOLVE(il2cpp_method_get_param); RESOLVE(il2cpp_type_get_name); RESOLVE(il2cpp_free); RESOLVE(il2cpp_runtime_invoke); RESOLVE(il2cpp_class_get_type); RESOLVE(il2cpp_type_get_object); RESOLVE(il2cpp_string_new); RESOLVE(il2cpp_string_length); RESOLVE(il2cpp_string_chars); RESOLVE(il2cpp_object_unbox); RESOLVE(il2cpp_object_new); RESOLVE(il2cpp_array_new); RESOLVE(il2cpp_object_get_class); RESOLVE(il2cpp_class_get_parent); RESOLVE(il2cpp_class_get_fields); RESOLVE(il2cpp_field_get_name); RESOLVE(il2cpp_field_get_value_object); RESOLVE(il2cpp_field_static_get_value); RESOLVE(il2cpp_field_set_value); RESOLVE(il2cpp_gchandle_new);
    #undef RESOLVE
    return true;
}

static Il2CppClass* find_class(const char* ns,const char* name){
    static P78MetadataCache<Il2CppClass*,128> cache;
    if(!ns)ns="";if(!name)return nullptr;if(auto c=cache.get(nullptr,ns,name))return c;
    Il2CppDomain* d=il2cpp_domain_get(); if(!d)return nullptr; usize ac=0; const Il2CppAssembly** as=il2cpp_domain_get_assemblies(d,&ac); if(!as)return nullptr;
    for(usize a=0;a<ac;++a){ const Il2CppImage* im=il2cpp_assembly_get_image(as[a]); if(!im)continue; usize cc=il2cpp_image_get_class_count(im); for(usize i=0;i<cc;++i){ Il2CppClass* c=il2cpp_image_get_class(im,i); if(!c)continue; const char* cn=il2cpp_class_get_name(c); const char* cs=il2cpp_class_get_namespace(c); if(streq(cn,name)&&streq(cs?cs:"",ns)){cache.put(nullptr,ns,name,c);return c;} } }
    return nullptr;
}
// One-time metadata discovery for the lighting service owner, whose namespace
// can change between game builds.
static Il2CppClass* find_class_exposing_method(const char* methodName){
    if(!methodName)return nullptr; Il2CppDomain* d=il2cpp_domain_get(); if(!d)return nullptr;
    usize ac=0; const Il2CppAssembly** as=il2cpp_domain_get_assemblies(d,&ac); if(!as)return nullptr;
    for(usize a=0;a<ac;++a){const Il2CppImage* im=il2cpp_assembly_get_image(as[a]);if(!im)continue;usize cc=il2cpp_image_get_class_count(im);
        for(usize i=0;i<cc;++i){Il2CppClass* c=il2cpp_image_get_class(im,i);if(!c)continue;void* it=nullptr;const MethodInfo* m=nullptr;
            while((m=il2cpp_class_get_methods(c,&it))!=nullptr){const char* n=il2cpp_method_get_name(m);if(n&&streq(n,methodName)&&il2cpp_method_get_param_count(m)==0)return c;}
        }
    }
    return nullptr;
}

static const MethodInfo* find_method_param1(Il2CppClass* c,const char* name,const char* paramType){
    if(!c)return nullptr; void* it=nullptr; const MethodInfo* m=nullptr;
    while((m=il2cpp_class_get_methods(c,&it))!=nullptr){ const char* mn=il2cpp_method_get_name(m); if(!streq(mn,name))continue; if(il2cpp_method_get_param_count(m)!=1)continue; const Il2CppType* pt=il2cpp_method_get_param(m,0); char* tn=pt?il2cpp_type_get_name(pt):nullptr; bool ok=tn&&streq(tn,paramType); if(tn)il2cpp_free(tn); if(ok)return m; }
    return nullptr;
}

static const MethodInfo* find_method_param2(Il2CppClass* c,const char* name,const char* p0,const char* p1){
    if(!c)return nullptr; void* it=nullptr; const MethodInfo* m=nullptr;
    while((m=il2cpp_class_get_methods(c,&it))!=nullptr){
        const char* mn=il2cpp_method_get_name(m); if(!streq(mn,name))continue;
        if(il2cpp_method_get_param_count(m)!=2)continue;
        const Il2CppType* t0=il2cpp_method_get_param(m,0); const Il2CppType* t1=il2cpp_method_get_param(m,1);
        char* n0=t0?il2cpp_type_get_name(t0):nullptr; char* n1=t1?il2cpp_type_get_name(t1):nullptr;
        bool ok=n0&&n1&&streq(n0,p0)&&streq(n1,p1);
        if(n0)il2cpp_free(n0); if(n1)il2cpp_free(n1);
        if(ok)return m;
    }
    return nullptr;
}

static const MethodInfo* find_method_param3(Il2CppClass* c,const char* name,const char* p0,const char* p1,const char* p2){
    if(!c)return nullptr; void* it=nullptr; const MethodInfo* m=nullptr;
    while((m=il2cpp_class_get_methods(c,&it))!=nullptr){
        const char* mn=il2cpp_method_get_name(m); if(!streq(mn,name))continue;
        if(il2cpp_method_get_param_count(m)!=3)continue;
        const Il2CppType* t0=il2cpp_method_get_param(m,0); const Il2CppType* t1=il2cpp_method_get_param(m,1); const Il2CppType* t2=il2cpp_method_get_param(m,2);
        char* n0=t0?il2cpp_type_get_name(t0):nullptr; char* n1=t1?il2cpp_type_get_name(t1):nullptr; char* n2=t2?il2cpp_type_get_name(t2):nullptr;
        bool ok=n0&&n1&&n2&&streq(n0,p0)&&streq(n1,p1)&&streq(n2,p2);
        if(n0)il2cpp_free(n0); if(n1)il2cpp_free(n1); if(n2)il2cpp_free(n2);
        if(ok)return m;
    }
    return nullptr;
}

static const MethodInfo* find_method_paramcount(Il2CppClass* c,const char* name,int count){
    if(!c)return nullptr; void* it=nullptr; const MethodInfo* m=nullptr;
    while((m=il2cpp_class_get_methods(c,&it))!=nullptr){
        const char* mn=il2cpp_method_get_name(m); if(!streq(mn,name))continue;
        if((int)il2cpp_method_get_param_count(m)==count)return m;
    }
    return nullptr;
}

static const MethodInfo* find_method1_hierarchy(Il2CppClass* c,const char* name,const char* paramType){
    for(Il2CppClass* cur=c;cur;cur=il2cpp_class_get_parent(cur)){
        const MethodInfo* m=find_method_param1(cur,name,paramType);
        if(m)return m;
    }
    return nullptr;
}

static Il2CppObject* invoke(const MethodInfo* m,void* obj,void** args=nullptr){ if(!m)return nullptr; Il2CppObject* ex=nullptr; Il2CppObject* r=il2cpp_runtime_invoke(m,obj,args,&ex); if(ex)return nullptr; return r; }
static int boxed_i32(Il2CppObject* o,int def=0){ if(!o)return def; void* p=il2cpp_object_unbox(o); return p?*(int*)p:def; }
static bool boxed_bool(Il2CppObject* o,bool def=false){ if(!o)return def; void* p=il2cpp_object_unbox(o); return p?(*(u8*)p!=0):def; }
static float boxed_float(Il2CppObject* o,float def=0){ if(!o)return def; void* p=il2cpp_object_unbox(o); return p?*(float*)p:def; }
static double boxed_double(Il2CppObject* o,double def=0){ if(!o)return def; void* p=il2cpp_object_unbox(o); return p?*(double*)p:def; }
static void string_ascii(Il2CppString* s,char* out,usize cap){ if(!out||cap==0)return; out[0]=0; if(!s)return; int n=il2cpp_string_length(s); const u16* ch=il2cpp_string_chars(s); if(!ch)return; usize j=0; for(int i=0;i<n&&j+1<cap;++i){ u16 c=ch[i]; out[j++]=(c<128)?(char)c:'?'; } out[j]=0; }


struct F2{float x,y;}; struct F3{float x,y,z;}; struct F4{float x,y,z,w;}; struct Rect4{float x,y,width,height;};

static Il2CppClass *C_Object,*C_Component,*C_GameObject,*C_Transform,*C_RectTransform,*C_RectTransformUtility,*C_Resources,*C_Shader,*C_Behaviour,*C_Graphic,*C_Selectable,*C_Light,*C_UIImage,*C_Texture2D,*C_Sprite,*C_ImageConversion,*C_Byte,*C_TooltipSpawner;
static const MethodInfo *M_Object_get_name,*M_Object_GetInstanceID,*M_Object_Instantiate,*M_Object_Destroy,*M_Resources_FindAll,*M_GameObject_GetComponents,*M_GameObject_SetActive,*M_GameObject_get_activeInHierarchy,*M_GameObject_get_transform,*M_Transform_get_parent,*M_Transform_SetParent,*M_Transform_IsChildOf,*M_Transform_SetSiblingIndex,*M_Transform_SetAsLastSibling,*M_Transform_InverseTransformPoint,*M_Component_get_gameObject;
static const MethodInfo *M_RectTransform_get_rect,*M_RectTransform_get_anchoredPosition,*M_RectTransform_set_anchoredPosition,*M_RectTransform_get_anchorMin,*M_RectTransform_set_anchorMin,*M_RectTransform_get_anchorMax,*M_RectTransform_set_anchorMax,*M_RectTransform_get_sizeDelta,*M_RectTransform_set_sizeDelta,*M_RectTransform_get_pivot,*M_RectTransform_set_pivot,*M_RectTransformUtility_ScreenPointToLocalPointInRectangle;
static const MethodInfo *M_Behaviour_set_enabled,*M_Graphic_set_raycastTarget,*M_Graphic_set_color,*M_Selectable_set_targetGraphic;
static const MethodInfo *M_Texture2D_ctor,*M_ImageConversion_LoadImage,*M_Sprite_Create,*M_UIImage_set_sprite,*M_UIImage_set_overrideSprite,*M_UIImage_set_preserveAspect,*M_Tooltip_Reset,*M_Tooltip_CursorOver,*M_Tooltip_CursorOut;
static const MethodInfo *M_Shader_PropertyToID,*M_Shader_SetGlobalColorInt;
static const MethodInfo *M_Component_get_transform,*M_Light_set_color,*M_Light_set_intensity,*M_Light_get_shadowStrength,*M_Light_set_shadowStrength;

static void log_resolve(const char* kind,const char* name,bool ok){ char b[512]; psprintf(b,"RESOLVE %-8s %-56s : %s\r\n",kind,name,ok?"OK":"MISSING"); log_raw(b); }

static bool init_unity_methods(){
    log_raw("-- Unity API resolution --\r\n");
    C_Object=find_class("UnityEngine","Object"); log_resolve("class","UnityEngine.Object",C_Object!=nullptr);
    C_Component=find_class("UnityEngine","Component"); log_resolve("class","UnityEngine.Component",C_Component!=nullptr);
    C_GameObject=find_class("UnityEngine","GameObject"); log_resolve("class","UnityEngine.GameObject",C_GameObject!=nullptr);
    C_Transform=find_class("UnityEngine","Transform"); log_resolve("class","UnityEngine.Transform",C_Transform!=nullptr);
    C_RectTransform=find_class("UnityEngine","RectTransform"); log_resolve("class","UnityEngine.RectTransform",C_RectTransform!=nullptr);
    C_RectTransformUtility=find_class("UnityEngine","RectTransformUtility"); log_resolve("class","UnityEngine.RectTransformUtility",C_RectTransformUtility!=nullptr);
    C_Resources=find_class("UnityEngine","Resources"); log_resolve("class","UnityEngine.Resources",C_Resources!=nullptr);
    C_Shader=find_class("UnityEngine","Shader"); log_resolve("class","UnityEngine.Shader",C_Shader!=nullptr);
    C_Behaviour=find_class("UnityEngine","Behaviour"); log_resolve("class","UnityEngine.Behaviour",C_Behaviour!=nullptr);
    C_Graphic=find_class("UnityEngine.UI","Graphic"); log_resolve("class","UnityEngine.UI.Graphic",C_Graphic!=nullptr);
    C_Selectable=find_class("UnityEngine.UI","Selectable"); log_resolve("class","UnityEngine.UI.Selectable [button ownership]",C_Selectable!=nullptr);
    // Image/tooltip APIs are optional modules and are deliberately resolved
    // only after the live CalendarUI control exists. The proven 2.7 lighting
    // runtime must never depend on artwork availability during startup.
    C_Light=find_class("UnityEngine","Light"); log_resolve("class","UnityEngine.Light",C_Light!=nullptr);
    if(!C_Object||!C_Component||!C_GameObject||!C_Transform||!C_Resources||!C_Shader||!C_Light) return false;
    M_Object_get_name=il2cpp_class_get_method_from_name(C_Object,"get_name",0); log_resolve("method","Object.get_name()",M_Object_get_name!=nullptr);
    M_Object_GetInstanceID=il2cpp_class_get_method_from_name(C_Object,"GetInstanceID",0); log_resolve("method","Object.GetInstanceID() [optional]",M_Object_GetInstanceID!=nullptr);
    M_Object_Instantiate=find_method_param1(C_Object,"Instantiate","UnityEngine.Object"); log_resolve("method","Object.Instantiate(Object)",M_Object_Instantiate!=nullptr);
    M_Object_Destroy=find_method_param1(C_Object,"Destroy","UnityEngine.Object");log_resolve("method","Object.Destroy(Object) [partial UI cleanup]",M_Object_Destroy!=nullptr);
    M_Resources_FindAll=il2cpp_class_get_method_from_name(C_Resources,"FindObjectsOfTypeAll",1); log_resolve("method","Resources.FindObjectsOfTypeAll(Type)",M_Resources_FindAll!=nullptr);
    M_GameObject_GetComponents=find_method_param1(C_GameObject,"GetComponents","System.Type"); log_resolve("method","GameObject.GetComponents(Type)",M_GameObject_GetComponents!=nullptr);
    M_GameObject_SetActive=find_method_param1(C_GameObject,"SetActive","System.Boolean"); log_resolve("method","GameObject.SetActive(Boolean)",M_GameObject_SetActive!=nullptr);
    M_GameObject_get_activeInHierarchy=il2cpp_class_get_method_from_name(C_GameObject,"get_activeInHierarchy",0); log_resolve("method","GameObject.get_activeInHierarchy()",M_GameObject_get_activeInHierarchy!=nullptr);
    M_GameObject_get_transform=il2cpp_class_get_method_from_name(C_GameObject,"get_transform",0); log_resolve("method","GameObject.get_transform()",M_GameObject_get_transform!=nullptr);
    M_Transform_get_parent=il2cpp_class_get_method_from_name(C_Transform,"get_parent",0); log_resolve("method","Transform.get_parent()",M_Transform_get_parent!=nullptr);
    M_Transform_SetParent=find_method_param2(C_Transform,"SetParent","UnityEngine.Transform","System.Boolean"); log_resolve("method","Transform.SetParent(Transform, Boolean)",M_Transform_SetParent!=nullptr);
    M_Transform_IsChildOf=find_method_param1(C_Transform,"IsChildOf","UnityEngine.Transform"); log_resolve("method","Transform.IsChildOf(Transform)",M_Transform_IsChildOf!=nullptr);
    M_Transform_SetSiblingIndex=find_method_param1(C_Transform,"SetSiblingIndex","System.Int32"); log_resolve("method","Transform.SetSiblingIndex(Int32)",M_Transform_SetSiblingIndex!=nullptr);
    M_Transform_SetAsLastSibling=il2cpp_class_get_method_from_name(C_Transform,"SetAsLastSibling",0); log_resolve("method","Transform.SetAsLastSibling()",M_Transform_SetAsLastSibling!=nullptr);
    M_Transform_InverseTransformPoint=find_method_param1(C_Transform,"InverseTransformPoint","UnityEngine.Vector3"); log_resolve("method","Transform.InverseTransformPoint(Vector3)",M_Transform_InverseTransformPoint!=nullptr);
    if(C_RectTransform){
        M_RectTransform_get_rect=il2cpp_class_get_method_from_name(C_RectTransform,"get_rect",0);
        M_RectTransform_get_anchoredPosition=il2cpp_class_get_method_from_name(C_RectTransform,"get_anchoredPosition",0);
        M_RectTransform_set_anchoredPosition=find_method_param1(C_RectTransform,"set_anchoredPosition","UnityEngine.Vector2");
        M_RectTransform_get_anchorMin=il2cpp_class_get_method_from_name(C_RectTransform,"get_anchorMin",0);
        M_RectTransform_set_anchorMin=find_method_param1(C_RectTransform,"set_anchorMin","UnityEngine.Vector2");
        M_RectTransform_get_anchorMax=il2cpp_class_get_method_from_name(C_RectTransform,"get_anchorMax",0);
        M_RectTransform_set_anchorMax=find_method_param1(C_RectTransform,"set_anchorMax","UnityEngine.Vector2");
        M_RectTransform_get_sizeDelta=il2cpp_class_get_method_from_name(C_RectTransform,"get_sizeDelta",0);
        M_RectTransform_set_sizeDelta=find_method_param1(C_RectTransform,"set_sizeDelta","UnityEngine.Vector2");
        M_RectTransform_get_pivot=il2cpp_class_get_method_from_name(C_RectTransform,"get_pivot",0);
        M_RectTransform_set_pivot=find_method_param1(C_RectTransform,"set_pivot","UnityEngine.Vector2");
    }
    log_resolve("method","RectTransform.get_rect()",M_RectTransform_get_rect!=nullptr);
    log_resolve("method","RectTransform.get_anchoredPosition()",M_RectTransform_get_anchoredPosition!=nullptr);
    log_resolve("method","RectTransform.set_anchoredPosition(Vector2)",M_RectTransform_set_anchoredPosition!=nullptr);
    log_resolve("method","RectTransform anchor/size/pivot geometry accessors",M_RectTransform_get_anchorMin&&M_RectTransform_set_anchorMin&&M_RectTransform_get_anchorMax&&M_RectTransform_set_anchorMax&&M_RectTransform_get_sizeDelta&&M_RectTransform_set_sizeDelta&&M_RectTransform_get_pivot&&M_RectTransform_set_pivot);
    if(C_RectTransformUtility) M_RectTransformUtility_ScreenPointToLocalPointInRectangle=find_method_paramcount(C_RectTransformUtility,"ScreenPointToLocalPointInRectangle",4);
    log_resolve("method","RectTransformUtility.ScreenPointToLocalPointInRectangle(...) ",M_RectTransformUtility_ScreenPointToLocalPointInRectangle!=nullptr);
    M_Component_get_gameObject=il2cpp_class_get_method_from_name(C_Component,"get_gameObject",0); log_resolve("method","Component.get_gameObject()",M_Component_get_gameObject!=nullptr);
    M_Component_get_transform=il2cpp_class_get_method_from_name(C_Component,"get_transform",0); log_resolve("method","Component.get_transform()",M_Component_get_transform!=nullptr);
    if(C_Light){M_Light_set_color=find_method_param1(C_Light,"set_color","UnityEngine.Color");M_Light_set_intensity=find_method_param1(C_Light,"set_intensity","System.Single");M_Light_get_shadowStrength=il2cpp_class_get_method_from_name(C_Light,"get_shadowStrength",0);M_Light_set_shadowStrength=find_method_param1(C_Light,"set_shadowStrength","System.Single");}
    log_resolve("method","Light.set_color(Color)",M_Light_set_color!=nullptr);
    log_resolve("method","Light.set_intensity(Single)",M_Light_set_intensity!=nullptr);
    log_resolve("method","Light.shadowStrength",M_Light_get_shadowStrength&&M_Light_set_shadowStrength);
    if(C_Behaviour)M_Behaviour_set_enabled=find_method_param1(C_Behaviour,"set_enabled","System.Boolean");
    log_resolve("method","Behaviour.set_enabled(Boolean)",M_Behaviour_set_enabled!=nullptr);
    if(C_Graphic){ M_Graphic_set_raycastTarget=find_method_param1(C_Graphic,"set_raycastTarget","System.Boolean"); M_Graphic_set_color=find_method_param1(C_Graphic,"set_color","UnityEngine.Color"); }
    if(C_Selectable)M_Selectable_set_targetGraphic=find_method_param1(C_Selectable,"set_targetGraphic","UnityEngine.UI.Graphic");
    log_resolve("method","Graphic.set_raycastTarget(Boolean)",M_Graphic_set_raycastTarget!=nullptr);
    M_Shader_PropertyToID=find_method_param1(C_Shader,"PropertyToID","System.String"); log_resolve("method","Shader.PropertyToID(String) [automatic exterior colour]",M_Shader_PropertyToID!=nullptr);
    M_Shader_SetGlobalColorInt=find_method_param2(C_Shader,"SetGlobalColor","System.Int32","UnityEngine.Color"); log_resolve("method","Shader.SetGlobalColor(Int32, Color) [automatic exterior colour]",M_Shader_SetGlobalColorInt!=nullptr);
    return M_Object_get_name&&M_Resources_FindAll&&M_GameObject_GetComponents&&M_GameObject_get_transform&&M_Component_get_gameObject&&M_Light_set_color&&M_Light_set_intensity;
}

static void object_name(void* obj,char* out,usize cap){ if(!obj){if(out&&cap)out[0]=0;return;} string_ascii((Il2CppString*)invoke(M_Object_get_name,obj,nullptr),out,cap); }
static int object_id(void* obj){ return (obj&&M_Object_GetInstanceID)?boxed_i32(invoke(M_Object_GetInstanceID,obj,nullptr),0):0; }
static F4 read_boxed_f4(Il2CppObject* o){ F4 z{0,0,0,0}; if(!o)return z; void* p=il2cpp_object_unbox(o); if(p)z=*(F4*)p; return z; }
static Il2CppFieldInfo* find_field_hierarchy(Il2CppClass* c,const char* name){
    static P78MetadataCache<Il2CppFieldInfo*,256> cache;Il2CppFieldInfo* cached=nullptr;if(cache.lookup(c,"",name,cached))return cached;
    for(Il2CppClass* cur=c;cur;cur=il2cpp_class_get_parent(cur)){
        void* it=nullptr; Il2CppFieldInfo* f=nullptr;
        while((f=il2cpp_class_get_fields(cur,&it))!=nullptr){ const char* fn=il2cpp_field_get_name(f); if(fn&&streq(fn,name)){cache.put(c,"",name,f);return f;} }
    }
    cache.put(c,"",name,nullptr);return nullptr;
}
static const MethodInfo* find_method0_hierarchy(Il2CppClass* c,const char* name){
    for(Il2CppClass* cur=c;cur;cur=il2cpp_class_get_parent(cur)){
        void* it=nullptr; const MethodInfo* m=nullptr;
        while((m=il2cpp_class_get_methods(cur,&it))!=nullptr){ const char* mn=il2cpp_method_get_name(m); if(mn&&streq(mn,name)&&il2cpp_method_get_param_count(m)==0) return m; }
    }
    return nullptr;
}
static Il2CppObject* field_object(void* obj,const char* name){
    if(!obj)return nullptr; Il2CppClass* c=il2cpp_object_get_class((Il2CppObject*)obj); Il2CppFieldInfo* f=find_field_hierarchy(c,name); return f?il2cpp_field_get_value_object(f,obj):nullptr;
}
static int field_i32(void* obj,const char* name,int def=0){ return boxed_i32(field_object(obj,name),def); }
static i64 field_i64(void* obj,const char* name,i64 def=0){ Il2CppObject* o=field_object(obj,name); if(!o)return def; void* p=il2cpp_object_unbox(o); return p?*(i64*)p:def; }
static float field_float(void* obj,const char* name,float def=0){ return boxed_float(field_object(obj,name),def); }
static F4 field_f4(void* obj,const char* name){ F4 z{0,0,0,0}; Il2CppObject* o=field_object(obj,name); if(!o)return z; void* p=il2cpp_object_unbox(o); if(p)z=*(F4*)p; return z; }
static void* static_ref_field(Il2CppClass* c,const char* name){
    Il2CppFieldInfo* f=find_field_hierarchy(c,name); if(!f||!il2cpp_field_static_get_value)return nullptr; void* p=nullptr; il2cpp_field_static_get_value(f,&p); return p;
}
static void set_field_float(void* object,const char* name,float value){
    if(!object||!il2cpp_field_set_value)return;Il2CppFieldInfo* field=find_field_hierarchy(il2cpp_object_get_class((Il2CppObject*)object),name);if(field)il2cpp_field_set_value((Il2CppObject*)object,field,&value);
}
static void set_field_i32(void* object,const char* name,int value){
    if(!object||!il2cpp_field_set_value)return;Il2CppFieldInfo* field=find_field_hierarchy(il2cpp_object_get_class((Il2CppObject*)object),name);if(field)il2cpp_field_set_value((Il2CppObject*)object,field,&value);
}
static void set_field_f4(void* object,const char* name,F4 value){
    if(!object||!il2cpp_field_set_value)return;Il2CppFieldInfo* field=find_field_hierarchy(il2cpp_object_get_class((Il2CppObject*)object),name);if(field)il2cpp_field_set_value((Il2CppObject*)object,field,&value);
}

struct RectState { bool valid;F2 anchorMin,anchorMax,anchored,sizeDelta,pivot; };

static bool get_rect4(void* rectTr,Rect4& r){
    r=Rect4{}; if(!rectTr||!M_RectTransform_get_rect)return false; Il2CppObject* box=invoke(M_RectTransform_get_rect,rectTr,nullptr); if(!box)return false; void* u=il2cpp_object_unbox(box); if(!u)return false; memcpy(&r,u,sizeof(r)); return true;
}
static bool get_vec2_prop(void* tr,const MethodInfo* m,F2& out){ out=F2{}; if(!tr||!m)return false; Il2CppObject* box=invoke(m,tr,nullptr); if(!box)return false; void* u=il2cpp_object_unbox(box); if(!u)return false; memcpy(&out,u,sizeof(out)); return true; }
static void set_vec2_prop(void* tr,const MethodInfo* m,F2 v){ if(!tr||!m)return; void* a[1]={&v}; invoke(m,tr,a); }
static bool capture_rect_state(void* tr,RectState& st){
    st=RectState{}; if(!tr)return false;
    if(!get_vec2_prop(tr,M_RectTransform_get_anchorMin,st.anchorMin))return false;
    if(!get_vec2_prop(tr,M_RectTransform_get_anchorMax,st.anchorMax))return false;
    if(!get_vec2_prop(tr,M_RectTransform_get_anchoredPosition,st.anchored))return false;
    if(!get_vec2_prop(tr,M_RectTransform_get_sizeDelta,st.sizeDelta))return false;
    if(!get_vec2_prop(tr,M_RectTransform_get_pivot,st.pivot))return false;
    st.valid=true; return true;
}
static void restore_rect_state(void* tr,const RectState& st){
    if(!tr||!st.valid)return;
    set_vec2_prop(tr,M_RectTransform_set_anchorMin,st.anchorMin);
    set_vec2_prop(tr,M_RectTransform_set_anchorMax,st.anchorMax);
    set_vec2_prop(tr,M_RectTransform_set_pivot,st.pivot);
    set_vec2_prop(tr,M_RectTransform_set_sizeDelta,st.sizeDelta);
    set_vec2_prop(tr,M_RectTransform_set_anchoredPosition,st.anchored);
}
static bool mouse_position(F3& p){
    p=F3{};
    // Read the physical cursor through user32, convert it to the foreground
    // game window's client area, and flip Y to Unity's bottom-left origin.
    if(pGetCursorPos&&pGetForegroundWindow&&pScreenToClient&&pGetClientRect){
        POINT_X pt{}; HWND hwnd=p70_game_foreground();
        if(!hwnd)return false;
        RECT_X rc{};
        if(hwnd&&pGetCursorPos(&pt)&&pScreenToClient(hwnd,&pt)&&pGetClientRect(hwnd,&rc)){
            long w=rc.right-rc.left, h=rc.bottom-rc.top;
            if(w>0&&h>0){
                p.x=(float)pt.x;
                p.y=(float)(h-1-pt.y);
                p.z=0.0f;
                return true;
            }
        }
    }
    return false;
}
static bool inverse_point(void* tr,F3 world,F3& local){
    local=F3{}; if(!tr||!M_Transform_InverseTransformPoint)return false; void* a[1]={&world}; Il2CppObject* box=invoke(M_Transform_InverseTransformPoint,tr,a); if(!box)return false; void* u=il2cpp_object_unbox(box); if(!u)return false; memcpy(&local,u,sizeof(local)); return true;
}

static bool screen_to_local(void* rectTr,F3 screen,F3& local){
    local=F3{};
    // Cursor position is in Unity-style screen client pixels. Use Unity's
    // screen-to-RectTransform conversion; the HUD canvas is screen-space, so
    // a null camera is the correct first choice.
    if(rectTr&&M_RectTransformUtility_ScreenPointToLocalPointInRectangle){
        F2 sp{screen.x,screen.y}; F2 lp{0,0};
        void* camera=nullptr; void* a[4]={rectTr,&sp,camera,&lp};
        Il2CppObject* box=invoke(M_RectTransformUtility_ScreenPointToLocalPointInRectangle,nullptr,a);
        if(box&&boxed_bool(box,false)){ local.x=lp.x; local.y=lp.y; local.z=0.0f; return true; }
    }
    // Compatibility fallback for a future build without RectTransformUtility metadata.
    return inverse_point(rectTr,screen,local);
}

static void* p50_level_state_instance(){
    Il2CppClass* c=find_class("TPS.Game","LevelState"); if(!c)return nullptr;
    void* s=static_ref_field(c,"<Instance>k__BackingField");
    if(!s){const MethodInfo* gi=find_method0_hierarchy(c,"get_Instance");if(gi)s=invoke(gi,nullptr,nullptr);}
    return s;
}
static void* p50_level_from_state(void* state){
    if(!state)return nullptr;
    void* level=field_object(state,"Level");
    if(!level)level=field_object(state,"<Level>k__BackingField");
    if(!level){const MethodInfo* gm=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)state),"get_Level");if(gm)level=invoke(gm,state,nullptr);}
    return level;
}
// In-memory lighting state. Profile 0 is the captured daytime light; the
// remaining profiles are the configured Dawn, Dusk and Night targets.
static void* g_p91Config=nullptr; static void* g_p91ExteriorLight=nullptr; static void* g_p91InteriorLight=nullptr; static void* g_p91Environment=nullptr; static void* g_p91LightingManager=nullptr; static Il2CppClass* g_p91GameManagerClass=nullptr;
static F4 g_p91DayColor{0,0,0,1},g_p91DaySkyColor{0,0,0,1},g_p91DaySunColor{0,0,0,1}; static float g_p91DayIntensity=0,g_p91DayShadowStrength=1;
static F4 g_dncDesiredLightColor{0,0,0,1},g_dncDesiredSkyColor{0,0,0,1},g_dncDesiredSunColor{0,0,0,1};static float g_dncDesiredIntensity=0,g_dncDesiredShadowStrength=1;
static F4 g_dncTransitionStartLight{0,0,0,1},g_dncTransitionStartSky{0,0,0,1},g_dncTransitionStartSun{0,0,0,1};static float g_dncTransitionStartIntensity=0,g_dncTransitionStartShadowStrength=1;
static F4 g_dncTransitionTargetLight{0,0,0,1},g_dncTransitionTargetSky{0,0,0,1},g_dncTransitionTargetSun{0,0,0,1};static float g_dncTransitionTargetIntensity=0,g_dncTransitionTargetShadowStrength=1;
static int g_p91Profile=0; static bool g_p91Captured=false;
struct DNCLightingBaseline { u64 configKey;F4 lightColor,skyColor,sunColor;float intensity,shadowStrength;bool used; };
static DNCLightingBaseline g_dncLightingBaselines[64]{};
static int g_p96AutoMode=0; // 0 manual, 1 weekly, 2 monthly
static u32 g_dncLightingEnvironmentGeneration=0;
static u64 g_dncLightingEnvironmentReadyAt=0;
static constexpr u64 DNC_LIGHTING_STARTUP_SETTLE_MS=3000;
static constexpr double DNC_GAME_SECONDS_PER_DAY=4.0;
static constexpr double DNC_DAYS_PER_WEEK=7.0;
static constexpr double DNC_DAYS_PER_MONTH=30.0;
static void* g_dncGameClockLevel=nullptr;static void* g_dncGameTime=nullptr;static const MethodInfo* g_dncGetGameTime=nullptr;
static bool g_dncAutoExteriorActive=false;static u32 g_dncAutoExteriorGeneration=0;static double g_dncVirtualHour=-1.0;
static u64 g_dncAutoCycleCalculatedEpoch=0xffffffffffffffffull;static u32 g_dncAutoCycleCalculatedEnvironment=0xffffffffu;static u32 g_dncAutoCycleCalculatedPreference=0xffffffffu;static int g_dncAutoCycleCalculatedMode=-1;
// Automatic follows continuous in-game cycle time. Manual selections use a
// short wall-clock smoothstep so their exterior light and stable Octalux sky
// route move together without invoking the game's noisy full lighting push.
static constexpr u64 DNC_MANUAL_LIGHTING_TRANSITION_MS=3000;
static constexpr u64 DNC_MANUAL_GRADUAL_SETTLE_MS=3000;
static u32 g_dncTransitionEnvironmentGeneration=0;
static u64 g_dncTransitionStartedAt=0;
static u64 g_dncManualGradualSettleUntil=0;
static int g_dncTransitionTargetProfile=-1;
static bool g_dncTransitionInitialised=false;
static bool g_dncTransitionActive=false;
static bool g_dncPreferencesReady=false;
static bool g_dncLightingHoldReady=false;
static const MethodInfo* g_dncPushLightingSettings=nullptr;
static void* g_dncOctaluxFeature=nullptr;
static const MethodInfo* g_dncOctaluxSetSkyColor=nullptr;
static const MethodInfo* g_dncOctaluxUpdateKeywords=nullptr;
static const MethodInfo* g_dncOctaluxGetCurrentQualitySettings=nullptr;
static void* g_dncOctaluxQualitySettings=nullptr;
static float g_dncOctaluxOriginalGradualBlend=0.0f;
static int g_dncOctaluxOriginalGradualRayCount=0;
static bool g_dncOctaluxGradualQualityTuned=false;
static const MethodInfo* g_dncGetSunUnityLightColor=nullptr;
static const MethodInfo* g_dncGetSunUnityLightIntensity=nullptr;
static int g_dncManualCommittedProfile=-1;
static u32 g_dncManualCommittedGeneration=0;
static void dncRestoreOctaluxGradualQuality();
// Lodge_V1's game-authored baseline is its Night state.  Day and dusk must be
// calculated above that baseline rather than treating it as an ordinary day.
static bool g_p91IsLodge=false;
static F4 dncLerpF4(F4 from,F4 to,float amount){return F4{from.x+(to.x-from.x)*amount,from.y+(to.y-from.y)*amount,from.z+(to.z-from.z)*amount,from.w+(to.w-from.w)*amount};}
static float dncLerpFloat(float from,float to,float amount){return from+(to-from)*amount;}
static float dncSmoothStep01(float value){if(value<=0)return 0;if(value>=1)return 1;return value*value*(3.0f-2.0f*value);}
static bool dncConvertHDRSunToUnityLight(F4 hdrSun,F4& lightColor,float& lightIntensity);
static void dncAdvanceLightingTransition(u64 now){
    if(!g_dncTransitionInitialised)return;
    float amount=1.0f;if(g_dncTransitionActive&&now&&g_dncTransitionStartedAt&&now<g_dncTransitionStartedAt+DNC_MANUAL_LIGHTING_TRANSITION_MS)amount=dncSmoothStep01((float)(now-g_dncTransitionStartedAt)/(float)DNC_MANUAL_LIGHTING_TRANSITION_MS);
    g_dncDesiredSkyColor=dncLerpF4(g_dncTransitionStartSky,g_dncTransitionTargetSky,amount);g_dncDesiredSunColor=dncLerpF4(g_dncTransitionStartSun,g_dncTransitionTargetSun,amount);g_dncDesiredShadowStrength=dncLerpFloat(g_dncTransitionStartShadowStrength,g_dncTransitionTargetShadowStrength,amount);
    // Match Automatic exactly: interpolate the authored HDR sun first, then
    // ask the game to convert that one intermediate value to Unity light.
    // Falling back to endpoint interpolation keeps the route safe if the
    // game's conversion getters are unavailable during a scene transition.
    if(!dncConvertHDRSunToUnityLight(g_dncDesiredSunColor,g_dncDesiredLightColor,g_dncDesiredIntensity)){g_dncDesiredLightColor=dncLerpF4(g_dncTransitionStartLight,g_dncTransitionTargetLight,amount);g_dncDesiredIntensity=dncLerpFloat(g_dncTransitionStartIntensity,g_dncTransitionTargetIntensity,amount);}
    if(amount>=1.0f)g_dncTransitionActive=false;
}
static void dncSetLightingTransitionTarget(int profile,F4 light,F4 sky,F4 sun,float intensity,float shadowStrength){
    u64 now=p78_clock?p78_clock():0;
    if(!g_dncTransitionInitialised||g_dncTransitionEnvironmentGeneration!=g_dncLightingEnvironmentGeneration){
        g_dncTransitionEnvironmentGeneration=g_dncLightingEnvironmentGeneration;g_dncTransitionTargetProfile=profile;g_dncTransitionInitialised=true;g_dncTransitionActive=false;g_dncTransitionStartedAt=0;g_dncManualGradualSettleUntil=now?now+DNC_MANUAL_GRADUAL_SETTLE_MS:0;
        g_dncTransitionStartLight=g_dncTransitionTargetLight=g_dncDesiredLightColor=light;g_dncTransitionStartSky=g_dncTransitionTargetSky=g_dncDesiredSkyColor=sky;g_dncTransitionStartSun=g_dncTransitionTargetSun=g_dncDesiredSunColor=sun;g_dncTransitionStartIntensity=g_dncTransitionTargetIntensity=g_dncDesiredIntensity=intensity;g_dncTransitionStartShadowStrength=g_dncTransitionTargetShadowStrength=g_dncDesiredShadowStrength=shadowStrength;return;
    }
    if(profile==g_dncTransitionTargetProfile)return;
    dncAdvanceLightingTransition(now);g_dncTransitionStartLight=g_dncDesiredLightColor;g_dncTransitionStartSky=g_dncDesiredSkyColor;g_dncTransitionStartSun=g_dncDesiredSunColor;g_dncTransitionStartIntensity=g_dncDesiredIntensity;g_dncTransitionStartShadowStrength=g_dncDesiredShadowStrength;
    g_dncTransitionTargetLight=light;g_dncTransitionTargetSky=sky;g_dncTransitionTargetSun=sun;g_dncTransitionTargetIntensity=intensity;g_dncTransitionTargetShadowStrength=shadowStrength;g_dncTransitionTargetProfile=profile;g_dncTransitionStartedAt=now;g_dncTransitionActive=now!=0;g_dncManualGradualSettleUntil=now?now+DNC_MANUAL_LIGHTING_TRANSITION_MS+DNC_MANUAL_GRADUAL_SETTLE_MS:0;
    const char* names[4]={"Day","Dusk","Night","Dawn"};char line[220];psprintf(line,"DAY & NIGHT CYCLE: smooth Manual transition to %s started (%llu ms).\r\n",profile>=0&&profile<4?names[profile]:"profile",(unsigned long long)DNC_MANUAL_LIGHTING_TRANSITION_MS);log_raw(line);
}
static bool p91_is_lodge(void* config){char name[256];object_name(config,name,sizeof(name));return contains_i(name,"Lodge_V1");}
static bool dncResolveOctaluxSkyRoute(){
    if(g_dncOctaluxFeature&&g_dncOctaluxSetSkyColor&&g_dncOctaluxUpdateKeywords&&g_dncOctaluxGetCurrentQualitySettings)return true;
    Il2CppClass* featureClass=find_class("TPS.Octalux","OctaluxRenderFeature");
    g_dncOctaluxSetSkyColor=find_method1_hierarchy(featureClass,"SetSkyColor","UnityEngine.Color");
    g_dncOctaluxUpdateKeywords=find_method0_hierarchy(featureClass,"UpdateMutuallyExclusiveKeywords");
    g_dncOctaluxGetCurrentQualitySettings=find_method0_hierarchy(featureClass,"get_CurrentQualitySettings");
    if(!featureClass||!g_dncOctaluxSetSkyColor||!g_dncOctaluxUpdateKeywords||!g_dncOctaluxGetCurrentQualitySettings||!M_Resources_FindAll)return false;
    const Il2CppType* featureType=il2cpp_class_get_type(featureClass);Il2CppObject* typeObject=featureType?il2cpp_type_get_object(featureType):nullptr;if(!typeObject)return false;
    void* args[1]={typeObject};Il2CppArray* features=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,args);if(!features||!features->max_length)return false;
    void* fallback=nullptr;int activeCount=0;const MethodInfo* getActive=nullptr;
    for(uptr i=0;i<features->max_length;++i){void* feature=features->vector[i];if(!feature)continue;if(!fallback)fallback=feature;if(!getActive)getActive=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)feature),"get_isActive");if(getActive&&boxed_bool(invoke(getActive,feature,nullptr),false)){if(!g_dncOctaluxFeature)g_dncOctaluxFeature=feature;++activeCount;}}
    if(!g_dncOctaluxFeature)g_dncOctaluxFeature=fallback;
    char name[180];object_name(g_dncOctaluxFeature,name,sizeof(name));char line[480];psprintf(line,"DAY & NIGHT CYCLE: Octalux route feature=%s candidates=%llu active=%d setter=%s keywordRefresh=%s qualitySettings=%s.\r\n",name[0]?name:"unnamed",(unsigned long long)features->max_length,activeCount,g_dncOctaluxSetSkyColor?"ready":"missing",g_dncOctaluxUpdateKeywords?"ready":"missing",g_dncOctaluxGetCurrentQualitySettings?"ready":"missing");log_raw(line);
    return g_dncOctaluxFeature&&g_dncOctaluxSetSkyColor&&g_dncOctaluxUpdateKeywords&&g_dncOctaluxGetCurrentQualitySettings;
}
static u64 dncLightingConfigKey(void* config){
    if(!config)return 0;i64 persistentId=field_i64(config,"_id",0);if(persistentId)return (u64)persistentId;
    return 0x8000000000000000ull|(u64)(u32)object_id(config);
}
static DNCLightingBaseline* dncFindLightingBaseline(u64 key,bool create){
    if(!key)return nullptr;DNCLightingBaseline* empty=nullptr;
    for(int i=0;i<64;++i){if(g_dncLightingBaselines[i].used&&g_dncLightingBaselines[i].configKey==key)return &g_dncLightingBaselines[i];if(!g_dncLightingBaselines[i].used&&!empty)empty=&g_dncLightingBaselines[i];}
    if(create&&empty){empty->used=true;empty->configKey=key;return empty;}return nullptr;
}
static void p93_reset_calendar_control();
static bool dncApplicationUnavailableOrBusy(){
    static Il2CppClass* appClass=nullptr;if(!appClass)appClass=find_class("TPS.Core","App");
    void* app=appClass?static_ref_field(appClass,"<Instance>k__BackingField"):nullptr;
    return !app||boxed_bool(field_object(app,"<IsQuitting>k__BackingField"))||boxed_bool(field_object(app,"<IsLoading>k__BackingField"));
}
static void dncBeginMuseumTransition(){
    dncRestoreOctaluxGradualQuality();
    // Stored preferences remain untouched. Clear only the outgoing live mode
    // so a different save cannot display or apply it while its own scope is
    // still loading.
    g_dncPreferencesReady=false;g_dncLightingHoldReady=false;g_dncTransitionActive=false;g_dncTransitionInitialised=false;g_dncTransitionTargetProfile=-1;g_dncManualGradualSettleUntil=0;g_p96AutoMode=0;g_dncGameClockLevel=nullptr;g_dncGameTime=nullptr;g_dncGetGameTime=nullptr;g_dncAutoExteriorActive=false;g_dncVirtualHour=-1.0;g_dncManualCommittedProfile=-1;g_dncManualCommittedGeneration=0;++g_dncLightingEnvironmentGeneration;
    // The retail lighting controller performs several authored-light pushes
    // shortly after the playable HUD first appears.  Keep the authored
    // baseline during that startup burst, then restore this museum's saved
    // profile once and let the frame-cadence hold take over.  This delay is
    // limited to museum entry/rebinding; user button changes remain immediate.
    u64 now=p78_clock?p78_clock():0;g_dncLightingEnvironmentReadyAt=now?now+DNC_LIGHTING_STARTUP_SETTLE_MS:0;
}
static void dncReleaseActiveEnvironment(){
    if(g_p91Environment||g_p91Config||g_p91ExteriorLight||g_p91InteriorLight){p93_reset_calendar_control();dncBeginMuseumTransition();log_raw("DAY & NIGHT CYCLE: level transition detected; UI discovery and automatic lighting suspended.\r\n");}
    g_p91Config=nullptr;g_p91ExteriorLight=nullptr;g_p91InteriorLight=nullptr;g_p91Environment=nullptr;g_p91Captured=false;g_dncPushLightingSettings=nullptr;g_dncGetSunUnityLightColor=nullptr;g_dncGetSunUnityLightIntensity=nullptr;
}
static bool p91_resolve_lighting_manager(){
    if(g_p91LightingManager)return true;
    static unsigned retry=0;if((retry++%10)!=0)return false;
    if(!g_p91GameManagerClass)g_p91GameManagerClass=find_class_exposing_method("get_LightingManager");if(!g_p91GameManagerClass)return false;
    void* game=static_ref_field(g_p91GameManagerClass,"<Instance>k__BackingField");
    if(!game){const MethodInfo* getInstance=find_method0_hierarchy(g_p91GameManagerClass,"get_Instance");if(getInstance)game=invoke(getInstance,nullptr,nullptr);}
    const MethodInfo* getLighting=find_method0_hierarchy(g_p91GameManagerClass,"get_LightingManager");
    void* manager=(getLighting&&game)?invoke(getLighting,game,nullptr):nullptr;if(!manager)return false;
    g_p91LightingManager=manager;log_raw("DAY & NIGHT CYCLE: lighting manager acquired after startup; environment binding resumed.\r\n");return true;
}
// The active scene can be destroyed between Day & Night Cycle ticks. Never
// write through the previous scene's Light/LightingConfig pointers; instead,
// re-check the persistent LightingManager and bind the current environment.
static bool p91_bind_active_environment(){
    if(dncApplicationUnavailableOrBusy()){dncReleaseActiveEnvironment();return false;}
    if(!p91_resolve_lighting_manager())return false;
    void* environment=field_object(g_p91LightingManager,"_activeLightingEnvironment");
    if(!environment){dncReleaseActiveEnvironment();return false;}
    void* config=field_object(environment,"_lightingConfig");
    if(!config)config=field_object(environment,"LightingConfig");
    void* exterior=field_object(environment,"_exteriorLight");
    void* interior=field_object(environment,"_interiorLight");
    if(!config||!exterior||!interior){dncReleaseActiveEnvironment();return false;}
    if(config!=g_p91Config||exterior!=g_p91ExteriorLight||interior!=g_p91InteriorLight||environment!=g_p91Environment){
        // The old CalendarUI is destroyed during a museum transition.  Never
        // retain its cloned button/RectTransform into the incoming scene.
        p93_reset_calendar_control();
        dncBeginMuseumTransition();
        g_p91Config=config;g_p91ExteriorLight=exterior;g_p91InteriorLight=interior;g_p91Environment=environment;g_p91Captured=false;g_p91IsLodge=p91_is_lodge(config);
        g_dncPushLightingSettings=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)environment),"PushLightingRenderSettings");Il2CppClass* configClass=il2cpp_object_get_class((Il2CppObject*)config);g_dncGetSunUnityLightColor=find_method0_hierarchy(configClass,"get_SunUnityLightColor");g_dncGetSunUnityLightIntensity=find_method0_hierarchy(configClass,"get_SunUnityLightIntensity");dncResolveOctaluxSkyRoute();
        // Lodge's authored/default lighting is Night. Other museums open in Day.
        g_p91Profile=g_p91IsLodge?2:0;
        log_raw(g_p91IsLodge?"DAY & NIGHT CYCLE: Wailon Lodge bound; its authored night is selected by default.\r\n":"DAY & NIGHT CYCLE: exterior lighting bound.\r\n");
    }
    return true;
}
// First playable UI control.  It deliberately uses a cloned native CalendarUI
// button for the game's own visual language, but disables its original click
// behaviour and handles only our small hit target. No external UI object is
// queried or modified here.
static void* g_p93ControlGO=nullptr; static void* g_p93ControlTr=nullptr; static bool g_p93MouseWasDown=false;
static void* g_p93TimelineHostGO=nullptr;static void* g_p93SpeedDonorGO=nullptr;static void* g_p93DateLabelDonorGO=nullptr;
static bool g_p96RightMouseWasDown=false;
static int g_p96LastProfile=-1;
static bool g_p97PopoverOpen=false;
static bool g_p97PopoverBuilt=false;
static u64 g_p97PopoverBuildStartedAt=0;
static void* g_p105InputShieldGO=nullptr;static void* g_p105InputShieldTr=nullptr;
static void* g_p97WeekGO=nullptr;static void* g_p97WeekTr=nullptr;static void* g_p97WeekLabelGO=nullptr;static void* g_p97WeekLabelTr=nullptr;static void* g_p97WeekLabel=nullptr;
static void* g_p97MonthGO=nullptr;static void* g_p97MonthTr=nullptr;static void* g_p97MonthLabelGO=nullptr;static void* g_p97MonthLabelTr=nullptr;static void* g_p97MonthLabel=nullptr;
static void* g_p97WeekPanelImage=nullptr;static void* g_p97WeekIconImage=nullptr;static void* g_p97MonthPanelImage=nullptr;static void* g_p97MonthIconImage=nullptr;
static void* g_p97WeekArtGO=nullptr;static void* g_p97MonthArtGO=nullptr;
static int g_p97WeekVisual=-1,g_p97MonthVisual=-1;
static void* g_p98PanelGO=nullptr;static void* g_p98PanelTr=nullptr;static void* g_p98ShadowGO=nullptr;static void* g_p98HeaderGO=nullptr;static void* g_p98HeaderTr=nullptr;static void* g_p98TileOuterTr=nullptr;static void* g_p98CloseGO=nullptr;static void* g_p98CloseTr=nullptr;
static void* g_p99ManualGO=nullptr;static void* g_p99ManualTr=nullptr;static void* g_p99ManualImage=nullptr;static void* g_p99ManualLabelGO=nullptr;static void* g_p99ManualLabel=nullptr;
static void* g_p99CalendarGO=nullptr;static void* g_p99CalendarTr=nullptr;static void* g_p99CalendarImage=nullptr;static void* g_p99CalendarLabelGO=nullptr;static void* g_p99CalendarLabel=nullptr;
static void* g_p99ManualIconTr=nullptr;static void* g_p99CalendarIconTr=nullptr;
static void* g_p99ManualFillGO=nullptr;static void* g_p99CalendarFillGO=nullptr;
static void* g_p99NativeTabDonorGO=nullptr;
static int g_p99ActiveTab=0;static int g_p99ManualVisual=-1,g_p99CalendarVisual=-1;
struct P99NativeTabVisual { void* idleGO;void* hoverGO;void* selectedGO;void* lockedGO;void* iconImage;bool ready; };
static P99NativeTabVisual g_p99ManualNative{},g_p99CalendarNative{};
// ACCEPTED TAB BASELINE: frozen by explicit user request. Future UI
// work must treat these values and assets as invariants unless the user
// explicitly asks to reopen tab design. Runtime state may only toggle the
// existing native visual layers; it must not mutate tab/icon geometry.
static constexpr int P99_LOCKED_TAB_WIDTH=158;
static constexpr int P99_LOCKED_TAB_HEIGHT=107;
static constexpr int P99_LOCKED_MANUAL_X=-538;
static constexpr int P99_LOCKED_AUTOMATED_X=-404;
static constexpr int P99_LOCKED_TAB_Y=760;
static constexpr int P99_LOCKED_OVERLAP=24;
static constexpr const char* P99_LOCKED_MANUAL_ICON="iconInteractions-Gorge_UI_InGame_Inspector_Icons_Interactions_Staff_JobAssignment";
static constexpr const char* P99_LOCKED_AUTOMATED_ICON="IconsTabs-Gorge_UI_InGame_Inspector_Icons_Tabs_General_Timetable";
static constexpr F4 P99_LOCKED_ICON_TINT{.84f,.24f,.02f,1};
static_assert(P99_LOCKED_AUTOMATED_X-P99_LOCKED_MANUAL_X==P99_LOCKED_TAB_WIDTH-P99_LOCKED_OVERLAP,"Accepted tab overlap changed");
static_assert(P99_LOCKED_TAB_WIDTH==158&&P99_LOCKED_TAB_HEIGHT==107,"Accepted native tab footprint changed");
static void* g_p101ManualGO[4]{};static void* g_p101ManualTr[4]{};static void* g_p101ManualLabelGO[4]{};static void* g_p101ManualLabelTr[4]{};static void* g_p101ManualLabel[4]{};static void* g_p101ManualArtGO[4]{};static void* g_p101ManualPanelImage[4]{};static void* g_p101ManualIconGO[4]{};static void* g_p101ManualIconImage[4]{};static int g_p101ManualVisual[4]={-1,-1,-1,-1};
static void* g_p102ManualHeadingGO=nullptr;static void* g_p102CalendarHeadingGO=nullptr;static void* g_p102CalendarSequenceGO=nullptr;static void* g_p102CalendarSequenceLabel=nullptr;static void* g_p102ManualStatusGO=nullptr;static void* g_p102ManualStatusLabelGO=nullptr;static void* g_p102ManualStatusLabel=nullptr;static void* g_p102CalendarStatusGO=nullptr;static void* g_p102CalendarStatusLabelGO=nullptr;static void* g_p102CalendarStatusLabel=nullptr;
static void* g_p106TimelineGO=nullptr;static void* g_p106TimelineTr=nullptr;static void* g_p106TimelineSegmentGO[4]{};static void* g_p106TimelineSegmentTr[4]{};static void* g_p106TimelineHandleGO[3]{};static void* g_p106TimelineHandleTr[3]{};static int g_p106DragBoundary=-1;static bool g_p106DragDirty=false;
static void* g_p106TimingSummaryGO=nullptr;static void* g_p106ResetGO=nullptr;static void* g_p106ResetTr=nullptr;static void* g_p106ResetImage=nullptr;static void* g_p106ResetLabelGO=nullptr;static void* g_p106ResetLabel=nullptr;static void* g_p106ResetGlyphImage=nullptr;static int g_p106ResetVisual=-1;
static void* g_p95ColourLayerGO=nullptr; static void* g_p95ColourLayerTr=nullptr; static void* g_p95ColourLayerImage=nullptr;
static void* g_p95PanelImage=nullptr; static void* g_p95PanelTr=nullptr; static void* g_p95IconGO=nullptr; static void* g_p95IconImage=nullptr; static void* g_p95IconTr=nullptr;
static void* g_p95Sprites[16]{}; static DNCGCHandle g_p95TextureHandles[16]{},g_p95SpriteHandles[16]{},g_p95TooltipStringHandles[12]{};
static Il2CppString* g_p95TooltipStrings[12]{}; static bool g_p95IconsTried=false,g_p95IconApplied=false; static int g_p95TooltipProfile=-1,g_p95TooltipControlId=0;
static void* g_p95TooltipTarget=nullptr;static Il2CppFieldInfo* g_p95TooltipField=nullptr;
static DNCGCHandle g_p103TooltipStringHandles[10]{};
static Il2CppString* g_p103TooltipStrings[10]{};
static void* g_p103TabTooltipTargets[2]{};
static bool g_p103TabHovered[2]{};
static int g_p95ButtonVariant=0;
#include "DayNightPreferences.h"
static int g_dncManualProfile=0;
// Full-strength hold lengths in hundredths of a virtual hour. The four smooth
// transition windows retain their accepted 3h/2h/1h/2h lengths.
static u32 g_dncPhaseHoldUnits[4]={100,900,200,400};
// UI phases are presented in chronological Dawn/Day/Dusk/Night order. Each
// visible total includes that state's following transition, so the four bar
// sections and labels add up to the complete 24-hour virtual day.
static constexpr u32 DNC_PHASE_TRANSITION_UNITS[4]={200,100,200,300};
static constexpr u32 DNC_PHASE_VISIBLE_TOTAL=2400u;
static u32 g_dncAppliedPreferenceGeneration=0;
static void p97_clear_popover_references(){
    g_p97PopoverOpen=false;g_p97PopoverBuilt=false;g_p97PopoverBuildStartedAt=0;g_p105InputShieldGO=nullptr;g_p105InputShieldTr=nullptr;
    g_p97WeekGO=nullptr;g_p97WeekTr=nullptr;g_p97WeekLabelGO=nullptr;g_p97WeekLabelTr=nullptr;g_p97WeekLabel=nullptr;g_p97MonthGO=nullptr;g_p97MonthTr=nullptr;g_p97MonthLabelGO=nullptr;g_p97MonthLabelTr=nullptr;g_p97MonthLabel=nullptr;
    g_p97WeekPanelImage=nullptr;g_p97WeekIconImage=nullptr;g_p97MonthPanelImage=nullptr;g_p97MonthIconImage=nullptr;g_p97WeekArtGO=nullptr;g_p97MonthArtGO=nullptr;g_p97WeekVisual=-1;g_p97MonthVisual=-1;
    g_p98PanelGO=nullptr;g_p98PanelTr=nullptr;g_p98ShadowGO=nullptr;g_p98HeaderGO=nullptr;g_p98HeaderTr=nullptr;g_p98TileOuterTr=nullptr;g_p98CloseGO=nullptr;g_p98CloseTr=nullptr;
    g_p99ManualGO=nullptr;g_p99ManualTr=nullptr;g_p99ManualImage=nullptr;g_p99ManualLabelGO=nullptr;g_p99ManualLabel=nullptr;g_p99CalendarGO=nullptr;g_p99CalendarTr=nullptr;g_p99CalendarImage=nullptr;g_p99CalendarLabelGO=nullptr;g_p99CalendarLabel=nullptr;g_p99ManualIconTr=nullptr;g_p99CalendarIconTr=nullptr;g_p99ManualFillGO=nullptr;g_p99CalendarFillGO=nullptr;g_p99NativeTabDonorGO=nullptr;g_p99ManualNative=P99NativeTabVisual{};g_p99CalendarNative=P99NativeTabVisual{};g_p99ActiveTab=0;g_p99ManualVisual=-1;g_p99CalendarVisual=-1;
    for(int i=0;i<4;++i){g_p101ManualGO[i]=nullptr;g_p101ManualTr[i]=nullptr;g_p101ManualLabelGO[i]=nullptr;g_p101ManualLabelTr[i]=nullptr;g_p101ManualLabel[i]=nullptr;g_p101ManualArtGO[i]=nullptr;g_p101ManualPanelImage[i]=nullptr;g_p101ManualIconGO[i]=nullptr;g_p101ManualIconImage[i]=nullptr;g_p101ManualVisual[i]=-1;g_p106TimelineSegmentGO[i]=nullptr;g_p106TimelineSegmentTr[i]=nullptr;}
    for(int i=0;i<3;++i){g_p106TimelineHandleGO[i]=nullptr;g_p106TimelineHandleTr[i]=nullptr;}
    g_p102ManualHeadingGO=nullptr;g_p102CalendarHeadingGO=nullptr;g_p102CalendarSequenceGO=nullptr;g_p102CalendarSequenceLabel=nullptr;g_p102ManualStatusGO=nullptr;g_p102ManualStatusLabelGO=nullptr;g_p102ManualStatusLabel=nullptr;g_p102CalendarStatusGO=nullptr;g_p102CalendarStatusLabelGO=nullptr;g_p102CalendarStatusLabel=nullptr;
    g_p106TimelineGO=nullptr;g_p106TimelineTr=nullptr;g_p106DragBoundary=-1;g_p106DragDirty=false;g_p106TimingSummaryGO=nullptr;g_p106ResetGO=nullptr;g_p106ResetTr=nullptr;g_p106ResetImage=nullptr;g_p106ResetLabelGO=nullptr;g_p106ResetLabel=nullptr;g_p106ResetGlyphImage=nullptr;g_p106ResetVisual=-1;
    for(int i=0;i<2;++i){g_p103TabTooltipTargets[i]=nullptr;g_p103TabHovered[i]=false;}
}
static void p93_reset_calendar_control(){
    g_p93ControlGO=nullptr;g_p93ControlTr=nullptr;g_p93TimelineHostGO=nullptr;g_p93SpeedDonorGO=nullptr;g_p93DateLabelDonorGO=nullptr;g_p93MouseWasDown=false;g_p96RightMouseWasDown=false;p97_clear_popover_references();
    g_p95ColourLayerGO=nullptr;g_p95ColourLayerTr=nullptr;g_p95ColourLayerImage=nullptr;g_p95PanelImage=nullptr;g_p95PanelTr=nullptr;g_p95IconGO=nullptr;g_p95IconImage=nullptr;g_p95IconTr=nullptr;g_p95ButtonVariant=0;g_p95IconApplied=false;g_p95TooltipProfile=-1;g_p95TooltipControlId=0;g_p95TooltipTarget=nullptr;g_p95TooltipField=nullptr;
}
struct P95TooltipText { Il2CppString* development; int term; int padding; };

static bool p103_is_transform_in_tree(void* tr,void* rootTr){
    if(!tr||!rootTr)return false;if(tr==rootTr)return true;if(!M_Transform_IsChildOf)return false;void* a[1]={rootTr};return boxed_bool(invoke(M_Transform_IsChildOf,tr,a),false);
}
static Il2CppArray* p103_find_all_components(Il2CppClass* componentClass){
    if(!componentClass||!M_Resources_FindAll)return nullptr;const Il2CppType* type=il2cpp_class_get_type(componentClass);Il2CppObject* typeObject=type?il2cpp_type_get_object(type):nullptr;if(!typeObject)return nullptr;void* a[1]={typeObject};return (Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);
}
static void p103_disable_behaviour_tree(void* root,const char* namespc,const char* className){
    if(!root||!M_GameObject_get_transform||!M_Component_get_transform||!M_Behaviour_set_enabled)return;Il2CppClass* componentClass=find_class(namespc,className);Il2CppArray* all=p103_find_all_components(componentClass);void* rootTr=invoke(M_GameObject_get_transform,root,nullptr);if(!all||!rootTr)return;bool off=false;void* a[1]={&off};for(uptr i=0;i<all->max_length;++i){void* component=all->vector[i];void* tr=component?invoke(M_Component_get_transform,component,nullptr):nullptr;if(p103_is_transform_in_tree(tr,rootTr))invoke(M_Behaviour_set_enabled,component,a);}
}
static bool p103_set_native_tooltip(void* root,const char* text,int stringIndex,void** targetOut=nullptr){
    if(!root||!text||stringIndex<0||stringIndex>=10||!C_TooltipSpawner||!M_Component_get_transform||!M_GameObject_get_transform||!M_Behaviour_set_enabled||!il2cpp_field_set_value)return false;Il2CppArray* all=p103_find_all_components(C_TooltipSpawner);void* rootTr=invoke(M_GameObject_get_transform,root,nullptr);if(!all||!rootTr)return false;void* target=nullptr;void* fallback=nullptr;
    for(uptr i=0;i<all->max_length;++i){void* tip=all->vector[i];void* tr=tip?invoke(M_Component_get_transform,tip,nullptr):nullptr;if(!p103_is_transform_in_tree(tr,rootTr))continue;if(tr==rootTr&&!target)target=tip;if(!fallback)fallback=tip;}
    if(!target)target=fallback;if(!target)return false;
    // Keep one authoritative spawner so a donor can never emit duplicate or
    // stale speed-control hover text.
    for(uptr i=0;i<all->max_length;++i){void* tip=all->vector[i];void* tr=tip?invoke(M_Component_get_transform,tip,nullptr):nullptr;if(!p103_is_transform_in_tree(tr,rootTr))continue;bool enabled=tip==target;void* a[1]={&enabled};invoke(M_Behaviour_set_enabled,tip,a);}
    Il2CppFieldInfo* tooltipField=find_field_hierarchy(il2cpp_object_get_class((Il2CppObject*)target),"_tooltip");if(!tooltipField)return false;if(!g_p103TooltipStrings[stringIndex]){g_p103TooltipStrings[stringIndex]=il2cpp_string_new(text);if(g_p103TooltipStrings[stringIndex]&&il2cpp_gchandle_new)g_p103TooltipStringHandles[stringIndex]=il2cpp_gchandle_new((Il2CppObject*)g_p103TooltipStrings[stringIndex],false);}if(!g_p103TooltipStrings[stringIndex])return false;P95TooltipText value{g_p103TooltipStrings[stringIndex],0,0};il2cpp_field_set_value((Il2CppObject*)target,tooltipField,&value);if(M_Tooltip_Reset)invoke(M_Tooltip_Reset,target,nullptr);if(targetOut)*targetOut=target;return true;
}
static void p103_make_tree_invisible(void* root,bool raycastTarget){
    if(!root||!C_Graphic||!M_Component_get_transform||!M_GameObject_get_transform)return;Il2CppArray* all=p103_find_all_components(C_Graphic);void* rootTr=invoke(M_GameObject_get_transform,root,nullptr);if(!all||!rootTr)return;F4 transparent{1,1,1,0};void* colorArg[1]={&transparent};void* raycastArg[1]={&raycastTarget};for(uptr i=0;i<all->max_length;++i){void* graphic=all->vector[i];void* tr=graphic?invoke(M_Component_get_transform,graphic,nullptr):nullptr;if(!p103_is_transform_in_tree(tr,rootTr))continue;if(M_Graphic_set_color)invoke(M_Graphic_set_color,graphic,colorArg);if(M_Graphic_set_raycastTarget)invoke(M_Graphic_set_raycastTarget,graphic,raycastArg);}
}
static bool p103_create_tooltip_proxy(void* donor,void* parent,const char* text,int stringIndex,bool nativeRaycast,void** targetOut=nullptr){
    if(!donor||!parent||!M_Object_Instantiate||!M_Transform_SetParent)return false;void* go=invoke(M_Object_Instantiate,nullptr,&donor);void* tr=go&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!go||!tr)return false;bool keep=false;void* pa[2]={parent,&keep};invoke(M_Transform_SetParent,tr,pa);F2 zero{0,0},one{1,1},centre{.5f,.5f};set_vec2_prop(tr,M_RectTransform_set_anchorMin,zero);set_vec2_prop(tr,M_RectTransform_set_anchorMax,one);set_vec2_prop(tr,M_RectTransform_set_pivot,centre);set_vec2_prop(tr,M_RectTransform_set_anchoredPosition,zero);set_vec2_prop(tr,M_RectTransform_set_sizeDelta,zero);p103_disable_behaviour_tree(go,"UnityEngine.UI","Button");p103_disable_behaviour_tree(go,"TPS.Core.UI","UIButton");p103_disable_behaviour_tree(go,"TPS.Core.UI","UIButtonAudio");p103_disable_behaviour_tree(go,"TPS.Core.UI","UINavigationFocus");p103_disable_behaviour_tree(go,"UnityEngine","Animator");p103_make_tree_invisible(go,nativeRaycast);bool installed=p103_set_native_tooltip(go,text,stringIndex,targetOut);if(M_Transform_SetAsLastSibling)invoke(M_Transform_SetAsLastSibling,tr,nullptr);return installed;
}
static void p103_drive_tab_tooltip(int index,bool hovered,void* tooltipRoot,const F3& mousePosition){
    if(index<0||index>1)return;void* target=g_p103TabTooltipTargets[index];if(!target)return;if(!M_Tooltip_CursorOver)M_Tooltip_CursorOver=il2cpp_class_get_method_from_name(C_TooltipSpawner,"CursorOver",3);if(!M_Tooltip_CursorOut)M_Tooltip_CursorOut=find_method0_hierarchy(C_TooltipSpawner,"CursorOut");if(hovered&&M_Tooltip_CursorOver){int controllerType=0;void* a[3]={tooltipRoot,(void*)&mousePosition,&controllerType};invoke(M_Tooltip_CursorOver,target,a);}else if(g_p103TabHovered[index]&&M_Tooltip_CursorOut)invoke(M_Tooltip_CursorOut,target,nullptr);g_p103TabHovered[index]=hovered;
}

static bool p95_read_icon(const char* filename,u8* data,DWORD capacity,DWORD& size){
    size=0;if(!filename||!data||!capacity||!pGetModuleFileNameW||!pCreateFileW||!pReadFile||!pSetFilePointer||!pCloseHandle)return false;
    wchar_t path[1024];DWORD n=pGetModuleFileNameW(nullptr,path,(DWORD)(sizeof(path)/sizeof(path[0])));if(!n||n>=sizeof(path)/sizeof(path[0]))return false;
    for(int i=(int)n-1;i>=0;--i)if(path[i]==L'\\'){path[i+1]=0;break;}
    wappend(path,sizeof(path)/sizeof(path[0]),L"Mods\\assets\\");wappend_ascii(path,sizeof(path)/sizeof(path[0]),filename);
    HANDLE h=pCreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){char line[220];psprintf(line,"DAY & NIGHT CYCLE: missing Mods\\assets\\%s; native fallback retained.\r\n",filename);log_raw(line);return false;}
    DWORD bytes=pSetFilePointer(h,0,nullptr,FILE_END);pSetFilePointer(h,0,nullptr,0);
    if(!bytes||bytes>capacity){pCloseHandle(h);return false;}
    DWORD read=0;bool ok=pReadFile(h,data,bytes,&read,nullptr)&&read==bytes;pCloseHandle(h);if(ok)size=bytes;return ok;
}

static bool p95_load_sprite(int index,const char* filename,const Rect4& rect){
    static u8 data[512*1024];DWORD bytes=0;if(!p95_read_icon(filename,data,(DWORD)sizeof(data),bytes))return false;
    Il2CppArray* raw=il2cpp_array_new(C_Byte,bytes);if(!raw)return false;memcpy((u8*)raw+32,data,bytes);
    Il2CppObject* texture=il2cpp_object_new(C_Texture2D);if(!texture)return false;int w=2,h=2;void* ctorArgs[2]={&w,&h};invoke(M_Texture2D_ctor,texture,ctorArgs);
    bool nonReadable=true;void* loadArgs[3]={texture,raw,&nonReadable};if(!boxed_bool(invoke(M_ImageConversion_LoadImage,nullptr,loadArgs),false))return false;
    if(il2cpp_gchandle_new)g_p95TextureHandles[index]=il2cpp_gchandle_new(texture,false);
    F2 pivot{0.5f,0.5f};Rect4 spriteRect=rect;void* createArgs[3]={texture,&spriteRect,&pivot};g_p95Sprites[index]=invoke(M_Sprite_Create,nullptr,createArgs);if(!g_p95Sprites[index])return false;
    if(il2cpp_gchandle_new)g_p95SpriteHandles[index]=il2cpp_gchandle_new((Il2CppObject*)g_p95Sprites[index],false);return true;
}

static bool p95_load_user_icons(){
    if(g_p95IconsTried)return g_p95Sprites[0]!=nullptr;
    // Resolve only now, on the proven game-thread tick after CalendarUI exists.
    if(!C_Byte)C_Byte=find_class("System","Byte");if(!C_Texture2D)C_Texture2D=find_class("UnityEngine","Texture2D");if(!C_Sprite)C_Sprite=find_class("UnityEngine","Sprite");if(!C_ImageConversion)C_ImageConversion=find_class("UnityEngine","ImageConversion");if(!C_UIImage)C_UIImage=find_class("UnityEngine.UI","Image");if(!C_TooltipSpawner)C_TooltipSpawner=find_class("TPS.Game.UI","TooltipSpawner");
    if(C_Texture2D&&!M_Texture2D_ctor)M_Texture2D_ctor=find_method_param2(C_Texture2D,".ctor","System.Int32","System.Int32");
    if(C_ImageConversion&&!M_ImageConversion_LoadImage)M_ImageConversion_LoadImage=find_method_param3(C_ImageConversion,"LoadImage","UnityEngine.Texture2D","System.Byte[]","System.Boolean");
    if(C_Sprite&&!M_Sprite_Create)M_Sprite_Create=find_method_param3(C_Sprite,"Create","UnityEngine.Texture2D","UnityEngine.Rect","UnityEngine.Vector2");
    if(C_UIImage&&!M_UIImage_set_sprite)M_UIImage_set_sprite=find_method_param1(C_UIImage,"set_sprite","UnityEngine.Sprite");
    if(C_UIImage&&!M_UIImage_set_overrideSprite)M_UIImage_set_overrideSprite=find_method_param1(C_UIImage,"set_overrideSprite","UnityEngine.Sprite");
    if(C_UIImage&&!M_UIImage_set_preserveAspect)M_UIImage_set_preserveAspect=find_method_param1(C_UIImage,"set_preserveAspect","System.Boolean");
    if(C_TooltipSpawner&&!M_Tooltip_Reset)M_Tooltip_Reset=find_method0_hierarchy(C_TooltipSpawner,"Reset");
    g_p95IconsTried=true;
    if(!C_Byte||!C_Texture2D||!C_UIImage||!M_Texture2D_ctor||!M_ImageConversion_LoadImage||!M_Sprite_Create||!M_UIImage_set_sprite||!M_UIImage_set_overrideSprite||!M_UIImage_set_preserveAspect||!il2cpp_array_new){log_raw("DAY & NIGHT CYCLE: optional four-file icon API unavailable; native fallback retained.\r\n");return false;}
    const char* files[4]={"Day.png","Dusk.png","Night.png","Dawn.png"};
    Rect4 rects[4]={{18,27,383,367},{0,27,420,154},{37,51,346,318},{0,27,420,250}};
    for(int i=0;i<4;++i)if(!p95_load_sprite(i,files[i],rects[i]))return false;
    // Genuine 128x128 base-game button artwork supplied from
    // UI_Atlas_Shared_Buttons. Each state has Idle, Hovered and Selected art.
    const char* buttons[12]={
        "DayButton_Idle.png","DayButton_Hovered.png","DayButton_Selected.png",
        "DuskButton_Idle.png","DuskButton_Hovered.png","DuskButton_Selected.png",
        "NightButton_Idle.png","NightButton_Hovered.png","NightButton_Selected.png",
        "DawnButton_Idle.png","DawnButton_Hovered.png","DawnButton_Selected.png"};
    int loadedButtons=0;for(int i=0;i<12;++i)if(p95_load_sprite(4+i,buttons[i],Rect4{0,0,139,126}))++loadedButtons;
    char loadedLine[180];psprintf(loadedLine,"DAY & NIGHT CYCLE: four icons loaded; base-game button variants loaded=%d/12 (source size 139x126).\r\n",loadedButtons);log_raw(loadedLine);return true;
}

static void p95_update_tooltip(){
    if(!g_p93ControlGO||!g_p93ControlTr||!C_TooltipSpawner||!M_Tooltip_Reset||!M_Resources_FindAll||!M_Component_get_transform||!M_Transform_IsChildOf||!il2cpp_field_set_value)return;
    int controlId=object_id(g_p93ControlGO);
    const char* manualLabels[4]={"Day & Night Cycle - Day (Manual)","Day & Night Cycle - Dusk (Manual)","Day & Night Cycle - Night (Manual)","Day & Night Cycle - Dawn (Manual)"};
    const char* weekLabels[4]={"Day & Night Cycle - Day (Automatic: Weekly)","Day & Night Cycle - Dusk (Automatic: Weekly)","Day & Night Cycle - Night (Automatic: Weekly)","Day & Night Cycle - Dawn (Automatic: Weekly)"};
    const char* autoLabels[4]={"Day & Night Cycle - Day (Automatic: Monthly)","Day & Night Cycle - Dusk (Automatic: Monthly)","Day & Night Cycle - Night (Automatic: Monthly)","Day & Night Cycle - Dawn (Automatic: Monthly)"};
    const char* label=g_p96AutoMode==1?weekLabels[g_p91Profile]:(g_p96AutoMode==2?autoLabels[g_p91Profile]:manualLabels[g_p91Profile]);int tooltipIndex=g_p96AutoMode*4+g_p91Profile;
    if(g_p95TooltipTarget&&g_p95TooltipField&&g_p95TooltipControlId==controlId&&g_p95TooltipProfile==tooltipIndex)return;
    if(!g_p95TooltipTarget||!g_p95TooltipField||g_p95TooltipControlId!=controlId){Il2CppArray* all=p103_find_all_components(C_TooltipSpawner);if(!all)return;void* target=nullptr;void* fallback=nullptr;for(uptr i=0;i<all->max_length;++i){void* tip=all->vector[i];void* tr=tip?invoke(M_Component_get_transform,tip,nullptr):nullptr;if(!tr)continue;if(tr==g_p93ControlTr){target=tip;break;}void* owns[1]={g_p93ControlTr};if(!fallback&&boxed_bool(invoke(M_Transform_IsChildOf,tr,owns),false))fallback=tip;}if(!target)target=fallback;if(!target)return;g_p95TooltipTarget=target;g_p95TooltipField=find_field_hierarchy(il2cpp_object_get_class((Il2CppObject*)target),"_tooltip");g_p95TooltipControlId=controlId;if(!g_p95TooltipField)return;}
    if(!g_p95TooltipStrings[tooltipIndex]){g_p95TooltipStrings[tooltipIndex]=il2cpp_string_new(label);if(g_p95TooltipStrings[tooltipIndex]&&il2cpp_gchandle_new)g_p95TooltipStringHandles[tooltipIndex]=il2cpp_gchandle_new((Il2CppObject*)g_p95TooltipStrings[tooltipIndex],false);}if(!g_p95TooltipStrings[tooltipIndex])return;
    P95TooltipText value{g_p95TooltipStrings[tooltipIndex],0,0};il2cpp_field_set_value((Il2CppObject*)g_p95TooltipTarget,g_p95TooltipField,&value);invoke(M_Tooltip_Reset,g_p95TooltipTarget,nullptr);char line[180];psprintf(line,"DAY & NIGHT CYCLE: tooltip updated to '%s'.\r\n",label);log_raw(line);g_p95TooltipControlId=controlId;g_p95TooltipProfile=tooltipIndex;
}

static void p95_apply_user_icon(){
    if(!g_p93ControlTr||!p95_load_user_icons()||!C_UIImage||!M_Resources_FindAll||!M_Component_get_transform||!M_Transform_IsChildOf)return;
    if(!g_p95PanelImage||!g_p95PanelTr||!g_p95IconGO||!g_p95IconImage||!g_p95IconTr){
        Il2CppArray* images=p103_find_all_components(C_UIImage);if(!images)return;
        for(uptr i=0;i<images->max_length;++i){void* image=images->vector[i];void* tr=image?invoke(M_Component_get_transform,image,nullptr):nullptr;if(!tr||tr==g_p93ControlTr)continue;void* owns[1]={g_p93ControlTr};if(!boxed_bool(invoke(M_Transform_IsChildOf,tr,owns),false))continue;void* igo=invoke(M_Component_get_gameObject,image,nullptr);char name[80];object_name(igo,name,sizeof(name));if(streq(name,"Panel")){g_p95PanelImage=image;g_p95PanelTr=tr;continue;}if(streq(name,"Icon")){g_p95IconGO=igo;g_p95IconImage=image;g_p95IconTr=tr;}}
    }
    // The donor Selectable owns Panel and can restore its yellow sprite at any
    // time. Clone the component-light Icon object instead, expand it to the
    // Panel bounds, and place it between Panel and the real state icon.
    if(!g_p95ColourLayerGO&&g_p95Sprites[4]&&g_p95IconGO&&g_p95PanelTr&&M_Object_Instantiate&&M_Transform_SetParent){g_p95ColourLayerGO=invoke(M_Object_Instantiate,nullptr,&g_p95IconGO);g_p95ColourLayerTr=g_p95ColourLayerGO?invoke(M_GameObject_get_transform,g_p95ColourLayerGO,nullptr):nullptr;if(g_p95ColourLayerTr){bool keepWorld=false;void* parentArgs[2]={g_p93ControlTr,&keepWorld};invoke(M_Transform_SetParent,g_p95ColourLayerTr,parentArgs);Rect4 panelRect{};F2 centre{0.5f,0.5f},zero{0,0},size{120,120};if(get_rect4(g_p95PanelTr,panelRect)){size.x=panelRect.width;size.y=panelRect.height;}set_vec2_prop(g_p95ColourLayerTr,M_RectTransform_set_anchorMin,centre);set_vec2_prop(g_p95ColourLayerTr,M_RectTransform_set_anchorMax,centre);set_vec2_prop(g_p95ColourLayerTr,M_RectTransform_set_pivot,centre);set_vec2_prop(g_p95ColourLayerTr,M_RectTransform_set_anchoredPosition,zero);set_vec2_prop(g_p95ColourLayerTr,M_RectTransform_set_sizeDelta,size);const Il2CppType* it=il2cpp_class_get_type(C_UIImage);Il2CppObject* ito=it?il2cpp_type_get_object(it):nullptr;void* ca[1]={ito};Il2CppArray* parts=ito?(Il2CppArray*)invoke(M_GameObject_GetComponents,g_p95ColourLayerGO,ca):nullptr;if(parts&&parts->max_length)g_p95ColourLayerImage=parts->vector[0];}}
    void* buttonImage=g_p95ColourLayerImage?g_p95ColourLayerImage:g_p95PanelImage;
    if(buttonImage){const int bases[4]={4,7,10,13};int index=bases[g_p91Profile]+g_p95ButtonVariant;if(g_p95Sprites[index]){void* s[1]={g_p95Sprites[index]};invoke(M_UIImage_set_sprite,buttonImage,s);invoke(M_UIImage_set_overrideSprite,buttonImage,s);bool preserve=false;void* pa[1]={&preserve};invoke(M_UIImage_set_preserveAspect,buttonImage,pa);}if(M_Graphic_set_raycastTarget&&buttonImage==g_p95ColourLayerImage){bool off=false;void* ra[1]={&off};invoke(M_Graphic_set_raycastTarget,buttonImage,ra);}if(M_Graphic_set_color){F4 white{1,1,1,1};void* c[1]={&white};invoke(M_Graphic_set_color,buttonImage,c);}}
    if(g_p95ColourLayerImage&&g_p95PanelImage&&M_Graphic_set_color){F4 hidden{1,1,1,0};void* c[1]={&hidden};invoke(M_Graphic_set_color,g_p95PanelImage,c);}
    // Make the real state Icon a child of the independent button layer. A UI
    // child always renders after its parent's Image, avoiding donor sibling
    // ordering differences that previously hid the icon behind the button.
    if(g_p95ColourLayerTr&&g_p95IconTr&&M_Transform_SetParent){void* parent=M_Transform_get_parent?invoke(M_Transform_get_parent,g_p95IconTr,nullptr):nullptr;if(parent!=g_p95ColourLayerTr){bool keepWorld=false;void* pa[2]={g_p95ColourLayerTr,&keepWorld};invoke(M_Transform_SetParent,g_p95IconTr,pa);}}
    if(g_p95ColourLayerTr&&M_Transform_SetAsLastSibling)invoke(M_Transform_SetAsLastSibling,g_p95ColourLayerTr,nullptr);
    if(g_p95IconTr&&M_Transform_SetAsLastSibling)invoke(M_Transform_SetAsLastSibling,g_p95IconTr,nullptr);
    if(g_p95IconImage&&g_p95IconTr){void* s[1]={g_p95Sprites[g_p91Profile]};invoke(M_UIImage_set_sprite,g_p95IconImage,s);invoke(M_UIImage_set_overrideSprite,g_p95IconImage,s);F2 centre{0.5f,0.5f},zero{0,0},size{78,78};set_vec2_prop(g_p95IconTr,M_RectTransform_set_anchorMin,centre);set_vec2_prop(g_p95IconTr,M_RectTransform_set_anchorMax,centre);set_vec2_prop(g_p95IconTr,M_RectTransform_set_pivot,centre);set_vec2_prop(g_p95IconTr,M_RectTransform_set_anchoredPosition,zero);set_vec2_prop(g_p95IconTr,M_RectTransform_set_sizeDelta,size);bool preserve=true;void* pa[1]={&preserve};invoke(M_UIImage_set_preserveAspect,g_p95IconImage,pa);if(M_Graphic_set_color){F4 white{1,1,1,1};void* c[1]={&white};invoke(M_Graphic_set_color,g_p95IconImage,c);}p95_update_tooltip();if(!g_p95IconApplied){log_raw("DAY & NIGHT CYCLE: base-game button layer active; donor Panel hidden and state Icon parented above it.\r\n");g_p95IconApplied=true;}}
}
static bool p93_is_under_calendar(void* tr){for(int depth=0;tr&&depth<10;++depth){void* go=M_Component_get_gameObject?invoke(M_Component_get_gameObject,tr,nullptr):nullptr;char n[160];object_name(go,n,sizeof(n));if(contains_i(n,"CalendarUI"))return true;tr=M_Transform_get_parent?invoke(M_Transform_get_parent,tr,nullptr):nullptr;}return false;}
static bool dncFindCalendarUiDonors(bool requireDateLabel){
    if(g_p93TimelineHostGO&&g_p93SpeedDonorGO&&(!requireDateLabel||g_p93DateLabelDonorGO))return true;
    if(!C_GameObject||!M_Resources_FindAll)return false;const Il2CppType* gt=il2cpp_class_get_type(C_GameObject);Il2CppObject* gto=gt?il2cpp_type_get_object(gt):nullptr;if(!gto)return false;void* a[1]={gto};Il2CppArray* all=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!all)return false;
    for(uptr i=0;i<all->max_length;++i){void* go=all->vector[i];if(!go||!M_GameObject_get_activeInHierarchy||!boxed_bool(invoke(M_GameObject_get_activeInHierarchy,go,nullptr),false))continue;char name[180];object_name(go,name,sizeof(name));bool host=streq(name,"containerTimelineButton"),speed=streq(name,"buttonSpeedUp"),date=streq(name,"DateLabel");if(!host&&!speed&&!date)continue;void* tr=M_GameObject_get_transform?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!p93_is_under_calendar(tr))continue;if(host&&!g_p93TimelineHostGO)g_p93TimelineHostGO=go;else if(speed&&!g_p93SpeedDonorGO)g_p93SpeedDonorGO=go;else if(date&&!g_p93DateLabelDonorGO)g_p93DateLabelDonorGO=go;if(g_p93TimelineHostGO&&g_p93SpeedDonorGO&&(!requireDateLabel||g_p93DateLabelDonorGO))return true;}
    return false;
}
static void p93_refresh_control_state();
static void p102_refresh_page_text();
static bool dncReadScaledGameTime(double& scaledTime){
    scaledTime=0;void* state=p50_level_state_instance();void* level=state?p50_level_from_state(state):nullptr;if(!level)return false;
    if(level!=g_dncGameClockLevel){g_dncGameClockLevel=level;g_dncGameTime=nullptr;g_dncGetGameTime=nullptr;void* manager=field_object(level,"<LevelAnalyticsManager>k__BackingField");if(!manager){const MethodInfo* getter=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)level),"get_LevelAnalyticsManager");if(getter)manager=invoke(getter,level,nullptr);}if(manager)g_dncGameTime=field_object(manager,"_gameTime");if(g_dncGameTime)g_dncGetGameTime=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)g_dncGameTime),"get_time");}
    if(!g_dncGameTime)return false;scaledTime=g_dncGetGameTime?boxed_double(invoke(g_dncGetGameTime,g_dncGameTime,nullptr),-1.0):boxed_double(field_object(g_dncGameTime,"_time"),-1.0);return scaledTime>=0;
}
static bool dncConvertHDRSunToUnityLight(F4 hdrSun,F4& lightColor,float& lightIntensity){
    if(!g_p91Config||!g_dncGetSunUnityLightColor||!g_dncGetSunUnityLightIntensity)return false;
    // The game exposes the exact HDR conversion only through LightingConfig's
    // getters. Swap the value for the duration of these two synchronous calls
    // and restore it before returning; no render push or frame update occurs
    // while the temporary value is present.
    F4 previous=field_f4(g_p91Config,"SunColor");set_field_f4(g_p91Config,"SunColor",hdrSun);
    Il2CppObject* colorBox=invoke(g_dncGetSunUnityLightColor,g_p91Config,nullptr);Il2CppObject* intensityBox=invoke(g_dncGetSunUnityLightIntensity,g_p91Config,nullptr);
    set_field_f4(g_p91Config,"SunColor",previous);
    void* colorData=colorBox?il2cpp_object_unbox(colorBox):nullptr;void* intensityData=intensityBox?il2cpp_object_unbox(intensityBox):nullptr;if(!colorData||!intensityData)return false;
    lightColor=*(F4*)colorData;lightIntensity=*(float*)intensityData;return true;
}
struct DNCProfileEndpoint {F4 color,sky,sun;float intensity,shadowStrength;};
static DNCProfileEndpoint g_dncProfileEndpoints[4]{};
static u32 g_dncProfileEndpointGeneration=0xffffffffu;
static void dncComputeProfileTarget(int profile,F4& color,F4& sky,F4& sun,float& intensity,float& shadowStrength){
    color=g_p91DayColor;sky=g_p91DaySkyColor;sun=g_p91DaySunColor;intensity=g_p91DayIntensity;shadowStrength=g_p91DayShadowStrength;
    if(g_p91IsLodge){
        if(profile==0){color=F4{g_p91DayColor.x*2.00f,g_p91DayColor.y*2.00f,g_p91DayColor.z*2.00f,1};sun=F4{g_p91DaySunColor.x*2.00f,g_p91DaySunColor.y*2.00f,g_p91DaySunColor.z*2.00f,1};sky=F4{0.75f,0.85f,1.00f,1};intensity*=1.25f;}
        if(profile==1){color=F4{g_p91DayColor.x*1.45f,g_p91DayColor.y*1.45f,g_p91DayColor.z*1.45f,1};sun=F4{g_p91DaySunColor.x*1.45f,g_p91DaySunColor.y*1.45f,g_p91DaySunColor.z*1.45f,1};sky=F4{0.60f,0.70f,0.85f,1};intensity*=1.10f;shadowStrength*=0.50f;}
    }else{
        if(profile==3){color=F4{g_p91DayColor.x*0.82f,g_p91DayColor.y*0.70f,g_p91DayColor.z*0.62f,1};sun=F4{g_p91DaySunColor.x*0.62f,g_p91DaySunColor.y*0.48f,g_p91DaySunColor.z*0.42f,1};sky=F4{1.05f,0.95f,0.90f,1};intensity*=0.72f;shadowStrength*=0.55f;}
        if(profile==1){color=F4{g_p91DayColor.x*0.68f,g_p91DayColor.y*0.50f,g_p91DayColor.z*0.38f,1};sun=F4{g_p91DaySunColor.x*0.30f,g_p91DaySunColor.y*0.24f,g_p91DaySunColor.z*0.28f,1};sky=F4{0.90f,1.00f,1.30f,1};intensity*=0.55f;shadowStrength*=0.35f;}
        if(profile==2){color=F4{g_p91DayColor.x*0.42f,g_p91DayColor.y*0.27f,g_p91DayColor.z*0.18f,1};sun=F4{g_p91DaySunColor.x*0.035f,g_p91DaySunColor.y*0.030f,g_p91DaySunColor.z*0.060f,1};sky=F4{0.55f,0.60f,0.90f,1};intensity*=0.34f;shadowStrength=0.0f;}
    }
    // Use the game's nonlinear HDR conversion so automatic hold endpoints
    // match their manual equivalents.
    dncConvertHDRSunToUnityLight(sun,color,intensity);
}
static void dncProfileTarget(int profile,F4& color,F4& sky,F4& sun,float& intensity,float& shadowStrength){
    if(g_dncProfileEndpointGeneration!=g_dncLightingEnvironmentGeneration){for(int i=0;i<4;++i){DNCProfileEndpoint& endpoint=g_dncProfileEndpoints[i];dncComputeProfileTarget(i,endpoint.color,endpoint.sky,endpoint.sun,endpoint.intensity,endpoint.shadowStrength);}g_dncProfileEndpointGeneration=g_dncLightingEnvironmentGeneration;}
    if(profile<0||profile>3)profile=0;const DNCProfileEndpoint& endpoint=g_dncProfileEndpoints[profile];color=endpoint.color;sky=endpoint.sky;sun=endpoint.sun;intensity=endpoint.intensity;shadowStrength=endpoint.shadowStrength;
}
struct DNCCyclePhase{int fromProfile,toProfile,displayProfile;float blend;double hour;};
static DNCCyclePhase dncCyclePhaseAt(double hour){
    const double dawnHold=(double)g_dncPhaseHoldUnits[0]/100.0;
    const double dayHold=(double)g_dncPhaseHoldUnits[1]/100.0;
    const double duskHold=(double)g_dncPhaseHoldUnits[2]/100.0;
    const double nightHold=(double)g_dncPhaseHoldUnits[3]/100.0;
    const double nightLead=nightHold*0.5;
    double cursor=nightLead;
    DNCCyclePhase phase{2,2,2,0,hour};
    if(hour<cursor){/* leading half of the Night hold */}
    else if(hour<(cursor+=3.0)){phase.fromProfile=2;phase.toProfile=3;phase.blend=dncSmoothStep01((float)((hour-(cursor-3.0))/3.0));}
    else if(hour<(cursor+=dawnHold)){phase.fromProfile=phase.toProfile=3;}
    else if(hour<(cursor+=2.0)){phase.fromProfile=3;phase.toProfile=0;phase.blend=dncSmoothStep01((float)((hour-(cursor-2.0))/2.0));}
    else if(hour<(cursor+=dayHold)){phase.fromProfile=phase.toProfile=0;}
    else if(hour<(cursor+=1.0)){phase.fromProfile=0;phase.toProfile=1;phase.blend=dncSmoothStep01((float)(hour-(cursor-1.0)));}
    else if(hour<(cursor+=duskHold)){phase.fromProfile=phase.toProfile=1;}
    else if(hour<(cursor+=2.0)){phase.fromProfile=1;phase.toProfile=2;phase.blend=dncSmoothStep01((float)((hour-(cursor-2.0))/2.0));}
    // The remaining tail is the second half of the Night hold. Since the four
    // saved holds total 16h and transitions total 8h, cursor always lands at
    // 24h minus half the Night hold.
    phase.displayProfile=phase.blend>=0.5f?phase.toProfile:phase.fromProfile;return phase;
}
static void p96_apply_auto_cycle(){
    if(!g_p96AutoMode||!g_dncPreferencesReady||!g_p91Captured)return;
    if(g_dncAutoCycleCalculatedEpoch==p78_epoch&&g_dncAutoCycleCalculatedEnvironment==g_dncLightingEnvironmentGeneration&&g_dncAutoCycleCalculatedPreference==g_dncPreferenceScopeGeneration&&g_dncAutoCycleCalculatedMode==g_p96AutoMode)return;
    double gameTime=0;if(!dncReadScaledGameTime(gameTime))return;
    if(!g_dncAutoExteriorActive||g_dncAutoExteriorGeneration!=g_dncLightingEnvironmentGeneration){set_field_f4(g_p91Config,"SunColor",g_p91DaySunColor);set_field_f4(g_p91Config,"SkyColor",g_p91DaySkyColor);if(g_p91Environment&&g_dncPushLightingSettings)invoke(g_dncPushLightingSettings,g_p91Environment,nullptr);g_dncAutoExteriorActive=true;g_dncAutoExteriorGeneration=g_dncLightingEnvironmentGeneration;log_raw("DAY & NIGHT CYCLE: automatic mode restored the authored baseline once; continuous testing now isolates Octalux sky colour plus its keyword refresh.\r\n");}
    double spanDays=g_p96AutoMode==1?DNC_DAYS_PER_WEEK:DNC_DAYS_PER_MONTH;double cycles=(gameTime/DNC_GAME_SECONDS_PER_DAY)/spanDays;i64 whole=(i64)cycles;double fraction=cycles-(double)whole;if(fraction<0)fraction+=1.0;double hour=fraction*24.0;DNCCyclePhase phase=dncCyclePhaseAt(hour);
    F4 fromColor{},fromSky{},fromSun{},toColor{},toSky{},toSun{};float fromIntensity=0,fromShadow=1,toIntensity=0,toShadow=1;dncProfileTarget(phase.fromProfile,fromColor,fromSky,fromSun,fromIntensity,fromShadow);dncProfileTarget(phase.toProfile,toColor,toSky,toSun,toIntensity,toShadow);
    g_dncDesiredLightColor=dncLerpF4(fromColor,toColor,phase.blend);g_dncDesiredSkyColor=dncLerpF4(fromSky,toSky,phase.blend);g_dncDesiredSunColor=dncLerpF4(fromSun,toSun,phase.blend);g_dncDesiredIntensity=dncLerpFloat(fromIntensity,toIntensity,phase.blend);g_dncDesiredShadowStrength=dncLerpFloat(fromShadow,toShadow,phase.blend);g_dncVirtualHour=hour;g_p91Profile=phase.displayProfile;g_dncLightingHoldReady=true;
    // Convert after interpolating HDR, rather than interpolating the already
    // converted endpoint intensities. This preserves the game's tone response
    // throughout the transition and exactly matches Manual at every hold.
    dncConvertHDRSunToUnityLight(g_dncDesiredSunColor,g_dncDesiredLightColor,g_dncDesiredIntensity);
    g_dncAutoCycleCalculatedEpoch=p78_epoch;g_dncAutoCycleCalculatedEnvironment=g_dncLightingEnvironmentGeneration;g_dncAutoCycleCalculatedPreference=g_dncPreferenceScopeGeneration;g_dncAutoCycleCalculatedMode=g_p96AutoMode;
    if(phase.displayProfile!=g_p96LastProfile){const char* labels[4]={"Day","Dusk","Night","Dawn"};const char* cadence=g_p96AutoMode==1?"weekly":"monthly";char line[420];psprintf(line,"DAY & NIGHT CYCLE: automatic %s virtual time %02d:%02d now %s; converted exterior=(%.4f,%.4f,%.4f) x %.4f.\r\n",cadence,(int)hour,(int)((hour-(int)hour)*60.0),labels[phase.displayProfile],(double)g_dncDesiredLightColor.x,(double)g_dncDesiredLightColor.y,(double)g_dncDesiredLightColor.z,(double)g_dncDesiredIntensity);log_raw(line);g_p96LastProfile=phase.displayProfile;p93_refresh_control_state();p95_update_tooltip();p102_refresh_page_text();}
}
static int dncShaderPropertyId(const char* property){if(!property||!M_Shader_PropertyToID)return 0;Il2CppString* name=il2cpp_string_new(property);void* a[1]={name};return boxed_i32(invoke(M_Shader_PropertyToID,nullptr,a),0);}
static void dncSetGlobalColor(int propertyId,F4& color){if(!propertyId||!M_Shader_SetGlobalColorInt)return;void* args[2]={&propertyId,&color};invoke(M_Shader_SetGlobalColorInt,nullptr,args);}
static void dncApplyLightTarget(void* light){if(!light)return;void* colorArgs[]={&g_dncDesiredLightColor};invoke(M_Light_set_color,light,colorArgs);void* intensityArgs[]={&g_dncDesiredIntensity};invoke(M_Light_set_intensity,light,intensityArgs);if(M_Light_set_shadowStrength){void* shadowArgs[]={&g_dncDesiredShadowStrength};invoke(M_Light_set_shadowStrength,light,shadowArgs);}}
static void dncApplyDirectLightingTargets(){
    static bool propertyResolved=false,routeLogged=false;static int mainLightColorId=0;
    if(!propertyResolved){propertyResolved=true;mainLightColorId=dncShaderPropertyId("_TwoPointMainLightColor");}
    dncSetGlobalColor(mainLightColorId,g_dncDesiredSunColor);
    dncApplyLightTarget(g_p91ExteriorLight);
    if(!routeLogged){routeLogged=true;log_raw("DAY & NIGHT CYCLE: stable automatic route applies game-converted HDR colour/intensity to the exterior Light only.\r\n");}
}
static void dncRestoreOctaluxGradualQuality(){
    if(!g_dncOctaluxGradualQualityTuned||!g_dncOctaluxQualitySettings)return;
    set_field_float(g_dncOctaluxQualitySettings,"GradualBlendValue",g_dncOctaluxOriginalGradualBlend);
    set_field_i32(g_dncOctaluxQualitySettings,"GradualRayCount",g_dncOctaluxOriginalGradualRayCount);
    g_dncOctaluxGradualQualityTuned=false;
    log_raw("DAY & NIGHT CYCLE: restored the game's original Octalux Gradual quality values.\r\n");
}
static void dncApplyAcceleratedOctaluxGradualQuality(){
    if(!g_dncOctaluxFeature||!g_dncOctaluxGetCurrentQualitySettings)return;
    void* settings=invoke(g_dncOctaluxGetCurrentQualitySettings,g_dncOctaluxFeature,nullptr);if(!settings)return;
    if(settings!=g_dncOctaluxQualitySettings){dncRestoreOctaluxGradualQuality();g_dncOctaluxQualitySettings=settings;g_dncOctaluxOriginalGradualBlend=field_float(settings,"GradualBlendValue",0.01f);g_dncOctaluxOriginalGradualRayCount=field_i32(settings,"GradualRayCount",32);}
    if(g_dncOctaluxGradualQualityTuned)return;
    float tunedBlend=g_dncOctaluxOriginalGradualBlend*(float)(DNC_DAYS_PER_MONTH/DNC_DAYS_PER_WEEK);if(tunedBlend>1.0f)tunedBlend=1.0f;
    int tunedRays=(int)((double)g_dncOctaluxOriginalGradualRayCount*(DNC_DAYS_PER_MONTH/DNC_DAYS_PER_WEEK)+0.999);if(tunedRays<g_dncOctaluxOriginalGradualRayCount)tunedRays=g_dncOctaluxOriginalGradualRayCount;if(tunedRays>512)tunedRays=512;
    set_field_float(settings,"GradualBlendValue",tunedBlend);set_field_i32(settings,"GradualRayCount",tunedRays);g_dncOctaluxGradualQualityTuned=true;
    char line[380];psprintf(line,"DAY & NIGHT CYCLE: accelerated Octalux Gradual quality scaled 30/7: blend %.5f -> %.5f, rays %d -> %d.\r\n",(double)g_dncOctaluxOriginalGradualBlend,(double)tunedBlend,g_dncOctaluxOriginalGradualRayCount,tunedRays);log_raw(line);
}
static void dncApplyOctaluxSky(){
    // Fast mode restored Weekly strength but glittered even when requested only
    // at phase boundaries. Stay on the stable Gradual solver and scale both its
    // blend and sample count by the exact 30/7 Monthly-to-Weekly speed ratio.
    // Monthly retains the original values and behaviour confirmed in testing.
    dncApplyDirectLightingTargets();if(!dncResolveOctaluxSkyRoute())return;
    void* args[1]={&g_dncDesiredSkyColor};invoke(g_dncOctaluxSetSkyColor,g_dncOctaluxFeature,args);invoke(g_dncOctaluxUpdateKeywords,g_dncOctaluxFeature,nullptr);
    u64 now=p78_clock?p78_clock():0;bool manualNeedsAcceleration=!g_p96AutoMode&&(g_dncTransitionActive||(g_dncManualGradualSettleUntil&&now&&now<g_dncManualGradualSettleUntil));
    if(g_p96AutoMode==1||manualNeedsAcceleration)dncApplyAcceleratedOctaluxGradualQuality();else dncRestoreOctaluxGradualQuality();
    static bool routeLogged=false;if(!routeLogged){routeLogged=true;log_raw("DAY & NIGHT CYCLE: stable Octalux Gradual updates use 30/7 blend/ray scaling for Weekly and active Manual transitions; Monthly and settled Manual retain original quality values. Fast/Rapid/global/interior routes are not used.\r\n");}
}
static void dncApplyPreferencesForCurrentMuseum(){
    static u32 appliedLightingGeneration=0;
    if(g_dncPreferencesReady&&appliedLightingGeneration==g_dncLightingEnvironmentGeneration)return;
    // The lighting environment can appear before LevelState and CalendarUI
    // finish replacing the outgoing museum. Wait for the playable HUD and a
    // short stable interval before touching either identity graph.
    if(!g_p93ControlGO)return;
    u64 now=p78_clock?p78_clock():0;if(g_dncLightingEnvironmentReadyAt&&now<g_dncLightingEnvironmentReadyAt)return;
    // An unseen museum starts from the mod's own Manual/Day default. Never
    // seed it from the previous museum's live profile.
    if(!dncPreferenceOpenScope(0))return;
    int mode=0,manualProfile=g_p91Profile;bool wasStored=false;if(!dncPreferenceGet(mode,manualProfile,g_dncPhaseHoldUnits,wasStored))return;
    if(g_dncAppliedPreferenceGeneration==g_dncPreferenceScopeGeneration&&appliedLightingGeneration==g_dncLightingEnvironmentGeneration){g_dncPreferencesReady=true;return;}
    g_dncAppliedPreferenceGeneration=g_dncPreferenceScopeGeneration;appliedLightingGeneration=g_dncLightingEnvironmentGeneration;g_dncManualProfile=manualProfile;g_p96AutoMode=mode;g_dncPreferencesReady=true;
    g_p96LastProfile=-1;
    if(g_p96AutoMode)p96_apply_auto_cycle();else g_p91Profile=g_dncManualProfile;
    g_p97WeekVisual=-1;g_p97MonthVisual=-1;g_p99ManualVisual=-1;g_p99CalendarVisual=-1;for(int i=0;i<4;++i)g_p101ManualVisual[i]=-1;
    p93_refresh_control_state();p95_update_tooltip();p102_refresh_page_text();
    const char* modeName=g_p96AutoMode==1?"Weekly":g_p96AutoMode==2?"Monthly":"Manual";const char* profileName=g_dncManualProfile==3?"Dawn":g_dncManualProfile==0?"Day":g_dncManualProfile==1?"Dusk":"Night";char line[360];psprintf(line,"DAY & NIGHT CYCLE PREFERENCES: %s mode applied for this museum after %llu ms lighting startup settle; remembered Manual choice=%s; source=%s.\r\n",modeName,(unsigned long long)DNC_LIGHTING_STARTUP_SETTLE_MS,profileName,wasStored?"saved preference":"museum default");log_raw(line);
}
static void dncSaveCurrentPreference(const char* reason){
    if(dncPreferenceSave(g_p96AutoMode,g_dncManualProfile,g_dncPhaseHoldUnits)){char line[260];psprintf(line,"DAY & NIGHT CYCLE PREFERENCES: saved %s.\r\n",reason?reason:"updated selection");log_raw(line);}
    else log_raw("DAY & NIGHT CYCLE PREFERENCES: selection is active, but its save could not be completed.\r\n");
}

static Il2CppClass* g_p97TMPTextClass=nullptr;static const MethodInfo* g_p97TMPSetText=nullptr;static const MethodInfo* g_p98TMPGetFontSize=nullptr;static const MethodInfo* g_p98TMPSetFontSize=nullptr;
static void p93_disable_button_component(void* go,const char* ns,const char* cn);
static void p97_set_active(void* go,bool active){if(!go||!M_GameObject_SetActive)return;void* a[1]={&active};invoke(M_GameObject_SetActive,go,a);}
static void* p97_first_component(void* go,Il2CppClass* c){if(!go||!c||!M_GameObject_GetComponents)return nullptr;const Il2CppType* t=il2cpp_class_get_type(c);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return nullptr;void* a[1]={to};Il2CppArray* parts=(Il2CppArray*)invoke(M_GameObject_GetComponents,go,a);return parts&&parts->max_length?parts->vector[0]:nullptr;}
static void p97_disable_behaviour(void* go,const char* ns,const char* name){Il2CppClass* c=find_class(ns,name);if(!go||!c||!M_GameObject_GetComponents||!M_Behaviour_set_enabled)return;const Il2CppType* t=il2cpp_class_get_type(c);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return;void* a[1]={to};Il2CppArray* parts=(Il2CppArray*)invoke(M_GameObject_GetComponents,go,a);if(!parts)return;for(uptr i=0;i<parts->max_length;++i)if(parts->vector[i]){bool off=false;void* x[1]={&off};invoke(M_Behaviour_set_enabled,parts->vector[i],x);}}
static bool p97_hit(void* tr,const F3& mouse){Rect4 r{};F3 local{};return tr&&get_rect4(tr,r)&&screen_to_local(tr,mouse,local)&&local.x>=r.x&&local.x<=r.x+r.width&&local.y>=r.y&&local.y<=r.y+r.height;}
static void p97_set_label(void* label,const char* text,bool selected){
    if(!label)return;if(!g_p97TMPTextClass)g_p97TMPTextClass=find_class("TMPro","TMP_Text");if(g_p97TMPTextClass&&!g_p97TMPSetText)g_p97TMPSetText=find_method1_hierarchy(g_p97TMPTextClass,"set_text","System.String");if(text&&g_p97TMPSetText){Il2CppString* s=il2cpp_string_new(text);void* a[1]={s};invoke(g_p97TMPSetText,label,a);}const MethodInfo* setAlignment=il2cpp_class_get_method_from_name(il2cpp_object_get_class((Il2CppObject*)label),"set_alignment",1);if(setAlignment){int centre=514;void* a[1]={&centre};invoke(setAlignment,label,a);}if(M_Graphic_set_color){F4 color=selected?F4{1,1,1,1}:F4{0.08f,0.18f,0.32f,1};void* c[1]={&color};invoke(M_Graphic_set_color,label,c);}if(M_Graphic_set_raycastTarget){bool off=false;void* a[1]={&off};invoke(M_Graphic_set_raycastTarget,label,a);}
}
static void p98_scale_font(void* label,float scale){if(!label||scale<=0)return;Il2CppClass* c=il2cpp_object_get_class((Il2CppObject*)label);if(!g_p98TMPGetFontSize)g_p98TMPGetFontSize=find_method0_hierarchy(c,"get_fontSize");if(!g_p98TMPSetFontSize)g_p98TMPSetFontSize=find_method1_hierarchy(c,"set_fontSize","System.Single");if(!g_p98TMPGetFontSize||!g_p98TMPSetFontSize)return;float size=boxed_float(invoke(g_p98TMPGetFontSize,label,nullptr),0);if(size<=0)return;size*=scale;void* a[1]={&size};invoke(g_p98TMPSetFontSize,label,a);}
static bool p98_clone_surface(void* source,void* parent,const F2& anchor,const F2& pivot,const F2& pos,const F2& size,const F4& color,void*& go,void*& tr){
    if(!source||!parent||!M_Object_Instantiate||!M_Transform_SetParent)return false;go=invoke(M_Object_Instantiate,nullptr,&source);tr=go?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!go||!tr)return false;bool keep=false;void* pa[2]={parent,&keep};invoke(M_Transform_SetParent,tr,pa);set_vec2_prop(tr,M_RectTransform_set_anchorMin,anchor);set_vec2_prop(tr,M_RectTransform_set_anchorMax,anchor);set_vec2_prop(tr,M_RectTransform_set_pivot,pivot);set_vec2_prop(tr,M_RectTransform_set_anchoredPosition,pos);set_vec2_prop(tr,M_RectTransform_set_sizeDelta,size);void* image=p97_first_component(go,C_UIImage);if(image){void* none=nullptr;void* s[1]={none};invoke(M_UIImage_set_sprite,image,s);invoke(M_UIImage_set_overrideSprite,image,s);if(M_Graphic_set_color){F4 c=color;void* a[1]={&c};invoke(M_Graphic_set_color,image,a);}if(M_Graphic_set_raycastTarget){bool off=false;void* a[1]={&off};invoke(M_Graphic_set_raycastTarget,image,a);}}p97_disable_behaviour(go,"TPS.Game.UI","TooltipSpawner");p97_disable_behaviour(go,"UnityEngine","Animator");return true;
}
static bool p105_enable_surface_raycast(void* go){
    void* image=p97_first_component(go,C_UIImage);if(!image||!M_Graphic_set_raycastTarget)return false;
    if(M_Behaviour_set_enabled){bool on=true;void* enabled[1]={&on};invoke(M_Behaviour_set_enabled,image,enabled);}
    bool on=true;void* raycast[1]={&on};invoke(M_Graphic_set_raycastTarget,image,raycast);return true;
}
static void p98_find_native_theme(void*& paper,void*& header,void*& closeBg,void*& closeIcon){
    paper=nullptr;header=nullptr;closeBg=nullptr;closeIcon=nullptr;if(!C_UIImage||!M_Resources_FindAll||!M_Component_get_transform)return;const Il2CppType* t=il2cpp_class_get_type(C_UIImage);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return;void* a[1]={to};Il2CppArray* images=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!images)return;
    for(uptr i=0;i<images->max_length;++i){void* image=images->vector[i];void* tr=image?invoke(M_Component_get_transform,image,nullptr):nullptr;Rect4 r{};if(!tr||!get_rect4(tr,r))continue;void* go=M_Component_get_gameObject?invoke(M_Component_get_gameObject,image,nullptr):nullptr;char name[96];object_name(go,name,sizeof(name));if(streq(name,"Background")&&r.width>600&&r.width<640&&r.height>1000)paper=image;else if(streq(name,"Background")&&r.width>580&&r.height>90&&r.height<130)header=image;else if(streq(name,"Background")&&r.width>90&&r.width<100&&r.height>75&&r.height<90)closeBg=image;else if(streq(name,"Icon")&&r.width>38&&r.width<45&&r.height>38&&r.height<46)closeIcon=image;}
    char line[180];psprintf(line,"DAY & NIGHT CYCLE: native popover theme paper=%d header=%d close=%d icon=%d.\r\n",paper!=nullptr,header!=nullptr,closeBg!=nullptr,closeIcon!=nullptr);log_raw(line);
}
static void* p98_find_named_sprite(const char* wanted){
    if(!wanted||!C_Sprite||!M_Resources_FindAll)return nullptr;const Il2CppType* t=il2cpp_class_get_type(C_Sprite);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return nullptr;void* a[1]={to};Il2CppArray* sprites=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!sprites)return nullptr;void* partial=nullptr;for(uptr i=0;i<sprites->max_length;++i){void* sprite=sprites->vector[i];char name[180];object_name(sprite,name,sizeof(name));if(streq(name,wanted))return sprite;if(!partial&&contains_i(name,wanted))partial=sprite;}return partial;
}
static void* p106_find_native_refresh_sprite(){
    if(!C_UIImage||!M_Resources_FindAll||!M_Component_get_transform||!M_Component_get_gameObject)return nullptr;const MethodInfo* getSprite=find_method0_hierarchy(C_UIImage,"get_sprite");if(!getSprite)return nullptr;const Il2CppType* type=il2cpp_class_get_type(C_UIImage);Il2CppObject* typeObject=type?il2cpp_type_get_object(type):nullptr;if(!typeObject)return nullptr;void* args[1]={typeObject};Il2CppArray* images=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,args);if(!images)return nullptr;
    for(uptr i=0;i<images->max_length;++i){void* image=images->vector[i];void* tr=image?invoke(M_Component_get_transform,image,nullptr):nullptr;void* go=image?invoke(M_Component_get_gameObject,image,nullptr):nullptr;if(!tr||!go)continue;char imageName[100];object_name(go,imageName,sizeof(imageName));if(!contains_i(imageName,"icon"))continue;bool resetAncestor=false;void* parent=tr;for(int depth=0;parent&&depth<7;++depth){void* parentGO=invoke(M_Component_get_gameObject,parent,nullptr);char parentName[120];object_name(parentGO,parentName,sizeof(parentName));if(contains_i(parentName,"reset")||contains_i(parentName,"refresh")){resetAncestor=true;break;}parent=M_Transform_get_parent?invoke(M_Transform_get_parent,parent,nullptr):nullptr;}if(!resetAncestor)continue;void* sprite=invoke(getSprite,image,nullptr);if(!sprite)continue;char spriteName[160];object_name(sprite,spriteName,sizeof(spriteName));char line[260];psprintf(line,"DAY & NIGHT CYCLE: native refresh icon sprite '%s' reused.\r\n",spriteName);log_raw(line);return sprite;}
    log_raw("DAY & NIGHT CYCLE: native refresh icon unavailable; themed circular-arrow glyph used.\r\n");return nullptr;
}
static void p98_apply_native_theme(void* target,void* donor){
    if(!target||!donor)return;const MethodInfo* getSprite=find_method0_hierarchy(C_UIImage,"get_sprite");const MethodInfo* getColor=find_method0_hierarchy(C_Graphic,"get_color");const MethodInfo* getMaterial=find_method0_hierarchy(C_Graphic,"get_material");const MethodInfo* setMaterial=il2cpp_class_get_method_from_name(C_Graphic,"set_material",1);const MethodInfo* getType=find_method0_hierarchy(C_UIImage,"get_type");const MethodInfo* setType=il2cpp_class_get_method_from_name(C_UIImage,"set_type",1);const MethodInfo* getPpu=find_method0_hierarchy(C_UIImage,"get_pixelsPerUnitMultiplier");const MethodInfo* setPpu=il2cpp_class_get_method_from_name(C_UIImage,"set_pixelsPerUnitMultiplier",1);
    void* sprite=getSprite?invoke(getSprite,donor,nullptr):nullptr;if(sprite){void* a[1]={sprite};invoke(M_UIImage_set_sprite,target,a);invoke(M_UIImage_set_overrideSprite,target,a);}if(getColor&&M_Graphic_set_color){Il2CppObject* boxed=invoke(getColor,donor,nullptr);void* data=boxed?il2cpp_object_unbox(boxed):nullptr;if(data){F4 color=*static_cast<F4*>(data);void* a[1]={&color};invoke(M_Graphic_set_color,target,a);}}if(getMaterial&&setMaterial){void* material=invoke(getMaterial,donor,nullptr);void* a[1]={material};invoke(setMaterial,target,a);}if(getType&&setType){int type=boxed_i32(invoke(getType,donor,nullptr),0);void* a[1]={&type};invoke(setType,target,a);}if(getPpu&&setPpu){float ppu=boxed_float(invoke(getPpu,donor,nullptr),1);void* a[1]={&ppu};invoke(setPpu,target,a);}
}
static bool p98_clone_label(void* donor,void* parent,const F2& anchor,const F2& pivot,const F2& pos,const F2& size,const char* text,const F4& color,float fontScale,void*& go,void*& tr){
    if(!donor||!parent||!M_Object_Instantiate||!M_Transform_SetParent)return false;go=invoke(M_Object_Instantiate,nullptr,&donor);tr=go?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!go||!tr)return false;bool keep=false;void* pa[2]={parent,&keep};invoke(M_Transform_SetParent,tr,pa);set_vec2_prop(tr,M_RectTransform_set_anchorMin,anchor);set_vec2_prop(tr,M_RectTransform_set_anchorMax,anchor);set_vec2_prop(tr,M_RectTransform_set_pivot,pivot);set_vec2_prop(tr,M_RectTransform_set_anchoredPosition,pos);set_vec2_prop(tr,M_RectTransform_set_sizeDelta,size);p97_disable_behaviour(go,"I2.Loc","Localize");p97_disable_behaviour(go,"UnityEngine","Animator");if(!g_p97TMPTextClass)g_p97TMPTextClass=find_class("TMPro","TMP_Text");void* label=p97_first_component(go,g_p97TMPTextClass);p97_set_label(label,text,true);p98_scale_font(label,fontScale);if(label&&M_Graphic_set_color){F4 c=color;void* a[1]={&c};invoke(M_Graphic_set_color,label,a);}return label!=nullptr;
}
static void* p98_find_text_donor(const char* wanted){
    if(!wanted||!M_Resources_FindAll)return nullptr;if(!g_p97TMPTextClass)g_p97TMPTextClass=find_class("TMPro","TMP_Text");if(!g_p97TMPTextClass)return nullptr;
    const Il2CppType* t=il2cpp_class_get_type(g_p97TMPTextClass);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return nullptr;void* a[1]={to};Il2CppArray* labels=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!labels)return nullptr;
    const MethodInfo* getText=find_method0_hierarchy(g_p97TMPTextClass,"get_text");if(!getText)return nullptr;
    for(uptr i=0;i<labels->max_length;++i){void* label=labels->vector[i];if(!label)continue;char value[120];string_ascii((Il2CppString*)invoke(getText,label,nullptr),value,sizeof(value));if(!streq(value,wanted))continue;void* go=M_Component_get_gameObject?invoke(M_Component_get_gameObject,label,nullptr):nullptr;void* tr=go&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!go||!tr)continue;if(g_p98PanelTr&&M_Transform_IsChildOf){void* own[1]={g_p98PanelTr};if(boxed_bool(invoke(M_Transform_IsChildOf,tr,own),false))continue;}char line[180];char name[100];object_name(go,name,sizeof(name));psprintf(line,"DAY & NIGHT CYCLE: native text donor '%s' found on '%s'.\r\n",wanted,name);log_raw(line);return go;}
    return nullptr;
}
static void p97_find_selector_images(void* root,void*& panel,void*& icon){
    panel=nullptr;icon=nullptr;if(!root||!C_UIImage||!M_Component_get_transform||!M_Transform_IsChildOf)return;void* rootTr=invoke(M_GameObject_get_transform,root,nullptr);Il2CppArray* images=p103_find_all_components(C_UIImage);if(!rootTr||!images)return;
    for(uptr i=0;i<images->max_length&&(!panel||!icon);++i){void* image=images->vector[i];void* tr=image?invoke(M_Component_get_transform,image,nullptr):nullptr;if(!tr)continue;void* owns[1]={rootTr};if(!boxed_bool(invoke(M_Transform_IsChildOf,tr,owns),false))continue;void* go=invoke(M_Component_get_gameObject,image,nullptr);char name[80];object_name(go,name,sizeof(name));if(streq(name,"Panel"))panel=image;else if(streq(name,"Icon"))icon=image;}
}
static void p97_set_selector_art(void* panel,void* icon,bool selected,int variant){
    if(panel){int base=selected?10:4;void* sprite=g_p95Sprites[base+variant];if(sprite){void* s[1]={sprite};invoke(M_UIImage_set_sprite,panel,s);invoke(M_UIImage_set_overrideSprite,panel,s);}if(M_Graphic_set_color){F4 white{1,1,1,1};void* c[1]={&white};invoke(M_Graphic_set_color,panel,c);}}
    if(icon&&M_Graphic_set_color){F4 hidden{1,1,1,0};void* c[1]={&hidden};invoke(M_Graphic_set_color,icon,c);}
}
static bool p97_create_selector(void* donor,void* labelDonor,void* parent,const F2& pos,const char* text,void*& go,void*& tr,void*& labelGO,void*& labelTr,void*& label){
    if(!donor||!labelDonor||!parent||!M_Object_Instantiate||!M_Transform_SetParent)return false;go=invoke(M_Object_Instantiate,nullptr,&donor);tr=go?invoke(M_GameObject_get_transform,go,nullptr):nullptr;labelGO=invoke(M_Object_Instantiate,nullptr,&labelDonor);labelTr=labelGO?invoke(M_GameObject_get_transform,labelGO,nullptr):nullptr;if(!go||!tr||!labelGO||!labelTr)return false;bool keep=false;void* pa[2]={parent,&keep};invoke(M_Transform_SetParent,tr,pa);invoke(M_Transform_SetParent,labelTr,pa);const MethodInfo* setScale=find_method1_hierarchy(il2cpp_object_get_class((Il2CppObject*)tr),"set_localScale","UnityEngine.Vector3");if(setScale){F3 one{1,1,1};void* sa[1]={&one};invoke(setScale,tr,sa);}F2 anchor{1,0},pivot{1,0},size{330,100};set_vec2_prop(tr,M_RectTransform_set_anchorMin,anchor);set_vec2_prop(tr,M_RectTransform_set_anchorMax,anchor);set_vec2_prop(tr,M_RectTransform_set_pivot,pivot);set_vec2_prop(tr,M_RectTransform_set_sizeDelta,size);set_vec2_prop(tr,M_RectTransform_set_anchoredPosition,pos);set_vec2_prop(labelTr,M_RectTransform_set_anchorMin,anchor);set_vec2_prop(labelTr,M_RectTransform_set_anchorMax,anchor);set_vec2_prop(labelTr,M_RectTransform_set_pivot,pivot);set_vec2_prop(labelTr,M_RectTransform_set_sizeDelta,size);set_vec2_prop(labelTr,M_RectTransform_set_anchoredPosition,pos);p93_disable_button_component(go,"UnityEngine.UI","Button");p93_disable_button_component(go,"TPS.Core.UI","UIButton");p97_disable_behaviour(go,"TPS.Game.UI","TooltipSpawner");p97_disable_behaviour(go,"UnityEngine","Animator");p97_disable_behaviour(labelGO,"I2.Loc","Localize");p97_disable_behaviour(labelGO,"UnityEngine","Animator");if(!g_p97TMPTextClass)g_p97TMPTextClass=find_class("TMPro","TMP_Text");label=p97_first_component(labelGO,g_p97TMPTextClass);p97_set_label(label,text,false);if(M_Transform_SetAsLastSibling){invoke(M_Transform_SetAsLastSibling,tr,nullptr);invoke(M_Transform_SetAsLastSibling,labelTr,nullptr);}return true;
}
static bool p97_install_wide_selector_art(void* parent,const F2& pos,void* oldPanel,void* oldIcon,void*& layerGO,void*& result){
    if(M_Graphic_set_color){F4 hidden{1,1,1,0};void* a[1]={&hidden};if(oldPanel)invoke(M_Graphic_set_color,oldPanel,a);if(oldIcon)invoke(M_Graphic_set_color,oldIcon,a);}void* layerTr=nullptr;F2 rb{1,0};if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,pos,F2{330,100},F4{1,1,1,1},layerGO,layerTr))return false;result=p97_first_component(layerGO,C_UIImage);if(result&&M_UIImage_set_preserveAspect){bool stretch=false;void* a[1]={&stretch};invoke(M_UIImage_set_preserveAspect,result,a);}return result!=nullptr;
}
static void p101_set_manual_art(int profile,int variant){
    if(profile<0||profile>3)return;bool selected=g_p96AutoMode==0&&g_p91Profile==profile;int state=variant==1?1:(variant==2?2:0);void* image=g_p101ManualPanelImage[profile];void* sprite=g_p95Sprites[(selected?10:4)+state];if(image&&sprite){void* a[1]={sprite};invoke(M_UIImage_set_sprite,image,a);invoke(M_UIImage_set_overrideSprite,image,a);if(M_Graphic_set_color){F4 white{1,1,1,1};void* c[1]={&white};invoke(M_Graphic_set_color,image,c);}}if(g_p101ManualIconImage[profile]&&M_Graphic_set_color){F4 iconColor=selected||variant==2?F4{1,1,1,1}:F4{.08f,.18f,.32f,1};void* c[1]={&iconColor};invoke(M_Graphic_set_color,g_p101ManualIconImage[profile],c);}p97_set_label(g_p101ManualLabel[profile],nullptr,selected||variant==2);
}
static bool p101_create_manual_selector(void* donor,void* labelDonor,void* parent,int profile,const F2& pos,const char* text){
    if(profile<0||profile>3)return false;if(!p97_create_selector(donor,labelDonor,parent,pos,text,g_p101ManualGO[profile],g_p101ManualTr[profile],g_p101ManualLabelGO[profile],g_p101ManualLabelTr[profile],g_p101ManualLabel[profile]))return false;void* oldPanel=nullptr;void* oldIcon=nullptr;p97_find_selector_images(g_p101ManualGO[profile],oldPanel,oldIcon);if(!p97_install_wide_selector_art(parent,pos,oldPanel,oldIcon,g_p101ManualArtGO[profile],g_p101ManualPanelImage[profile]))return false;F2 size{300,92};set_vec2_prop(g_p101ManualTr[profile],M_RectTransform_set_sizeDelta,size);set_vec2_prop(g_p101ManualLabelTr[profile],M_RectTransform_set_sizeDelta,size);F2 labelPos{pos.x+16,pos.y+4};set_vec2_prop(g_p101ManualLabelTr[profile],M_RectTransform_set_anchoredPosition,labelPos);void* artTr=g_p101ManualArtGO[profile]?invoke(M_Component_get_transform,g_p101ManualPanelImage[profile],nullptr):nullptr;if(artTr)set_vec2_prop(artTr,M_RectTransform_set_sizeDelta,size);void* iconTr=nullptr;F2 rb{1,0};float opticalRaise=(profile==1||profile==3)?8.0f:0.0f;F2 iconPos{pos.x-206,pos.y+21+opticalRaise};if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,iconPos,F2{50,50},F4{1,1,1,1},g_p101ManualIconGO[profile],iconTr))return false;g_p101ManualIconImage[profile]=p97_first_component(g_p101ManualIconGO[profile],C_UIImage);if(g_p101ManualIconImage[profile]&&g_p95Sprites[profile]){void* s[1]={g_p95Sprites[profile]};invoke(M_UIImage_set_sprite,g_p101ManualIconImage[profile],s);invoke(M_UIImage_set_overrideSprite,g_p101ManualIconImage[profile],s);bool preserve=true;void* p[1]={&preserve};invoke(M_UIImage_set_preserveAspect,g_p101ManualIconImage[profile],p);}if(M_Transform_SetAsLastSibling){invoke(M_Transform_SetAsLastSibling,iconTr,nullptr);invoke(M_Transform_SetAsLastSibling,g_p101ManualLabelTr[profile],nullptr);}p101_set_manual_art(profile,0);return true;
}
static void p106_refresh_timeline_geometry(){
    if(!g_p106TimelineTr)return;const float width=620.0f,height=30.0f;u32 cumulative=0;
    for(int i=0;i<4;++i){u32 visibleUnits=g_dncPhaseHoldUnits[i]+DNC_PHASE_TRANSITION_UNITS[i];float x=width*(float)cumulative/(float)DNC_PHASE_VISIBLE_TOTAL;float w=width*(float)visibleUnits/(float)DNC_PHASE_VISIBLE_TOTAL;if(g_p106TimelineSegmentTr[i]){set_vec2_prop(g_p106TimelineSegmentTr[i],M_RectTransform_set_anchoredPosition,F2{x,0});set_vec2_prop(g_p106TimelineSegmentTr[i],M_RectTransform_set_sizeDelta,F2{w,height});}cumulative+=visibleUnits;if(i<3&&g_p106TimelineHandleTr[i]){float handleX=width*(float)cumulative/(float)DNC_PHASE_VISIBLE_TOTAL;set_vec2_prop(g_p106TimelineHandleTr[i],M_RectTransform_set_anchoredPosition,F2{handleX,height*.5f});}}
}
static void dncFormatPhaseHours(char* output,usize capacity,u32 units){
    if(!output||!capacity)return;u32 whole=units/100,remainder=units%100;
    if(remainder==0)psprintf(output,"%u",whole);
    else if(remainder==25)psprintf(output,"%u.25",whole);
    else if(remainder==50)psprintf(output,"%u.5",whole);
    else if(remainder==75)psprintf(output,"%u.75",whole);
    else psprintf(output,"%u.%02u",whole,remainder);
}
static int p106_nearest_boundary(const F3& mouse){
    Rect4 rect{};F3 local{};if(!g_p106TimelineTr||!get_rect4(g_p106TimelineTr,rect)||!screen_to_local(g_p106TimelineTr,mouse,local)||rect.width<=0)return -1;float x=(local.x-rect.x)/rect.width;if(x<0)x=0;if(x>1)x=1;u32 wanted=(u32)(x*(float)DNC_PHASE_VISIBLE_TOTAL+.5f);u32 cumulative=0;int nearest=0;u32 best=0xffffffffu;for(int i=0;i<3;++i){cumulative+=g_dncPhaseHoldUnits[i]+DNC_PHASE_TRANSITION_UNITS[i];u32 distance=wanted>cumulative?wanted-cumulative:cumulative-wanted;if(distance<best){best=distance;nearest=i;}}return nearest;
}
static bool p106_drag_boundary(int boundary,const F3& mouse){
    if(boundary<0||boundary>2||!g_p106TimelineTr)return false;Rect4 rect{};F3 local{};if(!get_rect4(g_p106TimelineTr,rect)||!screen_to_local(g_p106TimelineTr,mouse,local)||rect.width<=0)return false;float x=(local.x-rect.x)/rect.width;if(x<0)x=0;if(x>1)x=1;u32 wanted=(u32)(x*(float)DNC_PHASE_VISIBLE_TOTAL+.5f);const u32 step=25;wanted=((wanted+step/2)/step)*step;u32 phaseTotals[4]{};for(int i=0;i<4;++i)phaseTotals[i]=g_dncPhaseHoldUnits[i]+DNC_PHASE_TRANSITION_UNITS[i];u32 boundaries[3]={phaseTotals[0],phaseTotals[0]+phaseTotals[1],phaseTotals[0]+phaseTotals[1]+phaseTotals[2]};u32 low=(boundary==0?0:boundaries[boundary-1])+DNC_PHASE_TRANSITION_UNITS[boundary]+DNC_PHASE_HOLD_MIN;u32 high=(boundary==2?DNC_PHASE_VISIBLE_TOTAL:boundaries[boundary+1])-(DNC_PHASE_TRANSITION_UNITS[boundary+1]+DNC_PHASE_HOLD_MIN);if(wanted<low)wanted=low;if(wanted>high)wanted=high;if(wanted==boundaries[boundary])return false;boundaries[boundary]=wanted;phaseTotals[0]=boundaries[0];phaseTotals[1]=boundaries[1]-boundaries[0];phaseTotals[2]=boundaries[2]-boundaries[1];phaseTotals[3]=DNC_PHASE_VISIBLE_TOTAL-boundaries[2];for(int i=0;i<4;++i)g_dncPhaseHoldUnits[i]=phaseTotals[i]-DNC_PHASE_TRANSITION_UNITS[i];p106_refresh_timeline_geometry();p102_refresh_page_text();return true;
}
static void p102_refresh_page_text(){
    const char* profiles[4]={"DAY","DUSK","NIGHT","DAWN"};int profile=g_p91Profile>=0&&g_p91Profile<4?g_p91Profile:0;char manual[120];psprintf(manual,"CURRENT LIGHTING: %s",profiles[profile]);p97_set_label(g_p102ManualStatusLabel,manual,false);char schedule[140];if(g_p96AutoMode)psprintf(schedule,"%s CYCLE  -  CURRENT: %s",g_p96AutoMode==1?"WEEKLY":"MONTHLY",profiles[profile]);else psprintf(schedule,"SELECT WEEK OR MONTH");p97_set_label(g_p102CalendarStatusLabel,schedule,false);
    char dawn[16],day[16],dusk[16],night[16],sequence[180];dncFormatPhaseHours(dawn,sizeof(dawn),g_dncPhaseHoldUnits[0]+DNC_PHASE_TRANSITION_UNITS[0]);dncFormatPhaseHours(day,sizeof(day),g_dncPhaseHoldUnits[1]+DNC_PHASE_TRANSITION_UNITS[1]);dncFormatPhaseHours(dusk,sizeof(dusk),g_dncPhaseHoldUnits[2]+DNC_PHASE_TRANSITION_UNITS[2]);dncFormatPhaseHours(night,sizeof(night),g_dncPhaseHoldUnits[3]+DNC_PHASE_TRANSITION_UNITS[3]);psprintf(sequence,"DAWN %sH  >  DAY %sH  >  DUSK %sH  >  NIGHT %sH",dawn,day,dusk,night);p97_set_label(g_p102CalendarSequenceLabel,sequence,false);p106_refresh_timeline_geometry();
}
static bool p99_add_glyph_rect(void* parent,const F2& pos,const F2& size,const F4& color){void* go=nullptr;void* tr=nullptr;F2 centre{.5f,.5f};return p98_clone_surface(g_p95IconGO,parent,centre,centre,pos,size,color,go,tr);}
static bool p99_create_calendar_glyph(void* parent){
    F4 orange{.84f,.24f,.02f,1};
    return p99_add_glyph_rect(parent,F2{0,16},F2{50,6},orange)&&p99_add_glyph_rect(parent,F2{0,-17},F2{50,6},orange)&&p99_add_glyph_rect(parent,F2{-22,0},F2{6,37},orange)&&p99_add_glyph_rect(parent,F2{22,0},F2{6,37},orange)&&p99_add_glyph_rect(parent,F2{-13,23},F2{6,11},orange)&&p99_add_glyph_rect(parent,F2{13,23},F2{6,11},orange)&&p99_add_glyph_rect(parent,F2{-11,3},F2{7,7},orange)&&p99_add_glyph_rect(parent,F2{9,3},F2{7,7},orange)&&p99_add_glyph_rect(parent,F2{-11,-9},F2{7,7},orange)&&p99_add_glyph_rect(parent,F2{9,-9},F2{7,7},orange);
}
static bool p99_has_named_ancestor(void* tr,const char* wanted){
    for(int depth=0;tr&&depth<14;++depth){void* go=M_Component_get_gameObject?invoke(M_Component_get_gameObject,tr,nullptr):nullptr;char name[160];object_name(go,name,sizeof(name));if(streq(name,wanted))return true;tr=M_Transform_get_parent?invoke(M_Transform_get_parent,tr,nullptr):nullptr;}return false;
}
static void* p99_find_native_tab_donor(){
    if(g_p99NativeTabDonorGO)return g_p99NativeTabDonorGO;
    if(!C_GameObject||!M_Resources_FindAll)return nullptr;const Il2CppType* gt=il2cpp_class_get_type(C_GameObject);Il2CppObject* gto=gt?il2cpp_type_get_object(gt):nullptr;if(!gto)return nullptr;void* a[1]={gto};Il2CppArray* all=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!all)return nullptr;void* inspectorFallback=nullptr;
    for(uptr i=0;i<all->max_length;++i){void* go=all->vector[i];if(!go)continue;char name[160];object_name(go,name,sizeof(name));if(!streq(name,"TabButtonUtilities")&&!streq(name,"tabOverview"))continue;void* tr=M_GameObject_get_transform?invoke(M_GameObject_get_transform,go,nullptr):nullptr;if(!tr)continue;if(g_p98PanelTr&&M_Transform_IsChildOf){void* own[1]={g_p98PanelTr};if(boxed_bool(invoke(M_Transform_IsChildOf,tr,own),false))continue;}if(streq(name,"TabButtonUtilities")&&p99_has_named_ancestor(tr,"UI_General_InGame_P_Building_ItemsList")){g_p99NativeTabDonorGO=go;return go;}if(streq(name,"tabOverview")&&p99_has_named_ancestor(tr,"UI_InGame_Inspector_P_Item(Clone)"))inspectorFallback=go;
    }
    g_p99NativeTabDonorGO=inspectorFallback;return inspectorFallback;
}
static bool p99_collect_native_tab_parts(void* root,P99NativeTabVisual& out){
    // Keep native tab state discovery on the proven whole-scene Image route.
    // Unity does not reliably return every inactive Idle/Hover child from this
    // donor through GetComponentsInChildren, even when includeInactive is true.
    // This runs only while constructing the two tabs and still filters every
    // result to the selected donor subtree below.
    out=P99NativeTabVisual{};if(!root||!C_UIImage||!M_Resources_FindAll||!M_Component_get_transform||!M_Transform_IsChildOf)return false;void* rootTr=M_GameObject_get_transform?invoke(M_GameObject_get_transform,root,nullptr):nullptr;const Il2CppType* it=il2cpp_class_get_type(C_UIImage);Il2CppObject* ito=it?il2cpp_type_get_object(it):nullptr;if(!rootTr||!ito)return false;void* a[1]={ito};Il2CppArray* images=(Il2CppArray*)invoke(M_Resources_FindAll,nullptr,a);if(!images)return false;
    for(uptr i=0;i<images->max_length;++i){void* image=images->vector[i];void* tr=image?invoke(M_Component_get_transform,image,nullptr):nullptr;if(!tr)continue;void* owns[1]={rootTr};if(!boxed_bool(invoke(M_Transform_IsChildOf,tr,owns),false))continue;void* go=M_Component_get_gameObject?invoke(M_Component_get_gameObject,image,nullptr):nullptr;char name[80];object_name(go,name,sizeof(name));bool stateImage=streq(name,"Idle")||streq(name,"Hover")||streq(name,"Selected")||streq(name,"Locked");if(stateImage&&M_Behaviour_set_enabled){bool on=true;void* e[1]={&on};invoke(M_Behaviour_set_enabled,image,e);}if(streq(name,"Idle"))out.idleGO=go;else if(streq(name,"Hover"))out.hoverGO=go;else if(streq(name,"Selected"))out.selectedGO=go;else if(streq(name,"Locked"))out.lockedGO=go;else if(streq(name,"Icon"))out.iconImage=image;
    }
    out.ready=out.idleGO&&out.hoverGO&&out.selectedGO&&out.iconImage;return out.ready;
}
static void p99_set_native_tab_state(P99NativeTabVisual& visual,bool selected,int variant){
    // The selected artwork is designed to merge into the native tabbed-panel
    // backing. Without that backing it appeared transparent, which made the
    // opaque orange Idle state look like the highlight. Keep the controller's
    // real semantic mapping now that the required backing is present.
    if(!visual.ready)return;p97_set_active(visual.idleGO,!selected&&variant!=1);p97_set_active(visual.hoverGO,!selected&&variant==1);p97_set_active(visual.selectedGO,selected);p97_set_active(visual.lockedGO,false);
}
static bool p99_clone_native_visual_part(void* sourceGO,void* parent,void*& cloneGO){
    cloneGO=nullptr;if(!sourceGO||!parent||!M_Object_Instantiate||!M_Transform_SetParent)return false;
    void* sourceTr=M_GameObject_get_transform?invoke(M_GameObject_get_transform,sourceGO,nullptr):nullptr;RectState geometry{};if(!capture_rect_state(sourceTr,geometry))return false;
    cloneGO=invoke(M_Object_Instantiate,nullptr,&sourceGO);void* cloneTr=cloneGO&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,cloneGO,nullptr):nullptr;if(!cloneGO||!cloneTr)return false;
    bool keep=false;void* pa[2]={parent,&keep};invoke(M_Transform_SetParent,cloneTr,pa);restore_rect_state(cloneTr,geometry);
    const MethodInfo* setScale=find_method1_hierarchy(il2cpp_object_get_class((Il2CppObject*)cloneTr),"set_localScale","UnityEngine.Vector3");if(setScale){F3 one{1,1,1};void* sa[1]={&one};invoke(setScale,cloneTr,sa);}
    p97_disable_behaviour(cloneGO,"UnityEngine","Animator");void* clonedImage=p97_first_component(cloneGO,C_UIImage);if(clonedImage){if(M_Behaviour_set_enabled){bool on=true;void* ea[1]={&on};invoke(M_Behaviour_set_enabled,clonedImage,ea);}if(M_Graphic_set_raycastTarget){bool off=false;void* ra[1]={&off};invoke(M_Graphic_set_raycastTarget,clonedImage,ra);}}
    return true;
}
static void p99_log_native_part_geometry(const char* tab,const char* state,void* go){
    void* stateTr=go&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,go,nullptr):nullptr;F2 pos{},size{};get_vec2_prop(stateTr,M_RectTransform_get_anchoredPosition,pos);get_vec2_prop(stateTr,M_RectTransform_get_sizeDelta,size);char line[240];psprintf(line,"DAY & NIGHT CYCLE: %s native %s visual pos=(%.1f,%.1f) size=(%.1f,%.1f).\r\n",tab,state,(double)pos.x,(double)pos.y,(double)size.x,(double)size.y);log_raw(line);
}
static bool p99_create_tab(void* parent,void* labelDonor,const F2& pos,const char* text,void*& go,void*& tr,void*& image,void*& labelGO,void*& label){
    (void)labelDonor;const bool calendar=contains_i(text,"CALENDAR");labelGO=nullptr;label=nullptr;image=nullptr;
    void* donor=p99_find_native_tab_donor();P99NativeTabVisual donorVisual{};if(!donor||!p99_collect_native_tab_parts(donor,donorVisual)){log_raw("DAY & NIGHT CYCLE: native tab visual donor was unavailable.\r\n");return false;}
    const F2 tabSize{(float)P99_LOCKED_TAB_WIDTH,(float)P99_LOCKED_TAB_HEIGHT};
    F2 rb{1,0};if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,pos,tabSize,F4{1,1,1,0},go,tr))return false;
    // The selected outline expects a pale panel beneath it. Supply only that
    // bounded interior dependency; do not clone the native TabButtons backing,
    // layout group, controller, animator or input components.
    void* fillGO=nullptr;void* fillTr=nullptr;F2 centre{.5f,.5f};F2 fillSize{tabSize.x*.72f,tabSize.y*.68f};if(!p98_clone_surface(g_p95IconGO,tr,centre,centre,F2{0,-2},fillSize,F4{1.0f,.96f,.78f,1},fillGO,fillTr))return false;p97_set_active(fillGO,false);if(calendar)g_p99CalendarFillGO=fillGO;else g_p99ManualFillGO=fillGO;
    P99NativeTabVisual& visual=calendar?g_p99CalendarNative:g_p99ManualNative;visual=P99NativeTabVisual{};
    if(!p99_clone_native_visual_part(donorVisual.idleGO,tr,visual.idleGO)||!p99_clone_native_visual_part(donorVisual.hoverGO,tr,visual.hoverGO)||!p99_clone_native_visual_part(donorVisual.selectedGO,tr,visual.selectedGO))return false;
    void* sourceIconGO=donorVisual.iconImage&&M_Component_get_gameObject?invoke(M_Component_get_gameObject,donorVisual.iconImage,nullptr):nullptr;void* iconGO=nullptr;if(!p99_clone_native_visual_part(sourceIconGO,tr,iconGO))return false;void* iconTr=iconGO&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,iconGO,nullptr):nullptr;visual.iconImage=p97_first_component(iconGO,C_UIImage);visual.ready=visual.idleGO&&visual.hoverGO&&visual.selectedGO&&visual.iconImage;image=p97_first_component(visual.idleGO,C_UIImage);
    const char* requestedIcon=calendar?P99_LOCKED_AUTOMATED_ICON:P99_LOCKED_MANUAL_ICON;void* iconSprite=p98_find_named_sprite(requestedIcon);
    if(calendar)g_p99CalendarIconTr=iconTr;else g_p99ManualIconTr=iconTr;
    if(iconSprite){void* s[1]={iconSprite};invoke(M_UIImage_set_sprite,visual.iconImage,s);invoke(M_UIImage_set_overrideSprite,visual.iconImage,s);bool preserve=true;void* pa[1]={&preserve};invoke(M_UIImage_set_preserveAspect,visual.iconImage,pa);char resolved[180];object_name(iconSprite,resolved,sizeof(resolved));char line[300];psprintf(line,"DAY & NIGHT CYCLE: %s tab icon resolved '%s'.\r\n",text,resolved);log_raw(line);}
    else if(calendar){void* none=nullptr;void* s[1]={none};invoke(M_UIImage_set_sprite,visual.iconImage,s);invoke(M_UIImage_set_overrideSprite,visual.iconImage,s);if(!p99_create_calendar_glyph(iconTr))return false;log_raw("DAY & NIGHT CYCLE: requested Automated timetable icon unavailable; procedural Calendar glyph retained.\r\n");}
    else {void* fallback=g_p95Sprites[0]?g_p95Sprites[0]:p98_find_named_sprite("UI_Atlas_Icons_Inspectors_TabMuseumUtilities_Idle");if(fallback){void* s[1]={fallback};invoke(M_UIImage_set_sprite,visual.iconImage,s);invoke(M_UIImage_set_overrideSprite,visual.iconImage,s);bool preserve=true;void* pa[1]={&preserve};invoke(M_UIImage_set_preserveAspect,visual.iconImage,pa);}log_raw("DAY & NIGHT CYCLE: requested Manual job-assignment icon unavailable; established Manual fallback retained.\r\n");}
    if(visual.iconImage&&M_Graphic_set_color){F4 orange=P99_LOCKED_ICON_TINT;void* ca[1]={&orange};invoke(M_Graphic_set_color,visual.iconImage,ca);}if(M_Transform_SetAsLastSibling&&iconTr)invoke(M_Transform_SetAsLastSibling,iconTr,nullptr);
    p99_set_native_tab_state(visual,false,0);p99_log_native_part_geometry(text,"Idle",visual.idleGO);p99_log_native_part_geometry(text,"Hover",visual.hoverGO);p99_log_native_part_geometry(text,"Selected",visual.selectedGO);log_raw(calendar?"DAY & NIGHT CYCLE: visual-only native Calendar tab created.\r\n":"DAY & NIGHT CYCLE: visual-only native Manual tab created.\r\n");return visual.ready&&image;
}
static void p99_set_tab_art(void* image,bool selected,int variant){
    // Locked baseline: state changes may only toggle prebuilt visual layers.
    // No RectTransform, sprite, icon or tint mutation belongs in this function.
    void* fill=image==g_p99ManualImage?g_p99ManualFillGO:g_p99CalendarFillGO;p97_set_active(fill,false);
    P99NativeTabVisual& native=image==g_p99ManualImage?g_p99ManualNative:g_p99CalendarNative;if(native.ready){p97_set_active(fill,selected);p99_set_native_tab_state(native,selected,variant);return;}
}
static void p99_show_active_page(){
    // Page selection must never affect tab hierarchy or geometry.  The two
    // fixed hit targets stay in place; this function owns content visibility.
    bool calendar=g_p99ActiveTab==1;
    p97_set_active(g_p97WeekGO,calendar);p97_set_active(g_p97WeekLabelGO,calendar);p97_set_active(g_p97WeekArtGO,calendar);
    p97_set_active(g_p97MonthGO,calendar);p97_set_active(g_p97MonthLabelGO,calendar);p97_set_active(g_p97MonthArtGO,calendar);
    p97_set_active(g_p102CalendarHeadingGO,calendar);p97_set_active(g_p102CalendarSequenceGO,calendar);p97_set_active(g_p106TimelineGO,calendar);
    p97_set_active(g_p106TimingSummaryGO,calendar);p97_set_active(g_p106ResetGO,calendar);p97_set_active(g_p106ResetLabelGO,calendar);
    p97_set_active(g_p102CalendarStatusGO,calendar);p97_set_active(g_p102CalendarStatusLabelGO,calendar);
    p97_set_active(g_p102ManualHeadingGO,!calendar);p97_set_active(g_p102ManualStatusGO,!calendar);p97_set_active(g_p102ManualStatusLabelGO,!calendar);
    for(int i=0;i<4;++i){p97_set_active(g_p101ManualGO[i],!calendar);p97_set_active(g_p101ManualLabelGO[i],!calendar);p97_set_active(g_p101ManualArtGO[i],!calendar);p97_set_active(g_p101ManualIconGO[i],!calendar);g_p101ManualVisual[i]=-1;}
    p102_refresh_page_text();g_p97WeekVisual=-1;g_p97MonthVisual=-1;g_p99ManualVisual=-1;g_p99CalendarVisual=-1;g_p106ResetVisual=-1;
}
static void p97_set_popover_active(bool active){
    g_p97PopoverOpen=active;if(!active){g_p106DragBoundary=-1;g_p106DragDirty=false;}if(active){g_p99ActiveTab=g_p96AutoMode?1:0;p99_show_active_page();}
    p97_set_active(g_p105InputShieldGO,active);p97_set_active(g_p98ShadowGO,active);p97_set_active(g_p98PanelGO,active);
    if(active&&M_Transform_SetAsLastSibling){if(g_p105InputShieldTr)invoke(M_Transform_SetAsLastSibling,g_p105InputShieldTr,nullptr);void* shadowTr=g_p98ShadowGO&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,g_p98ShadowGO,nullptr):nullptr;if(shadowTr)invoke(M_Transform_SetAsLastSibling,shadowTr,nullptr);if(g_p98PanelTr)invoke(M_Transform_SetAsLastSibling,g_p98PanelTr,nullptr);}
}
static void p97_destroy_ui_root(void* go){if(!go)return;p97_set_active(go,false);if(M_Object_Destroy){void* a[1]={go};invoke(M_Object_Destroy,nullptr,a);}}
static bool p97_abort_popover_build(){
    u64 started=g_p97PopoverBuildStartedAt;u64 now=p78_clock?p78_clock():0;void* shield=g_p105InputShieldGO;void* shadow=g_p98ShadowGO;void* panel=g_p98PanelGO;
    p97_destroy_ui_root(shield);p97_destroy_ui_root(shadow);p97_destroy_ui_root(panel);p97_clear_popover_references();
    char line[220];psprintf(line,"DAY & NIGHT CYCLE: incomplete settings pop-out discarded after %llu ms; a later click may retry construction.\r\n",(unsigned long long)(started&&now>=started?now-started:0));log_raw(line);return false;
}
static bool p97_ensure_popover(){
    if(g_p97PopoverBuilt)return true;if(g_p105InputShieldGO||g_p98ShadowGO||g_p98PanelGO)p97_abort_popover_build();if(!dncFindCalendarUiDonors(true))return false;void* host=g_p93TimelineHostGO;void* donor=g_p93SpeedDonorGO;void* labelDonor=g_p93DateLabelDonorGO;void* hostTr=host?invoke(M_GameObject_get_transform,host,nullptr):nullptr;void* parent=hostTr&&M_Transform_get_parent?invoke(M_Transform_get_parent,hostTr,nullptr):nullptr;if(!parent)return false;g_p97PopoverBuildStartedAt=p78_clock?p78_clock():0;
    F2 rb{1,0},rt{1,1},lt{0,1};F4 shadow{0,0,0,.22f},paper{1,.96f,.78f,1},header{.02f,.62f,.82f,1},closeRed{.92f,.12f,.05f,1};
    void* nativePaper=nullptr;void* nativeHeader=nullptr;void* nativeClose=nullptr;void* nativeCloseIcon=nullptr;p98_find_native_theme(nativePaper,nativeHeader,nativeClose,nativeCloseIcon);
    void* discardTr=nullptr;
    // Transparent modal shield: the cloned pop-out uses Win32 polling for its
    // controls, so its decorative Graphics deliberately do not raycast. Keep a
    // single invisible Graphic immediately behind the window to make Unity's
    // EventSystem treat the full visible footprint as UI and prevent clicks
    // from selecting museum objects underneath it. The 768x1004 footprint is
    // the exact union of the accepted body's final lowered position and its
    // raised header row: it reaches left under the timer tile and upward behind
    // the blue banner/close plate without extending beyond the visible window.
    if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,F2{-86,240},F2{768,1000},F4{1,1,1,0},g_p105InputShieldGO,g_p105InputShieldTr))return p97_abort_popover_build();
    void* shieldImage=p97_first_component(g_p105InputShieldGO,C_UIImage);if(!shieldImage)return p97_abort_popover_build();if(M_Behaviour_set_enabled){bool on=true;void* a[1]={&on};invoke(M_Behaviour_set_enabled,shieldImage,a);}if(M_Graphic_set_raycastTarget){bool on=true;void* a[1]={&on};invoke(M_Graphic_set_raycastTarget,shieldImage,a);}p93_disable_button_component(g_p105InputShieldGO,"UnityEngine.UI","Button");p93_disable_button_component(g_p105InputShieldGO,"TPS.Core.UI","UIButton");
    if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,F2{-90,386},F2{758,958},shadow,g_p98ShadowGO,discardTr))return p97_abort_popover_build();
    if(!p98_clone_surface(g_p95IconGO,parent,rb,rb,F2{-86,390},F2{760,900},paper,g_p98PanelGO,g_p98PanelTr))return p97_abort_popover_build();
    p98_apply_native_theme(p97_first_component(g_p98PanelGO,C_UIImage),nativePaper);
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rt,rt,F2{-6,88},F2{112,100},closeRed,g_p98CloseGO,g_p98CloseTr))return p97_abort_popover_build();
    p98_apply_native_theme(p97_first_component(g_p98CloseGO,C_UIImage),nativeClose);
    if(!p105_enable_surface_raycast(g_p98CloseGO))return p97_abort_popover_build();
    // Keep the white close plate behind the blue header, but stop the header
    // before the far-right edge so the plate remains substantially visible.
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,lt,lt,F2{0,90},F2{700,116},header,g_p98HeaderGO,g_p98HeaderTr))return p97_abort_popover_build();
    p98_apply_native_theme(p97_first_component(g_p98HeaderGO,C_UIImage),nativeHeader);
    if(!p105_enable_surface_raycast(g_p98HeaderGO))return p97_abort_popover_build();
    void* tileOuterGO=nullptr;void* tileOuterTr=nullptr;
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,lt,lt,F2{-8,100},F2{120,120},F4{1,1,1,1},tileOuterGO,tileOuterTr))return p97_abort_popover_build();
    g_p98TileOuterTr=tileOuterTr;
    if(!p105_enable_surface_raycast(tileOuterGO))return p97_abort_popover_build();
    void* tileInnerGO=nullptr;void* tileInnerTr=nullptr;F2 centre{0.5f,0.5f};
    if(!p98_clone_surface(g_p95IconGO,tileOuterTr,centre,centre,F2{0,0},F2{108,108},F4{.18f,.20f,.23f,1},tileInnerGO,tileInnerTr))return p97_abort_popover_build();
    void* badgeGO=nullptr;void* badgeTr=nullptr;
    if(!p98_clone_surface(g_p95IconGO,tileInnerTr,centre,centre,F2{0,0},F2{82,82},F4{1,1,1,1},badgeGO,badgeTr))return p97_abort_popover_build();
    void* badgeImage=p97_first_component(badgeGO,C_UIImage);void* timerSprite=p98_find_named_sprite("UI_Generic_T_Spritesheet_Icons_Timer");if(!timerSprite)timerSprite=g_p95Sprites[0];
    if(badgeImage&&timerSprite){void* s[1]={timerSprite};invoke(M_UIImage_set_sprite,badgeImage,s);invoke(M_UIImage_set_overrideSprite,badgeImage,s);bool preserve=true;void* a[1]={&preserve};invoke(M_UIImage_set_preserveAspect,badgeImage,a);}
    void* titleGO=nullptr;void* titleTr=nullptr;
    if(!p98_clone_label(labelDonor,g_p98PanelTr,lt,lt,F2{138,80},F2{470,82},"Cycle Settings",F4{1,1,1,1},1.08f,titleGO,titleTr))return p97_abort_popover_build();
    void* titleLabel=p97_first_component(titleGO,g_p97TMPTextClass);if(titleLabel){const MethodInfo* align=il2cpp_class_get_method_from_name(il2cpp_object_get_class((Il2CppObject*)titleLabel),"set_alignment",1);if(align){int midLeft=513;void* a[1]={&midLeft};invoke(align,titleLabel,a);}}
    void* closeLabelGO=nullptr;void* closeLabelTr=nullptr;if(nativeCloseIcon){if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rt,rt,F2{-32,54},F2{46,46},F4{1,1,1,1},closeLabelGO,closeLabelTr))return p97_abort_popover_build();p98_apply_native_theme(p97_first_component(closeLabelGO,C_UIImage),nativeCloseIcon);}else if(!p98_clone_label(labelDonor,g_p98PanelTr,rt,rt,F2{-14,67},F2{84,78},"X",F4{1,1,1,1},1.0f,closeLabelGO,closeLabelTr))return p97_abort_popover_build();
    // Match the header proportions used by the base game's inspector windows.
    F2 tilePos{-8,94},tileSize{108,108},tileInnerSize{98,98},badgeSize{74,74};set_vec2_prop(tileOuterTr,M_RectTransform_set_anchoredPosition,tilePos);set_vec2_prop(tileOuterTr,M_RectTransform_set_sizeDelta,tileSize);set_vec2_prop(tileInnerTr,M_RectTransform_set_sizeDelta,tileInnerSize);set_vec2_prop(badgeTr,M_RectTransform_set_sizeDelta,badgeSize);F2 closePos{-6,88},closeSize{100,100};set_vec2_prop(g_p98CloseTr,M_RectTransform_set_anchoredPosition,closePos);set_vec2_prop(g_p98CloseTr,M_RectTransform_set_sizeDelta,closeSize);if(closeLabelTr){F2 xPos{-28,68},xSize{46,46};set_vec2_prop(closeLabelTr,M_RectTransform_set_anchoredPosition,xPos);set_vec2_prop(closeLabelTr,M_RectTransform_set_sizeDelta,xSize);}
    // The base inspector has a translucent charcoal outer backing; only its foreground
    // content page uses the parchment/scroll sprite. Remove the extra outer
    // scroll that otherwise rises behind and beyond the blue heading.
    void* outerImage=p97_first_component(g_p98PanelGO,C_UIImage);
    // Render the charcoal backing on its own first-sibling surface so its top
    // and right edges can be corrected without moving the panel's children or
    // changing the panel hit rectangle. It rises 50 units and trims 10 units
    // from the right relative to the original 760x900 outer Image.
    void* backingGO=nullptr;void* backingTr=nullptr;F2 lb{0,0};F4 nativeGrey{.28f,.25f,.28f,.82f};if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,lb,lb,F2{0,0},F2{750,950},nativeGrey,backingGO,backingTr))return p97_abort_popover_build();void* backingImage=p97_first_component(backingGO,C_UIImage);
    // Preserve the native paper Image material/type/PPU after removing its
    // sprite, then apply the accepted charcoal tint and transparency.
    if(backingImage&&outerImage)p98_apply_native_theme(backingImage,outerImage);if(backingImage){void* none=nullptr;void* a[1]={none};invoke(M_UIImage_set_sprite,backingImage,a);invoke(M_UIImage_set_overrideSprite,backingImage,a);if(M_Graphic_set_color){F4 accepted=nativeGrey;void* c[1]={&accepted};invoke(M_Graphic_set_color,backingImage,c);}}
    if(outerImage){void* none=nullptr;void* a[1]={none};invoke(M_UIImage_set_sprite,outerImage,a);invoke(M_UIImage_set_overrideSprite,outerImage,a);if(M_Graphic_set_color){F4 transparent{1,1,1,0};void* c[1]={&transparent};invoke(M_Graphic_set_color,outerImage,c);}}
    if(M_Transform_SetSiblingIndex&&backingTr){int first=0;void* a[1]={&first};invoke(M_Transform_SetSiblingIndex,backingTr,a);}
    // Lower the taller mock-aligned card so its enlarged badge stays on-screen.
    // Keep the soft shadow behind the accepted charcoal backing. Its previous
    // 14-unit left offset exposed a lighter vertical strip beside the window.
    F2 panelPos{-86,240},shadowPos{-86,236};set_vec2_prop(g_p98PanelTr,M_RectTransform_set_anchoredPosition,panelPos);void* loweredShadowTr=g_p98ShadowGO?invoke(M_GameObject_get_transform,g_p98ShadowGO,nullptr):nullptr;if(loweredShadowTr)set_vec2_prop(loweredShadowTr,M_RectTransform_set_anchoredPosition,shadowPos);
    void* contentGO=nullptr;void* contentTr=nullptr;
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rb,rb,F2{-20,20},F2{720,760},paper,contentGO,contentTr))return p97_abort_popover_build();
    p98_apply_native_theme(p97_first_component(contentGO,C_UIImage),nativePaper);
    // Geometry-only tab strip.  The copied Items backing and the later mask both
    // carried visible first-slot rectangles.  Parent the two sprite-only tabs to
    // the parchment itself instead: their lower edge is exactly its upper edge,
    // and fixed positions are independent of visual state.
    // Locked accepted geometry. See the P99_LOCKED_* constants and assertions.
    const F2 manualPos{(float)P99_LOCKED_MANUAL_X,(float)P99_LOCKED_TAB_Y},calendarPos{(float)P99_LOCKED_AUTOMATED_X,(float)P99_LOCKED_TAB_Y};if(!p99_create_tab(contentTr,labelDonor,manualPos,"MANUAL",g_p99ManualGO,g_p99ManualTr,g_p99ManualImage,g_p99ManualLabelGO,g_p99ManualLabel))return p97_abort_popover_build();if(!p99_create_tab(contentTr,labelDonor,calendarPos,"CALENDAR",g_p99CalendarGO,g_p99CalendarTr,g_p99CalendarImage,g_p99CalendarLabelGO,g_p99CalendarLabel))return p97_abort_popover_build();
    if(!p103_create_tooltip_proxy(donor,g_p99ManualTr,"Manual Options",0,false,&g_p103TabTooltipTargets[0]))log_raw("DAY & NIGHT CYCLE: Manual tab tooltip proxy unavailable.\r\n");
    if(!p103_create_tooltip_proxy(donor,g_p99CalendarTr,"Automated Options",1,false,&g_p103TabTooltipTargets[1]))log_raw("DAY & NIGHT CYCLE: Automated tab tooltip proxy unavailable.\r\n");
    if(!p103_create_tooltip_proxy(donor,g_p98CloseTr,"Close Cycle Settings",2,true))log_raw("DAY & NIGHT CYCLE: Close tooltip proxy unavailable.\r\n");
    void* overviewGO=nullptr;void* overviewTr=nullptr;
    void* overviewDonor=p98_find_text_donor("Overview");if(!overviewDonor)overviewDonor=labelDonor;
    if(!p98_clone_label(overviewDonor,contentTr,lt,lt,F2{28,-18},F2{280,60},"Overview",F4{.08f,.24f,.40f,1},1.0f,overviewGO,overviewTr))return p97_abort_popover_build();p97_set_active(overviewGO,false);
    void* overviewLabel=p97_first_component(overviewGO,g_p97TMPTextClass);
    if(overviewLabel){p97_set_label(overviewLabel,"Overview",false);const MethodInfo* align=il2cpp_class_get_method_from_name(il2cpp_object_get_class((Il2CppObject*)overviewLabel),"set_alignment",1);if(align){int midLeft=513;void* a[1]={&midLeft};invoke(align,overviewLabel,a);}}
    void* dividerGO=nullptr;void* dividerTr=nullptr;
    if(!p98_clone_surface(g_p95IconGO,contentTr,lt,lt,F2{20,-78},F2{680,2},F4{.70f,.56f,.30f,.62f},dividerGO,dividerTr))return p97_abort_popover_build();
    // Both pages share this coordinate so switching tabs cannot shift the
    // first row of controls vertically.
    const float pageTopRowY=500.0f;F2 weekPos{-410,pageTopRowY},monthPos{-60,pageTopRowY};
    if(!p97_create_selector(donor,labelDonor,g_p98PanelTr,weekPos,"WEEK",g_p97WeekGO,g_p97WeekTr,g_p97WeekLabelGO,g_p97WeekLabelTr,g_p97WeekLabel))return p97_abort_popover_build();
    if(!p97_create_selector(donor,labelDonor,g_p98PanelTr,monthPos,"MONTH",g_p97MonthGO,g_p97MonthTr,g_p97MonthLabelGO,g_p97MonthLabelTr,g_p97MonthLabel))return p97_abort_popover_build();
    void* oldWeekPanel=nullptr;void* oldWeekIcon=nullptr;void* oldMonthPanel=nullptr;void* oldMonthIcon=nullptr;
    p97_find_selector_images(g_p97WeekGO,oldWeekPanel,oldWeekIcon);p97_find_selector_images(g_p97MonthGO,oldMonthPanel,oldMonthIcon);
    if(!p97_install_wide_selector_art(g_p98PanelTr,weekPos,oldWeekPanel,oldWeekIcon,g_p97WeekArtGO,g_p97WeekPanelImage))return p97_abort_popover_build();
    if(!p97_install_wide_selector_art(g_p98PanelTr,monthPos,oldMonthPanel,oldMonthIcon,g_p97MonthArtGO,g_p97MonthPanelImage))return p97_abort_popover_build();
    F2 selectorSize{300,92};set_vec2_prop(g_p97WeekTr,M_RectTransform_set_sizeDelta,selectorSize);set_vec2_prop(g_p97MonthTr,M_RectTransform_set_sizeDelta,selectorSize);set_vec2_prop(g_p97WeekLabelTr,M_RectTransform_set_sizeDelta,selectorSize);set_vec2_prop(g_p97MonthLabelTr,M_RectTransform_set_sizeDelta,selectorSize);
    void* weekArtTr=g_p97WeekArtGO?invoke(M_GameObject_get_transform,g_p97WeekArtGO,nullptr):nullptr;void* monthArtTr=g_p97MonthArtGO?invoke(M_GameObject_get_transform,g_p97MonthArtGO,nullptr):nullptr;if(weekArtTr)set_vec2_prop(weekArtTr,M_RectTransform_set_sizeDelta,selectorSize);if(monthArtTr)set_vec2_prop(monthArtTr,M_RectTransform_set_sizeDelta,selectorSize);
    g_p97WeekIconImage=nullptr;g_p97MonthIconImage=nullptr;
    if(M_Transform_SetAsLastSibling){invoke(M_Transform_SetAsLastSibling,g_p97WeekLabelTr,nullptr);invoke(M_Transform_SetAsLastSibling,g_p97MonthLabelTr,nullptr);}
    // Manual page: fixed two-by-two profile grid in chronological order.
    if(!p101_create_manual_selector(donor,labelDonor,g_p98PanelTr,3,F2{-410,pageTopRowY},"DAWN"))return p97_abort_popover_build();
    if(!p101_create_manual_selector(donor,labelDonor,g_p98PanelTr,0,F2{-60,pageTopRowY},"DAY"))return p97_abort_popover_build();
    if(!p101_create_manual_selector(donor,labelDonor,g_p98PanelTr,1,F2{-410,380},"DUSK"))return p97_abort_popover_build();
    if(!p101_create_manual_selector(donor,labelDonor,g_p98PanelTr,2,F2{-60,380},"NIGHT"))return p97_abort_popover_build();
    bool tooltipOK=true;tooltipOK=p103_set_native_tooltip(g_p97WeekGO,"Weekly Cycle",3)&&tooltipOK;tooltipOK=p103_set_native_tooltip(g_p97MonthGO,"Monthly Cycle",4)&&tooltipOK;tooltipOK=p103_set_native_tooltip(g_p101ManualGO[3],"Dawn Lighting",5)&&tooltipOK;tooltipOK=p103_set_native_tooltip(g_p101ManualGO[0],"Day Lighting",6)&&tooltipOK;tooltipOK=p103_set_native_tooltip(g_p101ManualGO[1],"Dusk Lighting",7)&&tooltipOK;tooltipOK=p103_set_native_tooltip(g_p101ManualGO[2],"Night Lighting",8)&&tooltipOK;if(!tooltipOK)log_raw("DAY & NIGHT CYCLE: one or more selector tooltip spawners were unavailable.\r\n");
    F4 ink{.08f,.24f,.40f,1};void* layoutTr=nullptr;
    // Page-specific section headings occupy the clear strip immediately below
    // the tabs. Their geometry is independent from the locked tab footprint.
    // Preserve the accepted visual centre (y=732) while giving the native-size
    // font enough vertical room. The previous 64-unit rect clipped the entire
    // line as soon as the donor font was restored to 100% scale.
    if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-60,680},F2{660,104},"Manual Options",ink,1.0f,g_p102ManualHeadingGO,layoutTr))return p97_abort_popover_build();
    void* manualHeading=p97_first_component(g_p102ManualHeadingGO,g_p97TMPTextClass);if(manualHeading){Il2CppClass* headingClass=il2cpp_object_get_class((Il2CppObject*)manualHeading);const MethodInfo* setRich=il2cpp_class_get_method_from_name(headingClass,"set_richText",1);if(setRich){bool on=true;void* a[1]={&on};invoke(setRich,manualHeading,a);}if(g_p97TMPSetText){Il2CppString* s=il2cpp_string_new("<b>Manual Options</b>");void* a[1]={s};invoke(g_p97TMPSetText,manualHeading,a);}const MethodInfo* align=il2cpp_class_get_method_from_name(headingClass,"set_alignment",1);if(align){int midLeft=513;void* a[1]={&midLeft};invoke(align,manualHeading,a);}}
    if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-60,680},F2{660,104},"Automated Options",ink,1.0f,g_p102CalendarHeadingGO,layoutTr))return p97_abort_popover_build();
    void* automatedHeading=p97_first_component(g_p102CalendarHeadingGO,g_p97TMPTextClass);if(automatedHeading){Il2CppClass* headingClass=il2cpp_object_get_class((Il2CppObject*)automatedHeading);const MethodInfo* setRich=il2cpp_class_get_method_from_name(headingClass,"set_richText",1);if(setRich){bool on=true;void* a[1]={&on};invoke(setRich,automatedHeading,a);}if(g_p97TMPSetText){Il2CppString* s=il2cpp_string_new("<b>Automated Options</b>");void* a[1]={s};invoke(g_p97TMPSetText,automatedHeading,a);}const MethodInfo* align=il2cpp_class_get_method_from_name(headingClass,"set_alignment",1);if(align){int midLeft=513;void* a[1]={&midLeft};invoke(align,automatedHeading,a);}}
    if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-70,350},F2{620,62},"DAWN  >  DAY  >  DUSK  >  NIGHT",ink,.66f,g_p102CalendarSequenceGO,layoutTr))return p97_abort_popover_build();g_p102CalendarSequenceLabel=p97_first_component(g_p102CalendarSequenceGO,g_p97TMPTextClass);
    // Three dividers resize the four full-strength holds. The smooth transition
    // budget is intentionally not exposed here so the accepted lighting
    // convergence remains unchanged.
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rb,rb,F2{-70,306},F2{620,30},F4{.20f,.24f,.30f,.42f},g_p106TimelineGO,g_p106TimelineTr))return p97_abort_popover_build();
    const F4 phaseColours[4]={{1.00f,.55f,.20f,.96f},{1.00f,.84f,.08f,.96f},{.62f,.28f,.66f,.96f},{.16f,.32f,.66f,.96f}};F2 timelineLB{0,0};
    for(int i=0;i<4;++i)if(!p98_clone_surface(g_p95IconGO,g_p106TimelineTr,timelineLB,timelineLB,F2{0,0},F2{1,30},phaseColours[i],g_p106TimelineSegmentGO[i],g_p106TimelineSegmentTr[i]))return p97_abort_popover_build();
    F2 handleCentre{.5f,.5f};for(int i=0;i<3;++i)if(!p98_clone_surface(g_p95IconGO,g_p106TimelineTr,timelineLB,handleCentre,F2{0,15},F2{18,48},F4{1,.97f,.80f,1},g_p106TimelineHandleGO[i],g_p106TimelineHandleTr[i]))return p97_abort_popover_build();
    p106_refresh_timeline_geometry();
    void* timingSummaryTr=nullptr;if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-250,258},F2{440,48},"FULL CYCLE: 24 HOURS",ink,.54f,g_p106TimingSummaryGO,timingSummaryTr))return p97_abort_popover_build();
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rb,rb,F2{-70,262},F2{150,42},F4{1.0f,.78f,.08f,.96f},g_p106ResetGO,g_p106ResetTr))return p97_abort_popover_build();g_p106ResetImage=p97_first_component(g_p106ResetGO,C_UIImage);if(!p105_enable_surface_raycast(g_p106ResetGO))return p97_abort_popover_build();
    void* refreshSprite=p106_find_native_refresh_sprite();F2 resetCentre{.5f,.5f};void* resetGlyphTr=nullptr;
    if(refreshSprite){if(!p98_clone_surface(g_p95IconGO,g_p106ResetTr,resetCentre,resetCentre,F2{0,0},F2{34,34},ink,g_p106ResetLabelGO,resetGlyphTr))return p97_abort_popover_build();g_p106ResetGlyphImage=p97_first_component(g_p106ResetLabelGO,C_UIImage);if(g_p106ResetGlyphImage){void* spriteArgs[1]={refreshSprite};invoke(M_UIImage_set_sprite,g_p106ResetGlyphImage,spriteArgs);invoke(M_UIImage_set_overrideSprite,g_p106ResetGlyphImage,spriteArgs);bool preserve=true;void* preserveArgs[1]={&preserve};invoke(M_UIImage_set_preserveAspect,g_p106ResetGlyphImage,preserveArgs);}}
    else {if(!p98_clone_label(overviewDonor,g_p106ResetTr,resetCentre,resetCentre,F2{0,0},F2{70,54},"↻",ink,.86f,g_p106ResetLabelGO,resetGlyphTr))return p97_abort_popover_build();g_p106ResetLabel=p97_first_component(g_p106ResetLabelGO,g_p97TMPTextClass);}
    g_p106ResetVisual=-1;
    if(!p103_set_native_tooltip(g_p106ResetGO,"Restore Standard Timings",9))log_raw("DAY & NIGHT CYCLE: Reset timing tooltip unavailable.\r\n");
    F4 statusFill{1.0f,.88f,.52f,.42f};void* statusTr=nullptr;if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rb,rb,F2{-70,130},F2{620,104},statusFill,g_p102ManualStatusGO,statusTr))return p97_abort_popover_build();
    if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-95,158},F2{570,62},"CURRENT LIGHTING: DAY",ink,.72f,g_p102ManualStatusLabelGO,layoutTr))return p97_abort_popover_build();g_p102ManualStatusLabel=p97_first_component(g_p102ManualStatusLabelGO,g_p97TMPTextClass);
    if(!p98_clone_surface(g_p95IconGO,g_p98PanelTr,rb,rb,F2{-70,130},F2{620,104},statusFill,g_p102CalendarStatusGO,statusTr))return p97_abort_popover_build();
    if(!p98_clone_label(overviewDonor,g_p98PanelTr,rb,rb,F2{-95,158},F2{570,62},"SELECT WEEK OR MONTH",ink,.72f,g_p102CalendarStatusLabelGO,layoutTr))return p97_abort_popover_build();g_p102CalendarStatusLabel=p97_first_component(g_p102CalendarStatusLabelGO,g_p97TMPTextClass);p102_refresh_page_text();
    g_p97PopoverBuilt=true;u64 finished=p78_clock?p78_clock():0;char buildLine[180];psprintf(buildLine,"DAY & NIGHT CYCLE: settings pop-out created in %llu ms using cached native donors.\r\n",(unsigned long long)(g_p97PopoverBuildStartedAt&&finished>=g_p97PopoverBuildStartedAt?finished-g_p97PopoverBuildStartedAt:0));g_p97PopoverBuildStartedAt=0;log_raw(buildLine);return true;
}
static void p93_disable_button_component(void* go,const char* ns,const char* cn){
    Il2CppClass* c=find_class(ns,cn);if(!go||!c||!M_GameObject_GetComponents||!M_Behaviour_set_enabled)return;const Il2CppType* t=il2cpp_class_get_type(c);Il2CppObject* to=t?il2cpp_type_get_object(t):nullptr;if(!to)return;void* a[1]={to};Il2CppArray* parts=(Il2CppArray*)invoke(M_GameObject_GetComponents,go,a);if(!parts)return;for(uptr i=0;i<parts->max_length;++i){void* part=parts->vector[i];if(part){if(M_Selectable_set_targetGraphic){void* target[1]={nullptr};invoke(M_Selectable_set_targetGraphic,part,target);}bool off=false;void* x[1]={&off};invoke(M_Behaviour_set_enabled,part,x);}}
}
static void p93_refresh_control_state(){
    if(!g_p93ControlGO)return;
    // Apply the genuine base-game button variant and matching state icon.
    p95_apply_user_icon();
}
static void p104_cycle_lighting_from_hud(){
    if(g_p97PopoverOpen)p97_set_popover_active(false);
    if(g_p96AutoMode){g_p96AutoMode=0;log_raw("DAY & NIGHT CYCLE: manual lighting mode enabled by right-click.\r\n");}
    // Dawn, Day, Dusk, Night. Right-click advances to the next manual state.
    if(g_p91Profile==3)g_p91Profile=0;else if(g_p91Profile==0)g_p91Profile=1;else if(g_p91Profile==1)g_p91Profile=2;else g_p91Profile=3;g_dncManualProfile=g_p91Profile;dncSaveCurrentPreference("HUD right-click Manual selection");
    p93_refresh_control_state();p95_update_tooltip();
    const char* label=g_p91Profile==3?"Dawn":g_p91Profile==0?"Day":g_p91Profile==1?"Dusk":"Night";char b[176];psprintf(b,"DAY & NIGHT CYCLE: date control right-click selected %s.\r\n",label);log_raw(b);
}
static void p93_calendar_control_tick(){
    // Campaign transitions leave CalendarUI absent for much longer than
    // Sandbox transitions. Never enumerate every loaded GameObject while the
    // application is loading or no playable lighting environment is active.
    if(dncApplicationUnavailableOrBusy()||!g_p91Environment){g_dncLightingHoldReady=false;if(g_p93ControlGO)p93_reset_calendar_control();return;}
    if(!g_p93ControlGO){
        // Resource-wide UI discovery is intentionally infrequent. Running it
        // on the 33 ms message tick can starve Campaign's level loader.
        static u64 nextDiscovery=0;u64 now=p78_clock?p78_clock():0;if(now&&now<nextDiscovery)return;if(now)nextDiscovery=now+1000;
        if(!dncFindCalendarUiDonors(false))return;void* host=g_p93TimelineHostGO;void* donor=g_p93SpeedDonorGO;
        if(!host||!donor||!M_Object_Instantiate||!M_Transform_SetParent)return;
        g_p93ControlGO=invoke(M_Object_Instantiate,nullptr,&donor);g_p93ControlTr=g_p93ControlGO&&M_GameObject_get_transform?invoke(M_GameObject_get_transform,g_p93ControlGO,nullptr):nullptr;void* hostTr=M_GameObject_get_transform?invoke(M_GameObject_get_transform,host,nullptr):nullptr;
        if(!g_p93ControlGO||!g_p93ControlTr||!hostTr){g_p93ControlGO=nullptr;g_p93ControlTr=nullptr;return;}
        // The timeline container uses a clipping/layout group, so place our
        // control beside it in CalendarUI instead of nesting inside it.
        void* calendarParent=M_Transform_get_parent?invoke(M_Transform_get_parent,hostTr,nullptr):nullptr;if(!calendarParent){g_p93ControlGO=nullptr;g_p93ControlTr=nullptr;return;}
        bool keepWorld=false;void* pa[2]={calendarParent,&keepWorld};invoke(M_Transform_SetParent,g_p93ControlTr,pa);
        // The Messages button occupies the only free horizontal gap beside the
        // date.  Put the compact cycle button immediately above the date card.
        // Match the Mail button's 60px footprint (120 canvas units) and line
        // its right edge up with the date card rather than the screen edge.
        F2 anchor{1.0f,0.0f},pivot{1.0f,0.0f},size{120.0f,120.0f},pos{-16.0f,250.0f};set_vec2_prop(g_p93ControlTr,M_RectTransform_set_anchorMin,anchor);set_vec2_prop(g_p93ControlTr,M_RectTransform_set_anchorMax,anchor);set_vec2_prop(g_p93ControlTr,M_RectTransform_set_pivot,pivot);set_vec2_prop(g_p93ControlTr,M_RectTransform_set_sizeDelta,size);set_vec2_prop(g_p93ControlTr,M_RectTransform_set_anchoredPosition,pos);
        p93_disable_button_component(g_p93ControlGO,"UnityEngine.UI","Button");p93_disable_button_component(g_p93ControlGO,"TPS.Core.UI","UIButton");
        p93_refresh_control_state();
        log_raw("DAY & NIGHT CYCLE: date-adjacent control attached. Left-click opens settings; right-click cycles lighting.\r\n");
    }
    if(!g_p93ControlTr)return;p96_apply_auto_cycle();bool down=(pGetAsyncKeyState(0x01)&0x8000)!=0,rightDown=(pGetAsyncKeyState(0x02)&0x8000)!=0;bool previousMouseDown=g_p93MouseWasDown;bool pressed=down&&!previousMouseDown,released=!down&&previousMouseDown,rightPressed=rightDown&&!g_p96RightMouseWasDown;g_p93MouseWasDown=down;g_p96RightMouseWasDown=rightDown;F3 mp{};bool haveMouse=mouse_position(mp),inside=haveMouse&&p97_hit(g_p93ControlTr,mp),insideManualTab=g_p97PopoverOpen&&haveMouse&&p97_hit(g_p99ManualTr,mp),insideCalendarTab=g_p97PopoverOpen&&haveMouse&&p97_hit(g_p99CalendarTr,mp),insideWeek=g_p97PopoverOpen&&g_p99ActiveTab==1&&haveMouse&&p97_hit(g_p97WeekTr,mp),insideMonth=g_p97PopoverOpen&&g_p99ActiveTab==1&&haveMouse&&p97_hit(g_p97MonthTr,mp),insideClose=g_p97PopoverOpen&&haveMouse&&p97_hit(g_p98CloseTr,mp),insideRaisedHeader=g_p97PopoverOpen&&haveMouse&&(p97_hit(g_p98HeaderTr,mp)||p97_hit(g_p98TileOuterTr,mp)||insideClose),insidePopover=g_p97PopoverOpen&&haveMouse&&(p97_hit(g_p98PanelTr,mp)||insideRaisedHeader||insideManualTab||insideCalendarTab);bool insideProfile[4]{};if(g_p97PopoverOpen&&g_p99ActiveTab==0&&haveMouse)for(int i=0;i<4;++i)insideProfile[i]=p97_hit(g_p101ManualTr[i],mp);bool insideTimeline=g_p97PopoverOpen&&g_p99ActiveTab==1&&haveMouse&&p97_hit(g_p106TimelineTr,mp);bool insideTimelineHandle[3]{};if(g_p97PopoverOpen&&g_p99ActiveTab==1&&haveMouse)for(int i=0;i<3;++i){insideTimelineHandle[i]=p97_hit(g_p106TimelineHandleTr[i],mp);insideTimeline=insideTimeline||insideTimelineHandle[i];}void* tooltipRoot=g_p98PanelTr&&M_Transform_get_parent?invoke(M_Transform_get_parent,g_p98PanelTr,nullptr):g_p98PanelTr;p103_drive_tab_tooltip(0,insideManualTab,tooltipRoot,mp);p103_drive_tab_tooltip(1,insideCalendarTab,tooltipRoot,mp);int variant=inside?((down||rightDown)?2:1):0;if(variant!=g_p95ButtonVariant){g_p95ButtonVariant=variant;p93_refresh_control_state();p95_update_tooltip();}
    bool insideReset=g_p97PopoverOpen&&g_p99ActiveTab==1&&haveMouse&&p97_hit(g_p106ResetTr,mp);
    if(g_p97PopoverOpen){int manualVariant=insideManualTab?(down?2:1):0,calendarVariant=insideCalendarTab?(down?2:1):0,manualVisual=(g_p99ActiveTab==0?10:0)+manualVariant,calendarVisual=(g_p99ActiveTab==1?10:0)+calendarVariant;if(manualVisual!=g_p99ManualVisual){p99_set_tab_art(g_p99ManualImage,g_p99ActiveTab==0,manualVariant);g_p99ManualVisual=manualVisual;}if(calendarVisual!=g_p99CalendarVisual){p99_set_tab_art(g_p99CalendarImage,g_p99ActiveTab==1,calendarVariant);g_p99CalendarVisual=calendarVisual;}if(g_p99ActiveTab==1){int weekVariant=insideWeek?(down?2:1):0,monthVariant=insideMonth?(down?2:1):0,weekVisual=(g_p96AutoMode==1?10:0)+weekVariant,monthVisual=(g_p96AutoMode==2?10:0)+monthVariant;if(weekVisual!=g_p97WeekVisual){p97_set_selector_art(g_p97WeekPanelImage,g_p97WeekIconImage,g_p96AutoMode==1,weekVariant);p97_set_label(g_p97WeekLabel,nullptr,g_p96AutoMode==1);g_p97WeekVisual=weekVisual;}if(monthVisual!=g_p97MonthVisual){p97_set_selector_art(g_p97MonthPanelImage,g_p97MonthIconImage,g_p96AutoMode==2,monthVariant);p97_set_label(g_p97MonthLabel,nullptr,g_p96AutoMode==2);g_p97MonthVisual=monthVisual;}}else{for(int i=0;i<4;++i){int profileVariant=insideProfile[i]?(down?2:1):0;int profileVisual=((g_p96AutoMode==0&&g_p91Profile==i)?10:0)+profileVariant;if(profileVisual!=g_p101ManualVisual[i]){p101_set_manual_art(i,profileVariant);g_p101ManualVisual[i]=profileVisual;}}}}
    int resetVisual=insideReset?(down?2:1):0;if(resetVisual!=g_p106ResetVisual){g_p106ResetVisual=resetVisual;if(g_p106ResetImage&&M_Graphic_set_color){F4 resetColor=resetVisual==2?F4{.96f,.46f,.04f,1}:resetVisual==1?F4{1.0f,.88f,.18f,1}:F4{1.0f,.78f,.08f,.96f};void* a[1]={&resetColor};invoke(M_Graphic_set_color,g_p106ResetImage,a);}F4 glyphColor=resetVisual==2?F4{1,1,1,1}:F4{.08f,.24f,.40f,1};if(g_p106ResetGlyphImage&&M_Graphic_set_color){void* a[1]={&glyphColor};invoke(M_Graphic_set_color,g_p106ResetGlyphImage,a);}p97_set_label(g_p106ResetLabel,nullptr,resetVisual==2);}
    if(g_p106DragBoundary>=0){if(down&&haveMouse){if(p106_drag_boundary(g_p106DragBoundary,mp))g_p106DragDirty=true;return;}if(released){if(g_p106DragDirty)dncSaveCurrentPreference("custom automatic phase lengths");g_p106DragBoundary=-1;g_p106DragDirty=false;log_raw("DAY & NIGHT CYCLE: automatic phase-length divider released.\r\n");return;}}
    if(pressed&&insideTimeline){g_p106DragBoundary=-1;for(int i=0;i<3;++i)if(insideTimelineHandle[i]){g_p106DragBoundary=i;break;}if(g_p106DragBoundary<0)g_p106DragBoundary=p106_nearest_boundary(mp);g_p106DragDirty=p106_drag_boundary(g_p106DragBoundary,mp);return;}
    if(pressed&&insideReset){g_dncPhaseHoldUnits[0]=100;g_dncPhaseHoldUnits[1]=900;g_dncPhaseHoldUnits[2]=200;g_dncPhaseHoldUnits[3]=400;g_p96LastProfile=-1;p106_refresh_timeline_geometry();p102_refresh_page_text();dncSaveCurrentPreference("standard automatic phase lengths restored");log_raw("DAY & NIGHT CYCLE: standard 1h Dawn, 9h Day, 2h Dusk and 4h Night holds restored.\r\n");return;}
    if(rightPressed){if(inside){p104_cycle_lighting_from_hud();return;}if(g_p97PopoverOpen&&!insidePopover)p97_set_popover_active(false);return;}
    if(!pressed)return;
    if(g_p97PopoverOpen&&insideClose){p97_set_popover_active(false);return;}
    if(g_p97PopoverOpen&&insideManualTab){g_p99ActiveTab=0;p99_show_active_page();p93_refresh_control_state();p95_update_tooltip();log_raw(g_p96AutoMode?"DAY & NIGHT CYCLE: Manual options page opened; automatic mode remains active.\r\n":"DAY & NIGHT CYCLE: Manual options page opened.\r\n");return;}
    if(g_p97PopoverOpen&&insideCalendarTab){g_p99ActiveTab=1;p99_show_active_page();log_raw("DAY & NIGHT CYCLE: Calendar tab selected.\r\n");return;}
    if(g_p97PopoverOpen&&insideWeek){g_p96AutoMode=1;g_p96LastProfile=-1;p96_apply_auto_cycle();dncSaveCurrentPreference("Weekly automatic mode");p93_refresh_control_state();p95_update_tooltip();p102_refresh_page_text();log_raw("DAY & NIGHT CYCLE: automatic weekly calendar mode enabled.\r\n");return;}
    if(g_p97PopoverOpen&&insideMonth){g_p96AutoMode=2;g_p96LastProfile=-1;p96_apply_auto_cycle();dncSaveCurrentPreference("Monthly automatic mode");p93_refresh_control_state();p95_update_tooltip();p102_refresh_page_text();log_raw("DAY & NIGHT CYCLE: automatic monthly calendar mode enabled.\r\n");return;}
    if(g_p97PopoverOpen&&g_p99ActiveTab==0){for(int i=0;i<4;++i)if(insideProfile[i]){g_p96AutoMode=0;g_p91Profile=i;g_dncManualProfile=i;dncSaveCurrentPreference("Manual lighting selection");for(int j=0;j<4;++j)g_p101ManualVisual[j]=-1;p93_refresh_control_state();p95_update_tooltip();p102_refresh_page_text();const char* names[4]={"Day","Dusk","Night","Dawn"};char line[180];psprintf(line,"DAY & NIGHT CYCLE: Manual page selected %s.\r\n",names[i]);log_raw(line);return;}}
    if(g_p97PopoverOpen&&insidePopover)return;
    if(!inside){if(g_p97PopoverOpen)p97_set_popover_active(false);return;}
    {bool open=!g_p97PopoverOpen;if(open&&p97_ensure_popover())p97_set_popover_active(true);else p97_set_popover_active(false);}
}
static void p91_exterior_light_tick(){
    // The CalendarUI exists independently of the active lighting environment.
    // Create/maintain its control even while a museum is still exposing its
    // LightingManager; otherwise a transient null environment hides the button.
    if(!p91_bind_active_environment()||!M_Light_set_color||!M_Light_set_intensity)return;
    if(!g_p91Captured){
        u64 configKey=dncLightingConfigKey(g_p91Config);DNCLightingBaseline* baseline=dncFindLightingBaseline(configKey,false);bool reused=baseline!=nullptr;
        if(reused){g_p91DayColor=baseline->lightColor;g_p91DaySkyColor=baseline->skyColor;g_p91DaySunColor=baseline->sunColor;g_p91DayIntensity=baseline->intensity;g_p91DayShadowStrength=baseline->shadowStrength;}
        else {
            const MethodInfo* gc=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)g_p91Config),"get_SunUnityLightColor");
            const MethodInfo* gi=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)g_p91Config),"get_SunUnityLightIntensity");
            g_p91DayColor=gc?read_boxed_f4(invoke(gc,g_p91Config,nullptr)):field_f4(g_p91Config,"SunColor");
            g_p91DayIntensity=gi?boxed_float(invoke(gi,g_p91Config,nullptr),0.0f):1.0f;
            g_p91DaySkyColor=field_f4(g_p91Config,"SkyColor");g_p91DaySunColor=field_f4(g_p91Config,"SunColor");
            g_p91DayShadowStrength=M_Light_get_shadowStrength?boxed_float(invoke(M_Light_get_shadowStrength,g_p91ExteriorLight,nullptr),1.0f):1.0f;
            baseline=dncFindLightingBaseline(configKey,true);if(baseline){baseline->lightColor=g_p91DayColor;baseline->skyColor=g_p91DaySkyColor;baseline->sunColor=g_p91DaySunColor;baseline->intensity=g_p91DayIntensity;baseline->shadowStrength=g_p91DayShadowStrength;}
        }
        g_p91Captured=true;char b[420];psprintf(b,"DAY & NIGHT CYCLE: museum baseline %s key=%llu colour=(%.3f,%.3f,%.3f) intensity=%.3f.\r\n",reused?"restored from immutable cache":"captured",(unsigned long long)configKey,(double)g_p91DayColor.x,(double)g_p91DayColor.y,(double)g_p91DayColor.z,(double)g_p91DayIntensity);log_raw(b);
    }
    dncApplyPreferencesForCurrentMuseum();
    if(g_p96AutoMode){g_dncManualCommittedProfile=-1;return;}
    dncRestoreOctaluxGradualQuality();
    bool leavingAutomatic=g_dncAutoExteriorActive;g_dncAutoExteriorActive=false;F4 color{},sky{},sun{};float intensity=0,shadowStrength=1;dncProfileTarget(g_p91Profile,color,sky,sun,intensity,shadowStrength);
    if(leavingAutomatic){
        // Begin at the exact interpolated Automatic frame currently visible,
        // rather than jumping back to the last remembered Manual endpoint.
        g_dncTransitionEnvironmentGeneration=g_dncLightingEnvironmentGeneration;g_dncTransitionInitialised=true;g_dncTransitionActive=false;g_dncTransitionTargetProfile=-1;g_dncTransitionStartedAt=0;g_dncManualGradualSettleUntil=0;
        g_dncTransitionStartLight=g_dncTransitionTargetLight=g_dncDesiredLightColor;g_dncTransitionStartSky=g_dncTransitionTargetSky=g_dncDesiredSkyColor;g_dncTransitionStartSun=g_dncTransitionTargetSun=g_dncDesiredSunColor;g_dncTransitionStartIntensity=g_dncTransitionTargetIntensity=g_dncDesiredIntensity;g_dncTransitionStartShadowStrength=g_dncTransitionTargetShadowStrength=g_dncDesiredShadowStrength;
    }
    dncSetLightingTransitionTarget(g_p91Profile,color,sky,sun,intensity,shadowStrength);dncAdvanceLightingTransition(p78_clock?p78_clock():0);
    // Never commit Manual through PushLightingRenderSettings: even a single
    // complete push requests Octalux's noisy rebuild and causes room glitter.
    // Record the destination config only after the smooth transition finishes;
    // visibility is driven continuously through the same stable Gradual sky
    // setter and direct exterior-light route used by Automatic.
    bool commitManual=leavingAutomatic||g_dncManualCommittedGeneration!=g_dncLightingEnvironmentGeneration||g_dncManualCommittedProfile!=g_p91Profile;
    if(commitManual&&!g_dncTransitionActive){set_field_f4(g_p91Config,"SunColor",g_dncDesiredSunColor);set_field_f4(g_p91Config,"SkyColor",g_dncDesiredSkyColor);g_dncManualCommittedProfile=g_p91Profile;g_dncManualCommittedGeneration=g_dncLightingEnvironmentGeneration;log_raw("DAY & NIGHT CYCLE: Manual transition reached its destination on the stable Gradual route; no complete lighting push was issued.\r\n");}
    dncApplyOctaluxSky();g_dncLightingHoldReady=true;
}
static void p91_hold_lighting_tick(){
    if(!g_dncLightingHoldReady||!g_p91Captured||!g_p91Config||!g_p91Environment||!g_p91ExteriorLight||!g_p91InteriorLight)return;
    if(g_p96AutoMode){p96_apply_auto_cycle();if(!g_dncAutoExteriorActive)return;dncApplyOctaluxSky();g_dncLightingHoldReady=true;return;}
    // A UI click can leave automatic mode between the fast UI tick and the
    // next 150 ms profile tick. Do not commit the last interpolated automatic
    // sky/sun values through the shared render path during that brief gap.
    if(g_dncAutoExteriorActive){g_dncAutoExteriorActive=false;g_dncLightingHoldReady=false;return;}
    dncAdvanceLightingTransition(p78_clock?p78_clock():0);
    // Manual uses the same stable Gradual Octalux sky route as Automatic and
    // never performs a full render-settings push during selection or hold.
    dncApplyOctaluxSky();g_dncLightingHoldReady=true;
}

#include "GameThreadBootstrap.h"
