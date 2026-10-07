std::string guestWorkingDirectory="C:\\nfsu2";
void logicalDrives(){ret(4,0);}
void driveType(){auto path=lower(guestString(arg(0)));ret(path.empty()||path.rfind("c:",0)==0?3:1,1);}
uint32_t fileError(){switch(errno){case ENOENT:return 2;case EACCES:case EPERM:return 5;case EEXIST:return 80;case ENOSPC:return 112;case EBADF:return 6;default:return 1;}}
std::filesystem::path gamePath(std::string guest){
    std::replace(guest.begin(),guest.end(),'\\','/');auto normalized=lower(guest);
    if(normalized=="c:/nfsu2")guest.clear();
    else if(normalized.rfind("c:/nfsu2/",0)==0)guest=guest.substr(9);
    else if(guest.find(':')!=std::string::npos||guest.rfind("//",0)==0)throw GuestStop("Unsupported guest path: "+guest);
    else {if(!guest.empty()&&guest.front()=='/')guest.erase(0,1);else{auto current=guestWorkingDirectory.substr(8);std::replace(current.begin(),current.end(),'\\','/');if(!current.empty())guest=current+"/"+guest;}}
    auto relative=std::filesystem::path(guest).lexically_normal();for(const auto& part:relative)if(part=="..")throw GuestStop("Guest path leaves nfsu2: "+guest);
    auto result=std::filesystem::path(hostGameRoot);
    for(const auto& part:relative){if(part==".")continue;std::error_code error;bool found=false;
        for(auto& entry:std::filesystem::directory_iterator(result,error))if(lower(entry.path().filename().string())==lower(part.string())){result=entry.path();found=true;break;}
        if(!found)result/=part;
    }
    auto root=std::filesystem::weakly_canonical(hostGameRoot);auto resolved=std::filesystem::weakly_canonical(result);
    auto check=resolved.lexically_relative(root);for(const auto& part:check)if(part=="..")throw GuestStop("Guest symlink leaves data root");return resolved;
}
std::map<uint32_t,int> fileHandles;uint32_t nextFileHandle=0xf0060000;
bool closeGuestFile(uint32_t handle){auto file=fileHandles.find(handle);if(file==fileHandles.end())return false;::close(file->second);fileHandles.erase(file);return true;}
void longPath(){
    auto path=gamePath(guestString(arg(0)));std::error_code error;if(!std::filesystem::exists(path,error)){lastError=2;ret(0,3);return;}
    auto relative=path.lexically_relative(std::filesystem::weakly_canonical(hostGameRoot)).string();
    std::string result="C:\\nfsu2";if(relative!="."&&!relative.empty())result+="\\"+relative;std::replace(result.begin(),result.end(),'/','\\');
    if(arg(2)<=result.size()){ret(result.size()+1,3);return;}std::memcpy(ptr(arg(1),result.size()+1),result.c_str(),result.size()+1);ret(result.size(),3);
}
void createFile(){
    auto path=gamePath(guestString(arg(0)));uint32_t access=arg(1),disposition=arg(4),flags=arg(5);
    if(flags&0x40000000u)throw GuestStop("Overlapped CreateFile requires asynchronous file service");
    int options=access&0x40000000u?(access&0x80000000u?O_RDWR:O_WRONLY):O_RDONLY;
    switch(disposition){case 1:options|=O_CREAT|O_EXCL;break;case 2:options|=O_CREAT|O_TRUNC;break;case 3:break;case 4:options|=O_CREAT;break;case 5:options|=O_TRUNC;break;default:lastError=87;ret(UINT32_MAX,7);return;}
    bool existed=std::filesystem::exists(path);int descriptor=::open(path.c_str(),options|O_CLOEXEC,0600);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","Guest file %s access=%x disposition=%u",path.filename().string().c_str(),access,disposition);
    if(descriptor<0){lastError=fileError();ret(UINT32_MAX,7);return;}uint32_t handle=nextFileHandle++;fileHandles.emplace(handle,descriptor);lastError=(disposition==2||disposition==4)&&existed?183:0;ret(handle,7);
}
void readFile(){
    auto file=fileHandles.find(arg(0));if(file==fileHandles.end()){lastError=6;ret(0,5);return;}if(arg(4))throw GuestStop("Overlapped ReadFile pending");
    if(arg(3))write32(arg(3),0);ssize_t count;do{count=::read(file->second,ptr(arg(1),arg(2)),arg(2));}while(count<0&&errno==EINTR);
    if(count<0){lastError=fileError();ret(0,5);return;}if(arg(3))write32(arg(3),count);ret(1,5);
}
void fileSize(){auto file=fileHandles.find(arg(0));struct stat status{};if(file==fileHandles.end()){lastError=6;ret(UINT32_MAX,2);return;}if(fstat(file->second,&status)){lastError=fileError();ret(UINT32_MAX,2);return;}uint64_t size=status.st_size;if(arg(1))write32(arg(1),size>>32);lastError=0;ret(size,2);}
void fileAttributes(){auto path=gamePath(guestString(arg(0)));struct stat status{};if(stat(path.c_str(),&status)){lastError=fileError();ret(UINT32_MAX,1);return;}ret(S_ISDIR(status.st_mode)?0x10:0x20,1);}
void setFilePointer(){
    auto file=fileHandles.find(arg(0));if(file==fileHandles.end()){lastError=6;ret(UINT32_MAX,4);return;}
    uint32_t method=arg(3),high=arg(2);if(method>2){lastError=87;ret(UINT32_MAX,4);return;}
    int64_t distance=int32_t(arg(1));if(high){uint64_t bits=(uint64_t(read32(high))<<32)|arg(1);std::memcpy(&distance,&bits,8);}
    off_t result=::lseek(file->second,off_t(distance),method==0?SEEK_SET:method==1?SEEK_CUR:SEEK_END);
    if(result<0){lastError=errno==EINVAL?131:fileError();ret(UINT32_MAX,4);return;}
    if(!high&&uint64_t(result)>UINT32_MAX){lastError=87;ret(UINT32_MAX,4);return;}
    if(high)write32(high,uint64_t(result)>>32);lastError=0;ret(uint32_t(result),4);
}
const std::map<std::string,recomp_func_t> fileFunctions={
    {"SetFilePointer",setFilePointer},
    {"GetLogicalDrives",logicalDrives},{"GetDriveTypeA",driveType},
    {"GetLongPathNameA",longPath},{"CreateFileA",createFile},{"ReadFile",readFile},{"GetFileSize",fileSize},{"GetFileAttributesA",fileAttributes}
};
