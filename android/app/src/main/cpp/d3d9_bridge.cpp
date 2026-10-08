#include "d3d9_bridge.h"
#include "runtime_log.h"
#include "dxt_shadow.h"
#include <d3d9.h>
#include <map>
#include <cstring>
#include <stdexcept>
#ifdef __ANDROID__
#include <android/native_window.h>
#endif
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>

extern "C" IDirect3D9* Direct3DCreate9(UINT sdk);
extern "C" void dxvkAndroidSetWindow(ANativeWindow* window);
extern "C" HWND dxvkAndroidGetWindowHandle();
extern "C" void dxvkAndroidSetRenderSize(uint32_t width,uint32_t height);
void setD3D9RenderSize(unsigned width,unsigned height){dxvkAndroidSetRenderSize(width,height);}
#ifndef __ANDROID__
extern "C" ANativeWindow* dxvkCreateTestWindow();
extern "C" void dxvkDestroyTestWindow(ANativeWindow* window);
#endif
namespace {
constexpr uint32_t methodBase=0x7e000000;
enum class Kind:uint32_t {D3D9=1,Device=2,Texture=3,Cube=4,Surface=5,VertexBuffer=6,IndexBuffer=7,
    VertexShader=8,PixelShader=9,Declaration=10,StateBlock=11,Query=12,SwapChain=13,VolumeTexture=14,Volume=15};
struct Object {IUnknown* native;Kind kind;uint32_t references=1;CompressedView compressed;};
std::map<uint32_t,Object> objects;
std::map<IUnknown*,uint32_t> addresses;
void* guestBase;
uint32_t (*allocateGuest)(uint32_t);
void (*freeGuest)(uint32_t);
std::atomic<ANativeWindow*> hostWindow{nullptr};
std::atomic<bool> surfaceRequested{false},surfaceAvailable{false};
void* pointer(uint32_t address){return address?static_cast<uint8_t*>(guestBase)+address:nullptr;}
void write(uint32_t address,uint32_t value){if(!address)throw std::runtime_error("Null D3D9 output");std::memcpy(pointer(address),&value,4);}
uint32_t wrap(IUnknown* native,Kind kind){
    if(!native)return 0;auto previous=addresses.find(native);if(previous!=addresses.end()){++objects.at(previous->second).references;return previous->second;}
    static constexpr uint32_t sizes[]={0,17,119,22,22,17,14,14,5,5,5,6,8,10,22,11};
    uint32_t count=sizes[static_cast<uint32_t>(kind)];
    uint32_t table=allocateGuest(count*4),object=allocateGuest(4);
    for(uint32_t i=0;i<count;++i)write(table+i*4,methodBase+static_cast<uint32_t>(kind)*4096+i*16);
    write(object,table);objects.emplace(object,Object{native,kind});addresses.emplace(native,object);return object;
}
template<class T>T* nativeObject(uint32_t address){if(!address)return nullptr;auto found=objects.find(address);if(found==objects.end())throw std::runtime_error("Invalid D3D9 resource handle");auto native=dynamic_cast<T*>(found->second.native);if(!native)throw std::runtime_error("D3D9 resource interface mismatch");return native;}
uint32_t wrapTexture(IDirect3DBaseTexture9* texture){if(!texture)return 0;switch(texture->GetType()){case D3DRTYPE_TEXTURE:return wrap(texture,Kind::Texture);case D3DRTYPE_CUBETEXTURE:return wrap(texture,Kind::Cube);case D3DRTYPE_VOLUMETEXTURE:return wrap(texture,Kind::VolumeTexture);default:throw std::runtime_error("Unknown D3D9 base texture type");}}
float floatValue(uint32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
uint32_t floatBits(float value){uint32_t bits;std::memcpy(&bits,&value,4);return bits;}
CompressedView compressionView(uint32_t address){auto found=objects.find(address);return found==objects.end()?CompressedView{}:found->second.compressed;}
D3DPRESENT_PARAMETERS presentation(uint32_t address){
    auto p=static_cast<const uint32_t*>(pointer(address));if(!p)throw std::runtime_error("Null D3D9 presentation parameters");
    D3DPRESENT_PARAMETERS result{};result.BackBufferWidth=p[0];result.BackBufferHeight=p[1];result.BackBufferFormat=static_cast<D3DFORMAT>(p[2]);result.BackBufferCount=p[3];
    result.MultiSampleType=static_cast<D3DMULTISAMPLE_TYPE>(p[4]);result.MultiSampleQuality=p[5];result.SwapEffect=static_cast<D3DSWAPEFFECT>(p[6]);result.hDeviceWindow=dxvkAndroidGetWindowHandle();
    result.Windowed=p[8];result.EnableAutoDepthStencil=p[9];result.AutoDepthStencilFormat=static_cast<D3DFORMAT>(p[10]);result.Flags=p[11];result.FullScreen_RefreshRateInHz=p[12];result.PresentationInterval=p[13];return result;
}
#include "d3d9_locks.inc"
bool compressedFormat(D3DFORMAT format){return format>=D3DFMT_DXT1&&format<=D3DFMT_DXT5;}
D3DFORMAT nativeTextureFormat(IDirect3DDevice9* device,D3DFORMAT format,DWORD usage,D3DRESOURCETYPE type){
    if(!compressedFormat(format)||(usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return format;
    IDirect3D9* d3d=nullptr;D3DDEVICE_CREATION_PARAMETERS parameters{};D3DDISPLAYMODE mode{};
    if(FAILED(device->GetDirect3D(&d3d)))return format;
    device->GetCreationParameters(&parameters);d3d->GetAdapterDisplayMode(parameters.AdapterOrdinal,&mode);
    HRESULT supported=d3d->CheckDeviceFormat(parameters.AdapterOrdinal,parameters.DeviceType,mode.Format,usage,type,format);d3d->Release();
    return SUCCEEDED(supported)?format:D3DFMT_A8B8G8R8;
}
void describeOriginal(uint32_t address,D3DSURFACE_DESC* desc){auto view=compressionView(address);if(view.texture&&desc)desc->Format=static_cast<D3DFORMAT>(view.texture->format);}
}
void configureD3D9Bridge(void* base,uint32_t (*allocator)(uint32_t),void (*deallocator)(uint32_t)){
    guestBase=base;allocateGuest=allocator;freeGuest=deallocator;
#ifdef __ANDROID__
    // Shader caches are optional; the Android process has no writable cwd.
    setenv("DXVK_STATE_CACHE","0",1);
    setenv("DXVK_LOG_PATH","none",1);
#endif
#ifndef __ANDROID__
    if(std::getenv("NFS_D3D9_TEST_DISPLAY")){auto window=dxvkCreateTestWindow();if(!window)throw std::runtime_error("Xlib test display unavailable");setD3D9HostWindow(window);acknowledgeD3D9Surface(true);}
#endif
}
void shutdownD3D9Bridge(){
    for(auto& lock:resourceLocks)freeGuest(lock.second.guest);resourceLocks.clear();
    for(uint32_t kind=15;kind;--kind)for(auto it=objects.begin();it!=objects.end();){
        if(static_cast<uint32_t>(it->second.kind)!=kind){++it;continue;}
        auto object=it->second;for(uint32_t n=0;n<object.references;++n)object.native->Release();
        uint32_t table;std::memcpy(&table,pointer(it->first),4);freeGuest(table);freeGuest(it->first);addresses.erase(object.native);it=objects.erase(it);
    }
    surfaceRequested=false;surfaceAvailable=false;
#ifndef __ANDROID__
    if(std::getenv("NFS_D3D9_TEST_DISPLAY")){auto window=hostWindow.load();setD3D9HostWindow(nullptr);dxvkDestroyTestWindow(window);}
#endif
}
void setD3D9HostWindow(ANativeWindow* window){hostWindow=window;dxvkAndroidSetWindow(window);}
bool d3d9RequestsSurface(){return surfaceRequested;}
void acknowledgeD3D9Surface(bool available){surfaceAvailable=available;}
uint32_t createGuestD3D9(uint32_t sdk){
    auto native=Direct3DCreate9(sdk);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","DXVK native Direct3DCreate9 sdk=%u",sdk);
    return wrap(native,Kind::D3D9);
}
uint32_t dispatchGuestD3D9(uint32_t token,const uint32_t* args,uint32_t* argumentCount){
    uint32_t kind=(token-methodBase)/4096,method=((token-methodBase)%4096)/16;
    auto found=objects.find(args[0]);
    if(found==objects.end()||static_cast<uint32_t>(found->second.kind)!=kind){
        NFS_RUNTIME_LOG(ANDROID_LOG_ERROR,"NFSU2","Invalid D3D9 COM call object=%08x token=%08x kind=%u method=%u actualKind=%u",args[0],token,kind,method,
            found==objects.end()?0:static_cast<uint32_t>(found->second.kind));
        throw std::runtime_error("Invalid guest D3D9 COM object");
    }
    auto object=found->second.native;
    // Android may replace ANativeWindow after backgrounding, even at the same
    // pointer address. Override the destination with this surface generation so
    // DXVK recreates its presenter while keeping the game's device/resources.
    if((kind==2&&method==17)||(kind==13&&method==3)){
        *argumentCount=kind==2?5:6;auto window=dxvkAndroidGetWindowHandle();
        if(!window)return D3DERR_DEVICELOST;
        auto source=static_cast<const RECT*>(pointer(args[1])),destination=static_cast<const RECT*>(pointer(args[2]));
        auto dirty=static_cast<const RGNDATA*>(pointer(args[4]));
        return kind==2?static_cast<IDirect3DDevice9*>(object)->Present(source,destination,window,dirty):
            static_cast<IDirect3DSwapChain9*>(object)->Present(source,destination,window,dirty,args[5]);
    }
    static std::map<uint32_t,uint64_t> methodCalls;auto calls=++methodCalls[token];
    if(calls<=3)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","D3D9 COM kind=%u method=%u",kind,method);
    if(method==1){*argumentCount=1;++found->second.references;return object->AddRef();}
    if(method==2){*argumentCount=1;uint32_t references=object->Release();if(!--found->second.references){uint32_t table;std::memcpy(&table,pointer(args[0]),4);freeGuest(table);freeGuest(args[0]);addresses.erase(object);objects.erase(found);}return references;}
    if(method==0){
        *argumentCount=3;IUnknown* result=nullptr;HRESULT status=object->QueryInterface(*static_cast<const GUID*>(pointer(args[1])),reinterpret_cast<void**>(&result));
        if(SUCCEEDED(status))write(args[2],wrap(result,found->second.kind));else write(args[2],0);return status;
    }
    if(kind==1){
        auto d3d=static_cast<IDirect3D9*>(object);
        switch(method){
        case 3:*argumentCount=2;return D3DERR_NOTAVAILABLE;
        case 4:*argumentCount=1;return d3d->GetAdapterCount();
        case 5:*argumentCount=4;return d3d->GetAdapterIdentifier(args[1],args[2],static_cast<D3DADAPTER_IDENTIFIER9*>(pointer(args[3])));
        case 6:*argumentCount=3;return d3d->GetAdapterModeCount(args[1],static_cast<D3DFORMAT>(args[2]));
        case 7:*argumentCount=5;return d3d->EnumAdapterModes(args[1],static_cast<D3DFORMAT>(args[2]),args[3],static_cast<D3DDISPLAYMODE*>(pointer(args[4])));
        case 8:*argumentCount=3;return d3d->GetAdapterDisplayMode(args[1],static_cast<D3DDISPLAYMODE*>(pointer(args[2])));
        case 9:*argumentCount=6;return d3d->CheckDeviceType(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),static_cast<D3DFORMAT>(args[4]),args[5]);
        case 10:{*argumentCount=7;auto format=static_cast<D3DFORMAT>(args[6]);auto type=static_cast<D3DRESOURCETYPE>(args[5]);
            HRESULT status=d3d->CheckDeviceFormat(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),args[4],type,format);
            if(FAILED(status)&&compressedFormat(format)&&!(args[4]&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))&&(type==D3DRTYPE_TEXTURE||type==D3DRTYPE_CUBETEXTURE||type==D3DRTYPE_SURFACE))
                status=d3d->CheckDeviceFormat(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),args[4],type,D3DFMT_A8B8G8R8);
            return status;}
        case 11:*argumentCount=7;return d3d->CheckDeviceMultiSampleType(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),args[4],static_cast<D3DMULTISAMPLE_TYPE>(args[5]),static_cast<DWORD*>(pointer(args[6])));
        case 12:*argumentCount=6;return d3d->CheckDepthStencilMatch(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),static_cast<D3DFORMAT>(args[4]),static_cast<D3DFORMAT>(args[5]));
        case 13:*argumentCount=5;return d3d->CheckDeviceFormatConversion(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DFORMAT>(args[3]),static_cast<D3DFORMAT>(args[4]));
        case 14:*argumentCount=4;return d3d->GetDeviceCaps(args[1],static_cast<D3DDEVTYPE>(args[2]),static_cast<D3DCAPS9*>(pointer(args[3])));
        case 15:*argumentCount=2;return d3d->GetAdapterMonitor(args[1])?0xf0000300:0;
        case 16:{
            *argumentCount=7;auto p=static_cast<const uint32_t*>(pointer(args[5]));if(!p||!args[6])return D3DERR_INVALIDCALL;
            if(!hostWindow.load())throw std::runtime_error("D3D9 CreateDevice requires Android surface");
            surfaceRequested=true;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!surfaceAvailable){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Android surface handover timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            auto window=dxvkAndroidGetWindowHandle();if(!window)return D3DERR_DEVICELOST;
            D3DPRESENT_PARAMETERS parameters{};parameters.BackBufferWidth=p[0];parameters.BackBufferHeight=p[1];parameters.BackBufferFormat=static_cast<D3DFORMAT>(p[2]);parameters.BackBufferCount=p[3];
            parameters.MultiSampleType=static_cast<D3DMULTISAMPLE_TYPE>(p[4]);parameters.MultiSampleQuality=p[5];parameters.SwapEffect=static_cast<D3DSWAPEFFECT>(p[6]);parameters.hDeviceWindow=window;
            parameters.Windowed=p[8];parameters.EnableAutoDepthStencil=p[9];parameters.AutoDepthStencilFormat=static_cast<D3DFORMAT>(p[10]);parameters.Flags=p[11];parameters.FullScreen_RefreshRateInHz=p[12];parameters.PresentationInterval=p[13];
            IDirect3DDevice9* device=nullptr;HRESULT status=d3d->CreateDevice(args[1],static_cast<D3DDEVTYPE>(args[2]),window,args[4],&parameters,&device);
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","D3D9 backbuffer %ux%u CreateDevice result=%08x",parameters.BackBufferWidth,parameters.BackBufferHeight,uint32_t(status));
            write(args[6],SUCCEEDED(status)?wrap(device,Kind::Device):0);return status;
        }
        }
    }
    if(kind==2){auto device=static_cast<IDirect3DDevice9*>(object);switch(method){
        case 9:{*argumentCount=2;D3DDEVICE_CREATION_PARAMETERS p{};HRESULT status=device->GetCreationParameters(&p);if(SUCCEEDED(status)){write(args[1],p.AdapterOrdinal);write(args[1]+4,p.DeviceType);write(args[1]+8,0xf0000301);write(args[1]+12,p.BehaviorFlags);}return status;}
        case 13:{*argumentCount=3;auto p=presentation(args[1]);IDirect3DSwapChain9* result=nullptr;HRESULT status=device->CreateAdditionalSwapChain(&p,&result);write(args[2],SUCCEEDED(status)?wrap(result,Kind::SwapChain):0);return status;}
        case 16:{*argumentCount=2;auto p=presentation(args[1]);return device->Reset(&p);}
        case 23:{*argumentCount=9;if(args[8])throw std::runtime_error("Shared texture handle pending");if(!args[7])return D3DERR_INVALIDCALL;
            auto original=static_cast<D3DFORMAT>(args[5]),format=nativeTextureFormat(device,original,args[4],D3DRTYPE_TEXTURE);IDirect3DTexture9* texture=nullptr;
            HRESULT status=device->CreateTexture(args[1],args[2],args[3],args[4],format,static_cast<D3DPOOL>(args[6]),&texture,nullptr);
            uint32_t address=SUCCEEDED(status)?wrap(texture,Kind::Texture):0;
            if(address&&format!=original){objects.at(address).compressed.texture=std::make_shared<CompressedTexture>(original,args[1],args[2],texture->GetLevelCount(),1);NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSU2","CPU DXT fallback texture %ux%u format=%u",args[1],args[2],original);}
            write(args[7],address);return status;}
        case 25:{*argumentCount=8;if(args[7])throw std::runtime_error("Shared cube handle pending");if(!args[6])return D3DERR_INVALIDCALL;
            auto original=static_cast<D3DFORMAT>(args[4]),format=nativeTextureFormat(device,original,args[3],D3DRTYPE_CUBETEXTURE);IDirect3DCubeTexture9* texture=nullptr;
            HRESULT status=device->CreateCubeTexture(args[1],args[2],args[3],format,static_cast<D3DPOOL>(args[5]),&texture,nullptr);
            uint32_t address=SUCCEEDED(status)?wrap(texture,Kind::Cube):0;
            if(address&&format!=original)objects.at(address).compressed.texture=std::make_shared<CompressedTexture>(original,args[1],args[1],texture->GetLevelCount(),6);
            write(args[6],address);return status;}
#include "d3d9_device_methods.inc"
    }}
    if(kind==3){auto texture=static_cast<IDirect3DTexture9*>(object);
        if(method==17){*argumentCount=3;auto desc=static_cast<D3DSURFACE_DESC*>(pointer(args[2]));HRESULT status=texture->GetLevelDesc(args[1],desc);if(SUCCEEDED(status))describeOriginal(args[0],desc);return status;}
        if(method==18){*argumentCount=3;IDirect3DSurface9* surface=nullptr;HRESULT status=texture->GetSurfaceLevel(args[1],&surface);uint32_t address=SUCCEEDED(status)?wrap(surface,Kind::Surface):0;if(address){auto view=compressionView(args[0]);view.level=args[1];objects.at(address).compressed=view;}write(args[2],address);return status;}
        if(method==19){*argumentCount=5;return lockTexture(args[0],args[1],args[2],static_cast<const RECT*>(pointer(args[3])),args[4]);}
        if(method==20){*argumentCount=2;copyLockBack(args[0],args[1]);HRESULT status=texture->UnlockRect(args[1]);releaseLock(args[0],args[1]);return status;}}
    if(kind==4){auto texture=static_cast<IDirect3DCubeTexture9*>(object);
        if(method==17){*argumentCount=3;auto desc=static_cast<D3DSURFACE_DESC*>(pointer(args[2]));HRESULT status=texture->GetLevelDesc(args[1],desc);if(SUCCEEDED(status))describeOriginal(args[0],desc);return status;}
        if(method==18){*argumentCount=4;IDirect3DSurface9* surface=nullptr;HRESULT status=texture->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(args[1]),args[2],&surface);uint32_t address=SUCCEEDED(status)?wrap(surface,Kind::Surface):0;if(address){auto view=compressionView(args[0]);view.face=args[1];view.level=args[2];objects.at(address).compressed=view;}write(args[3],address);return status;}
        if(method==19){*argumentCount=6;return lockCube(args[0],args[1],args[2],args[3],static_cast<const RECT*>(pointer(args[4])),args[5]);}
        if(method==20){*argumentCount=3;uint32_t subresource=args[1]*32+args[2];copyLockBack(args[0],subresource);HRESULT status=texture->UnlockRect(static_cast<D3DCUBEMAP_FACES>(args[1]),args[2]);releaseLock(args[0],subresource);return status;}}
    if(kind==5){auto surface=static_cast<IDirect3DSurface9*>(object);
        if(method==12){*argumentCount=2;auto desc=static_cast<D3DSURFACE_DESC*>(pointer(args[1]));HRESULT status=surface->GetDesc(desc);if(SUCCEEDED(status))describeOriginal(args[0],desc);return status;}
        if(method==13){*argumentCount=4;return lockSurface(args[0],args[1],static_cast<const RECT*>(pointer(args[2])),args[3]);}
        if(method==14){*argumentCount=1;copyLockBack(args[0],0);HRESULT status=surface->UnlockRect();releaseLock(args[0],0);return status;}}
    if(kind==6||kind==7){
        if(method==11){*argumentCount=5;return kind==6?lockBuffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>(args[0],args[1],args[2],args[3],args[4]):lockBuffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>(args[0],args[1],args[2],args[3],args[4]);}
        if(method==12){*argumentCount=1;copyLockBack(args[0],0);HRESULT status=kind==6?static_cast<IDirect3DVertexBuffer9*>(object)->Unlock():static_cast<IDirect3DIndexBuffer9*>(object)->Unlock();releaseLock(args[0],0);return status;}}
#include "d3d9_resource_methods.inc"
    throw std::runtime_error("D3D9 guest method pending: kind="+std::to_string(kind)+" slot="+std::to_string(method));
}
