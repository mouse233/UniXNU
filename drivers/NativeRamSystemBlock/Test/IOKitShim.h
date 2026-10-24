// Host dependency substitutes; the transport implementation is included unchanged.
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <cerrno>
using UInt64=uint64_t;
using UInt32=uint32_t;
using IOOptionBits=unsigned;
using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnNotWritable=2,
    kIOReturnNotReady=3,kIOReturnBadArgument=4,kIOReturnIOError=5;
constexpr unsigned kIODirectionIn=1,kIODirectionOut=2;
struct OSObject { virtual ~OSObject()=default; virtual void release() {} };
struct OSBoolean : OSObject {};
inline OSBoolean no,yes;
inline OSObject *kOSBooleanFalse=&no,*kOSBooleanTrue=&yes;
struct OSDictionary : OSObject {};
struct OSString : OSObject {
    std::string value;
    explicit OSString(const char *s):value(s){}
    size_t getLength() { return value.size(); }
    const char *getCStringNoCopy() { return value.c_str(); }
};
struct OSNumber : OSObject {
    uint64_t value;
    explicit OSNumber(uint64_t v):value(v){}
    uint64_t unsigned64BitValue() { return value; }
    unsigned unsigned32BitValue() { return value; }
};
inline std::vector<OSObject *> registry;
inline unsigned slept=0,registered=0;
inline bool iteratorValid=true,bootEnabled=true,attachOK=true,startOK=true;
struct OSIterator : OSObject {
    size_t at=0;
    OSObject *getNextObject() { return at<registry.size()?registry[at++]:nullptr; }
    bool isValid() { return iteratorValid; }
    void release() override { delete this; }
};
struct IOService : OSObject {
    IOService *provider=nullptr;
    bool isVolume=false;
    std::map<std::string,OSObject *> properties;
    virtual bool init(OSDictionary *) { return true; }
    virtual bool start(IOService *) { return startOK; }
    virtual void stop(IOService *) {}
    bool attach(IOService *p) { provider=p;return attachOK; }
    void detach(IOService *) { provider=nullptr; }
    IOService *getProvider() { return provider; }
    const char *getName() { return "mock media"; }
    OSObject *getProperty(const char *s) { return properties[s]; }
    void *metaCast(const char *s) { assert(!strcmp(s,"AppleAPFSVolume"));return isVolume?this:nullptr; }
    void registerService() { registered++; }
    static IOService *getResourceService() { static IOService resource;return &resource; }
    static OSDictionary *serviceMatching(const char *s) { assert(!strcmp(s,"IOMedia"));static OSDictionary d;return &d; }
    static OSIterator *getMatchingServices(OSDictionary *) { return new OSIterator; }
};
struct IOMemoryDescriptor {
    unsigned direction=kIODirectionIn;
    uint64_t length=512,limit=512,actual=0;
    uint8_t output[1024]={};
    unsigned getDirection() { return direction; }
    uint64_t getLength() { return length; }
    uint64_t writeBytes(uint64_t at,const void *src,uint64_t n) {
        assert(at==0); actual=n<limit?n:limit;
        assert(actual<=sizeof(output));memcpy(output,src,actual);return actual;
    }
};
struct IOStorageAttributes {};
struct IOStorageCompletion {
    void *target,*parameter;
    void (*action)(void *,void *,IOReturn,UInt64);
};
struct IOBlockStorageDevice : IOService {
    virtual IOReturn doEjectMedia()=0;
    virtual IOReturn doFormatMedia(UInt64)=0;
    virtual UInt32 doGetFormatCapacities(UInt64 *,UInt32) const=0;
    virtual char *getVendorString()=0;
    virtual char *getProductString()=0;
    virtual char *getRevisionString()=0;
    virtual char *getAdditionalDeviceInfoString()=0;
    virtual IOReturn reportBlockSize(UInt64 *)=0;
    virtual IOReturn reportEjectability(bool *)=0;
    virtual IOReturn reportMaxValidBlock(UInt64 *)=0;
    virtual IOReturn reportMediaState(bool *,bool *)=0;
    virtual IOReturn reportRemovability(bool *)=0;
    virtual IOReturn reportWriteProtection(bool *)=0;
    virtual IOReturn doSynchronizeCache()=0;
    virtual IOReturn doSynchronize(UInt64,UInt64,IOOptionBits)=0;
    virtual IOReturn getWriteCacheState(bool *)=0;
    virtual IOReturn setWriteCacheState(bool)=0;
    virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *,UInt64,UInt64,IOStorageAttributes *,IOStorageCompletion *)=0;
};
#define OSDeclareDefaultStructors(c)
#define OSDefineMetaClassAndStructors(c,b)
#define OSDynamicCast(c,o) dynamic_cast<c *>(o)
#define IOLog(...) ((void)0)
#define kIOBSDNameKey "BSD Name"
#define kIOBSDMajorKey "BSD Major"
#define kIOBSDMinorKey "BSD Minor"
#define makedev(a,b) (((a)<<24)|(b))
inline uintptr_t ml_static_ptovirt(uint64_t p) { return p; }
inline bool PE_parse_boot_argn(const char *,unsigned *p,size_t) { *p=bootEnabled?1:0;return true; }
inline void IOSleep(unsigned n) { slept+=n; }
