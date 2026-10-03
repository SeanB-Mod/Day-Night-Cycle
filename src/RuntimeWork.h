// Main-thread ownership is captured from the process-owned Unity window.
static DWORD (*p78_threadId)()=nullptr;
static u64 (*p78_clock)()=nullptr;
static DWORD p78_mainThread=0;
static bool p78_main(){return p78_threadId&&p78_mainThread&&p78_threadId()==p78_mainThread;}
static unsigned p78_workDepth=0;
static u64 p78_epoch=0;
struct P78WorkScope {
    P78WorkScope(){if(p78_workDepth++==0)++p78_epoch;}
    ~P78WorkScope(){--p78_workDepth;}
};
// Only stable successful metadata resolutions are cached, never live game objects.
template<class T,int N> struct P78MetadataCache {
    struct Entry {void* owner;char space[96],name[128];T value;bool used;};
    Entry entries[N]{};unsigned next=0;
    bool lookup(void* owner,const char* space,const char* name,T& value){
        for(auto& e:entries)if(e.used&&e.owner==owner&&streq(e.space,space)&&streq(e.name,name)){value=e.value;return true;}
        return false;
    }
    T get(void* owner,const char* space,const char* name){
        T value=nullptr;if(lookup(owner,space,name,value))return value;
        return nullptr;
    }
    void put(void* owner,const char* space,const char* name,T value){
        if(slen(space)>=96||slen(name)>=128)return;
        auto& e=entries[next++%N];e.owner=owner;scopy(e.space,sizeof(e.space),space);scopy(e.name,sizeof(e.name),name);e.value=value;e.used=true;
    }
};
