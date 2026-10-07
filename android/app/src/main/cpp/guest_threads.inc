// Guest registers remain serialized; blocked native threads release the machine.
struct GuestThread {
    pthread_t native{};
    uint32_t id{},handle{},start{},parameter{},stack{},tib{},exitCode=259,suspended{};
    int32_t priority=0;
    bool started=false,done=false;
    bool timer=false,periodic=false,cancelled=false;
    uint32_t delay{},timerId{};
};
std::vector<std::shared_ptr<GuestThread>> guestThreads;
std::map<uint32_t,std::shared_ptr<GuestThread>> threadHandles;
uint32_t nextThreadId=2,nextThreadHandle=0xf0030000;
struct GuestEvent {bool manual,signaled;};
std::map<uint32_t,std::shared_ptr<GuestEvent>> eventHandles;
std::map<std::string,std::weak_ptr<GuestEvent>> namedEvents;
uint32_t nextEventHandle=0xf0040000;
struct GuestMutex {uint32_t owner=0,depth=0;bool abandoned=false;};
std::map<uint32_t,std::shared_ptr<GuestMutex>> mutexHandles;
std::map<std::string,std::weak_ptr<GuestMutex>> namedMutexes;
uint32_t nextMutexHandle=0xf0080000;
void createMutex(){
    auto name=guestString(arg(2));std::shared_ptr<GuestMutex> value;
    if(!name.empty())value=namedMutexes[name].lock();
    if(!name.empty()&&namedEvents.count(name)&&!namedEvents[name].expired()){lastError=6;ret(0,3);return;}
    lastError=value?183:0;
    if(!value){value=std::make_shared<GuestMutex>();if(arg(1)){value->owner=guestThreadId;value->depth=1;}if(!name.empty())namedMutexes[name]=value;}
    uint32_t handle=nextMutexHandle++;mutexHandles.emplace(handle,value);ret(handle,3);
}
void releaseMutex(){
    auto value=mutexHandles.find(arg(0));if(value==mutexHandles.end()){lastError=6;ret(0,1);return;}
    if(value->second->owner!=guestThreadId){lastError=288;ret(0,1);return;}
    if(!--value->second->depth)value->second->owner=0;ret(1,1);
}
void createEvent(){
    auto name=guestString(arg(3));std::shared_ptr<GuestEvent> event;
    if(!name.empty()&&namedMutexes.count(name)&&!namedMutexes[name].expired()){lastError=6;ret(0,4);return;}
    if(!name.empty())event=namedEvents[name].lock();
    lastError=event?183:0;
    if(!event){event=std::make_shared<GuestEvent>(GuestEvent{arg(1)!=0,arg(2)!=0});if(!name.empty())namedEvents[name]=event;}
    uint32_t handle=nextEventHandle++;eventHandles.emplace(handle,event);ret(handle,4);
}
void setEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=true;ret(1,1);}
void resetEvent(){auto event=eventHandles.find(arg(0));if(event==eventHandles.end()){lastError=6;ret(0,1);return;}event->second->signaled=false;ret(1,1);}
void* guestThreadEntry(void* context){
    auto& thread=*static_cast<GuestThread*>(context);
    std::unique_lock<std::recursive_mutex> lease(machine);machineLease=&lease;guestThreadId=thread.id;
    State fresh;fresh.esp=thread.stack+(4u<<20)-64;fresh.fs=thread.tib;fresh.load();
    try {
        while(thread.suspended){checkGuestProgress();yieldGuestMachine(1);}
        if(thread.timer){
            do {
                auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(thread.delay);
                while(!thread.cancelled&&std::chrono::steady_clock::now()<deadline){checkGuestProgress();yieldGuestMachine(1);}
                if(thread.cancelled)break;
                callGuest(thread.start,{thread.timerId,0,thread.parameter,0,0});
            }while(thread.periodic&&!thread.cancelled);
            thread.exitCode=0;
        }else thread.exitCode=callGuest(thread.start,{thread.parameter});
    }catch(const GuestStop& error){
        thread.exitCode=1;
        if(!stopRequested){guestThreadFault=error.what();NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSU2","Guest thread %u: %s",thread.id,error.what());}
    }catch(const std::exception& error){thread.exitCode=1;guestThreadFault=error.what();}
    heaps->free(GuestHeaps::process,thread.stack);heaps->free(GuestHeaps::process,thread.tib);
    for(auto& value:mutexHandles)if(value.second->owner==thread.id){value.second->owner=0;value.second->depth=0;value.second->abandoned=true;}
    thread.done=true;machineLease=nullptr;return nullptr;
}
void reapGuestThreads(){
    for(auto it=guestThreads.begin();it!=guestThreads.end();){
        auto& thread=*it;
        if(thread->done){if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}it=guestThreads.erase(it);}
        else ++it;
    }
}
void createThread(){
    reapGuestThreads();
    uint32_t flags=arg(4),idOut=arg(5);
    if(flags&~4u){lastError=87;ret(0,6);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->handle=nextThreadHandle++;
    thread->start=arg(2);thread->parameter=arg(3);thread->suspended=flags&4?1:0;
    if(!recomp_lookup(thread->start)){lastError=87;ret(0,6);return;}
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);lastError=8;ret(0,6);return;}
    thread->started=true;guestThreads.push_back(thread);threadHandles.emplace(thread->handle,thread);
    if(idOut)write32(idOut,thread->id);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Created guest thread %u at %08x suspended=%u",thread->id,thread->start,thread->suspended);
    ret(thread->handle,6);
}
void resumeThread(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(UINT32_MAX,1);return;}uint32_t old=thread->second->suspended;if(old)--thread->second->suspended;ret(old,1);}
void setThreadPriority(){
    uint32_t handle=arg(0);int32_t priority=static_cast<int32_t>(arg(1));
    if(priority< -2&&priority!= -15){lastError=87;ret(0,2);return;}if(priority>2&&priority!=15){lastError=87;ret(0,2);return;}
    auto thread=threadHandles.find(handle);
    if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}
    thread->second->priority=priority;ret(1,2);
}
void threadExitCode(){auto thread=threadHandles.find(arg(0));if(thread==threadHandles.end()){lastError=6;ret(0,2);return;}write32(arg(1),thread->second->exitCode);ret(1,2);}
void waitThread(){
    uint32_t handle=arg(0),timeout=arg(1);auto found=threadHandles.find(handle);
    auto mutexFound=mutexHandles.find(handle);
    if(mutexFound!=mutexHandles.end()){
        auto value=mutexFound->second;auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
        while(value->owner&&value->owner!=guestThreadId){if(timeout!=UINT32_MAX&&std::chrono::steady_clock::now()>=deadline){ret(258,2);return;}checkGuestProgress();yieldGuestMachine(1);}
        value->owner=guestThreadId;++value->depth;bool abandoned=value->abandoned;value->abandoned=false;ret(abandoned?128:0,2);return;
    }
    auto eventFound=eventHandles.find(handle);
    if(found==threadHandles.end()&&eventFound==eventHandles.end()){lastError=6;ret(UINT32_MAX,2);return;}
    auto thread=found==threadHandles.end()?nullptr:found->second;
    auto event=eventFound==eventHandles.end()?nullptr:eventFound->second;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(thread?!thread->done:!event->signaled){if(timeout!=UINT32_MAX&&std::chrono::steady_clock::now()>=deadline){ret(258,2);return;}checkGuestProgress();yieldGuestMachine(1);}
    if(event&&!event->manual)event->signaled=false;
    ret(0,2);
}
void joinGuestThreads(std::unique_lock<std::recursive_mutex>& lease){
    stopRequested=true;lease.unlock();
    for(auto& thread:guestThreads)if(thread->started){pthread_join(thread->native,nullptr);thread->started=false;}
    lease.lock();
}
std::map<uint32_t,std::shared_ptr<GuestThread>> timerHandles;
uint32_t nextTimerId=1;
void timeSetEvent(){
    reapGuestThreads();
    uint32_t flags=arg(4),delay=arg(0),callback=arg(2);
    if(flags&~0x101u)throw GuestStop("timeSetEvent callback flags pending: "+hex(flags));
    if(!delay||!recomp_lookup(callback)){ret(0,5);return;}
    auto thread=std::make_shared<GuestThread>();thread->id=nextThreadId++;thread->timer=true;thread->periodic=flags&1;
    thread->delay=delay;thread->start=callback;thread->parameter=arg(3);thread->timerId=nextTimerId++;
    thread->stack=allocate(4u<<20);thread->tib=allocate(4096);
    write32(thread->tib,UINT32_MAX);write32(thread->tib+4,thread->stack+(4u<<20));write32(thread->tib+8,thread->stack);
    write32(thread->tib+0x18,thread->tib);write32(thread->tib+0x20,1);write32(thread->tib+0x24,thread->id);
    pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
    if(!error){error=pthread_attr_setstacksize(&attributes,64u<<20);if(!error)error=pthread_create(&thread->native,&attributes,guestThreadEntry,thread.get());pthread_attr_destroy(&attributes);}
    if(error){heaps->free(GuestHeaps::process,thread->stack);heaps->free(GuestHeaps::process,thread->tib);ret(0,5);return;}
    thread->started=true;guestThreads.push_back(thread);timerHandles.emplace(thread->timerId,thread);
    if(thread->timerId<=3||thread->timerId%500==0)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest timer %u callback=%08x period=%u",thread->timerId,callback,delay);
    ret(thread->timerId,5);
}
void timeKillEvent(){
    auto timer=timerHandles.find(arg(0));if(timer==timerHandles.end()){ret(97,1);return;}
    auto thread=timer->second;thread->cancelled=true;
    if(thread->id==guestThreadId)throw GuestStop("Synchronous timer self-cancellation pending");
    while(!thread->done){checkGuestProgress();yieldGuestMachine(1);}
    timerHandles.erase(timer);ret(0,1);
}
