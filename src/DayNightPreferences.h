#pragma once

// Day & Night Cycle owns this entire subsystem. It deliberately has its own
// scope, snapshot format, filenames, validation, backups and runtime state.
// No other mod DLL, export, storage structure or symbol is consulted.
struct DNCPreferenceScope {
    u32 gameMode;
    u32 saveSlot;
    u32 career;
    u32 museum;
};
static bool dncPreferenceSameScope(DNCPreferenceScope a,DNCPreferenceScope b){
    return a.gameMode==b.gameMode&&a.saveSlot==b.saveSlot&&a.career==b.career&&a.museum==b.museum;
}

struct DNCPreferenceSnapshot {
    u32 magic;
    u32 version;
    DNCPreferenceScope scope;
    u32 cycleMode;      // 0 Manual, 1 Weekly, 2 Monthly
    u32 manualProfile;  // 0 Day, 1 Dusk, 2 Night, 3 Dawn
    u32 phaseHoldUnits[4]; // Dawn, Day, Dusk, Night; 100 units = one virtual hour
    u32 checksum;
};
static_assert(sizeof(DNCPreferenceScope)==16&&sizeof(DNCPreferenceSnapshot)==52,"Day & Night Cycle preference layout changed");
static constexpr u32 DNC_PREFERENCE_MAGIC=0x32434E44u; // DNC2
static constexpr u32 DNC_PREFERENCE_VERSION=2u;
static constexpr u32 DNC_PHASE_HOLD_TOTAL=1600u;
static constexpr u32 DNC_PHASE_HOLD_MIN=50u;

static DWORD (*dncPreferenceGetFileSize)(HANDLE,DWORD*)=nullptr;
static BOOL (*dncPreferenceFlushFile)(HANDLE)=nullptr;
static BOOL (*dncPreferenceMoveFile)(LPCWSTR,LPCWSTR,DWORD)=nullptr;
static BOOL (*dncPreferenceCopyFile)(LPCWSTR,LPCWSTR,BOOL)=nullptr;
static DWORD (*dncPreferenceLastError)()=nullptr;

static DNCPreferenceScope g_dncPreferenceScope{};
static DNCPreferenceSnapshot g_dncPreferenceSnapshot{};
static u32 g_dncPreferenceScopeGeneration=0;
static bool g_dncPreferenceHaveScope=false;
static bool g_dncPreferenceHaveStored=false;
static bool g_dncPreferencePrimaryHealthy=false;
static bool g_dncPreferenceWritable=false;
static u64 g_dncPreferenceRetryAfter=0;
static wchar_t g_dncPreferencePath[800]{};
static wchar_t g_dncPreferenceBackupPath[800]{};
static wchar_t g_dncPreferenceTempPath[800]{};

static u32 dncPreferenceChecksum(DNCPreferenceSnapshot snapshot){
    snapshot.checksum=0;u32 value=2166136261u;const u8* bytes=(const u8*)&snapshot;
    for(u32 i=0;i<(u32)sizeof(snapshot);++i)value=(value^bytes[i])*16777619u;
    return value;
}
static bool dncPreferenceValidPhaseHolds(const u32* phaseHoldUnits){
    if(!phaseHoldUnits)return false;u32 total=0;for(int i=0;i<4;++i){if(phaseHoldUnits[i]<DNC_PHASE_HOLD_MIN)return false;total+=phaseHoldUnits[i];}return total==DNC_PHASE_HOLD_TOTAL;
}
static DNCPreferenceSnapshot dncPreferenceMakeSnapshot(DNCPreferenceScope scope,u32 mode,u32 manualProfile,const u32* phaseHoldUnits){
    static const u32 defaults[4]={100,900,200,400};DNCPreferenceSnapshot snapshot{};snapshot.magic=DNC_PREFERENCE_MAGIC;snapshot.version=DNC_PREFERENCE_VERSION;snapshot.scope=scope;snapshot.cycleMode=mode;snapshot.manualProfile=manualProfile;const u32* holds=dncPreferenceValidPhaseHolds(phaseHoldUnits)?phaseHoldUnits:defaults;for(int i=0;i<4;++i)snapshot.phaseHoldUnits[i]=holds[i];snapshot.checksum=dncPreferenceChecksum(snapshot);return snapshot;
}
static bool dncPreferenceValid(const DNCPreferenceSnapshot& snapshot,DNCPreferenceScope scope){
    return snapshot.magic==DNC_PREFERENCE_MAGIC&&snapshot.version==DNC_PREFERENCE_VERSION&&dncPreferenceSameScope(snapshot.scope,scope)&&snapshot.cycleMode<=2&&snapshot.manualProfile<=3&&dncPreferenceValidPhaseHolds(snapshot.phaseHoldUnits)&&snapshot.checksum==dncPreferenceChecksum(snapshot);
}
static bool dncPreferenceResolveIo(){
    if(dncPreferenceGetFileSize&&dncPreferenceFlushFile&&dncPreferenceMoveFile&&dncPreferenceCopyFile&&dncPreferenceLastError)return true;
    void* kernel=find_loaded_module("kernel32.dll");if(!kernel)return false;
    dncPreferenceGetFileSize=(decltype(dncPreferenceGetFileSize))resolve_export(kernel,"GetFileSize");
    dncPreferenceFlushFile=(decltype(dncPreferenceFlushFile))resolve_export(kernel,"FlushFileBuffers");
    dncPreferenceMoveFile=(decltype(dncPreferenceMoveFile))resolve_export(kernel,"MoveFileExW");
    dncPreferenceCopyFile=(decltype(dncPreferenceCopyFile))resolve_export(kernel,"CopyFileW");
    dncPreferenceLastError=(decltype(dncPreferenceLastError))resolve_export(kernel,"GetLastError");
    return dncPreferenceGetFileSize&&dncPreferenceFlushFile&&dncPreferenceMoveFile&&dncPreferenceCopyFile&&dncPreferenceLastError;
}
static void* dncPreferenceCurrentLevel(){
    Il2CppClass* levelStateClass=find_class("TPS.Game","LevelState");if(!levelStateClass)return nullptr;
    void* state=static_ref_field(levelStateClass,"<Instance>k__BackingField");
    if(!state){const MethodInfo* getInstance=find_method0_hierarchy(levelStateClass,"get_Instance");if(getInstance)state=invoke(getInstance,nullptr,nullptr);}
    if(!state)return nullptr;void* level=field_object(state,"Level");if(!level)level=field_object(state,"<Level>k__BackingField");
    if(!level){const MethodInfo* getLevel=find_method0_hierarchy(il2cpp_object_get_class((Il2CppObject*)state),"get_Level");if(getLevel)level=invoke(getLevel,state,nullptr);}
    return level;
}
static bool dncPreferenceCurrentScope(DNCPreferenceScope& output,void*& level){
    level=dncPreferenceCurrentLevel();if(!level||!boxed_bool(field_object(level,"<LoadHUDComplete>k__BackingField")))return false;
    void* history=field_object(level,"<LevelPlaythroughHistory>k__BackingField");if(!history)return false;
    Il2CppClass* appClass=find_class("TPS.Core","App");void* app=appClass?static_ref_field(appClass,"<Instance>k__BackingField"):nullptr;
    if(!app||boxed_bool(field_object(app,"<IsQuitting>k__BackingField"))||boxed_bool(field_object(app,"<IsLoading>k__BackingField")))return false;
    void* saveSystem=field_object(app,"_saveSystem");void* context=saveSystem?field_object(saveSystem,"_context"):nullptr;if(!saveSystem||!context)return false;
    const char* contextName=il2cpp_class_get_name(il2cpp_object_get_class((Il2CppObject*)context));
    int gameMode=streq(contextName,"CareerSaveSystemContext")?1:streq(contextName,"SandboxSaveSystemContext")?2:0;
    int saveSlot=field_i32(saveSystem,"_currentSaveSlot",-1);
    void* career=field_object(history,"CareerPlaythroughIDPlaythroughStarted");if(!career)career=field_object(history,"CareerPlaythroughID");
    void* museum=field_object(history,"LevelPlaythroughID");
    if(!gameMode||saveSlot<0||!career||!museum)return false;
    output={(u32)gameMode,(u32)saveSlot,(u32)boxed_i32((Il2CppObject*)career),(u32)boxed_i32((Il2CppObject*)museum)};
    return output.career!=0&&output.museum!=0;
}
enum DNCPreferenceReadResult { DNC_PREFERENCE_CORRUPT=-1,DNC_PREFERENCE_MISSING=0,DNC_PREFERENCE_VALID=1,DNC_PREFERENCE_TRANSIENT=-2 };
static int dncPreferenceReadFile(const wchar_t* path,DNCPreferenceScope scope,DNCPreferenceSnapshot& output){
    if(!path||!path[0])return DNC_PREFERENCE_CORRUPT;HANDLE file=pCreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){DWORD error=dncPreferenceLastError();return error==2||error==3?DNC_PREFERENCE_MISSING:DNC_PREFERENCE_TRANSIENT;}
    DWORD high=0,size=dncPreferenceGetFileSize(file,&high),read=0;DNCPreferenceSnapshot candidate{};
    bool ok=!high&&size==sizeof(candidate)&&pReadFile(file,&candidate,sizeof(candidate),&read,nullptr)&&read==sizeof(candidate)&&dncPreferenceValid(candidate,scope);
    pCloseHandle(file);if(ok)output=candidate;return ok?DNC_PREFERENCE_VALID:DNC_PREFERENCE_CORRUPT;
}
static bool dncPreferenceCommit(const DNCPreferenceSnapshot& snapshot){
    if(!g_dncPreferenceHaveScope||!g_dncPreferenceWritable)return false;
    HANDLE file=pCreateFileW(g_dncPreferenceTempPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=pWriteFile(file,&snapshot,sizeof(snapshot),&written,nullptr)&&written==sizeof(snapshot)&&dncPreferenceFlushFile(file);pCloseHandle(file);
    if(ok&&g_dncPreferencePrimaryHealthy&&!dncPreferenceCopyFile(g_dncPreferencePath,g_dncPreferenceBackupPath,0))log_raw("DAY & NIGHT CYCLE PREFERENCES: backup refresh failed; committing the flushed primary update without replacing the previous backup.\r\n");
    if(ok)ok=dncPreferenceMoveFile(g_dncPreferenceTempPath,g_dncPreferencePath,9)!=0;
    if(ok){g_dncPreferenceSnapshot=snapshot;g_dncPreferenceHaveStored=true;g_dncPreferencePrimaryHealthy=true;return true;}
    log_raw("DAY & NIGHT CYCLE PREFERENCES: commit failed; previous file retained.\r\n");return false;
}
static bool dncPreferenceOpenScope(int defaultManualProfile){
    if(defaultManualProfile<0||defaultManualProfile>3)defaultManualProfile=0;
    if(!dncPreferenceResolveIo()||!g_dncPreferenceFolder[0]||!g_dncPreferenceBasePath[0])return false;
    if(!pCreateDirectoryW(g_dncPreferenceFolder,nullptr)&&dncPreferenceLastError()!=183)return false;
    DNCPreferenceScope next{};void* level=nullptr;if(!dncPreferenceCurrentScope(next,level))return false;
    if(g_dncPreferenceHaveScope&&dncPreferenceSameScope(next,g_dncPreferenceScope))return g_dncPreferenceWritable;
    u64 now=p78_clock?p78_clock():0;if(g_dncPreferenceRetryAfter&&now&&now<g_dncPreferenceRetryAfter)return false;
    // Store value identities only. Unity owns the Level object and can destroy
    // it during a museum handover, so a raw managed pointer must never become
    // part of Day & Night Cycle's persistent runtime state.
    char suffix[160];psprintf(suffix,".v2-%u-%u-%u-%u.bin",next.gameMode,next.saveSlot,next.career,next.museum);wcopy(g_dncPreferencePath,sizeof(g_dncPreferencePath)/sizeof(g_dncPreferencePath[0]),g_dncPreferenceBasePath);wappend_ascii(g_dncPreferencePath,sizeof(g_dncPreferencePath)/sizeof(g_dncPreferencePath[0]),suffix);wcopy(g_dncPreferenceBackupPath,sizeof(g_dncPreferenceBackupPath)/sizeof(g_dncPreferenceBackupPath[0]),g_dncPreferencePath);wappend(g_dncPreferenceBackupPath,sizeof(g_dncPreferenceBackupPath)/sizeof(g_dncPreferenceBackupPath[0]),L".bak");wcopy(g_dncPreferenceTempPath,sizeof(g_dncPreferenceTempPath)/sizeof(g_dncPreferenceTempPath[0]),g_dncPreferencePath);wappend(g_dncPreferenceTempPath,sizeof(g_dncPreferenceTempPath)/sizeof(g_dncPreferenceTempPath[0]),L".tmp");
    DNCPreferenceSnapshot loaded{};int primary=dncPreferenceReadFile(g_dncPreferencePath,next,loaded);g_dncPreferencePrimaryHealthy=primary==DNC_PREFERENCE_VALID;
    int backup=primary==DNC_PREFERENCE_VALID?DNC_PREFERENCE_MISSING:dncPreferenceReadFile(g_dncPreferenceBackupPath,next,loaded);
    if(primary!=DNC_PREFERENCE_VALID&&backup!=DNC_PREFERENCE_VALID&&(primary==DNC_PREFERENCE_TRANSIENT||backup==DNC_PREFERENCE_TRANSIENT)){g_dncPreferenceRetryAfter=now?now+1000:0;log_raw("DAY & NIGHT CYCLE PREFERENCES: storage is temporarily unavailable; retrying shortly.\r\n");return false;}
    g_dncPreferenceRetryAfter=0;g_dncPreferenceScope=next;g_dncPreferenceHaveScope=true;++g_dncPreferenceScopeGeneration;
    g_dncPreferenceHaveStored=primary==DNC_PREFERENCE_VALID||backup==DNC_PREFERENCE_VALID;g_dncPreferenceWritable=g_dncPreferenceHaveStored||(primary==DNC_PREFERENCE_MISSING&&backup==DNC_PREFERENCE_MISSING);
    g_dncPreferenceSnapshot=g_dncPreferenceHaveStored?loaded:dncPreferenceMakeSnapshot(next,0,(u32)defaultManualProfile,nullptr);
    if(backup==DNC_PREFERENCE_VALID)log_raw("DAY & NIGHT CYCLE PREFERENCES: recovered the last valid backup.\r\n");
    if(!g_dncPreferenceWritable)log_raw("DAY & NIGHT CYCLE PREFERENCES: damaged storage retained; preference writes are disabled for this museum.\r\n");
    char message[360];psprintf(message,"DAY & NIGHT CYCLE PREFERENCES: scope mode=%u slot=%u career=%u museum=%u saved=%d writable=%d.\r\n",next.gameMode,next.saveSlot,next.career,next.museum,(int)g_dncPreferenceHaveStored,(int)g_dncPreferenceWritable);log_raw(message);
    return g_dncPreferenceWritable;
}
static bool dncPreferenceGet(int& cycleMode,int& manualProfile,u32* phaseHoldUnits,bool& wasStored){
    if(!g_dncPreferenceHaveScope||!g_dncPreferenceWritable)return false;
    cycleMode=(int)g_dncPreferenceSnapshot.cycleMode;manualProfile=(int)g_dncPreferenceSnapshot.manualProfile;if(phaseHoldUnits)for(int i=0;i<4;++i)phaseHoldUnits[i]=g_dncPreferenceSnapshot.phaseHoldUnits[i];wasStored=g_dncPreferenceHaveStored;return true;
}
static bool dncPreferenceSave(int cycleMode,int manualProfile,const u32* phaseHoldUnits){
    if(cycleMode<0||cycleMode>2||manualProfile<0||manualProfile>3||!dncPreferenceValidPhaseHolds(phaseHoldUnits))return false;
    if(!dncPreferenceOpenScope(manualProfile))return false;
    DNCPreferenceScope current{};void* level=nullptr;
    if(!dncPreferenceCurrentScope(current,level)||!dncPreferenceSameScope(current,g_dncPreferenceScope)){log_raw("DAY & NIGHT CYCLE PREFERENCES: context changed; stale preference write refused.\r\n");return false;}
    DNCPreferenceSnapshot next=dncPreferenceMakeSnapshot(current,(u32)cycleMode,(u32)manualProfile,phaseHoldUnits);
    if(g_dncPreferenceHaveStored&&next.checksum==g_dncPreferenceSnapshot.checksum)return true;
    return dncPreferenceCommit(next);
}
