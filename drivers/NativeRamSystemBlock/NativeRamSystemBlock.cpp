// SPDX-License-Identifier: BSD-2-Clause-Patent
// Dedicated mount-only transport. No allocation of disk contents, UFS, or store.
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/param.h>
#include "ReadOnlyWindow.h"

extern "C" { extern unsigned long gPhysBase, gPhysSize; }
static struct {
    uint64_t reads, lastBlock, rejectedWrites;
    unsigned media, volumes;
    char lastName[48];
} evidence[2];

class NativeRamSystemBlock : public IOBlockStorageDevice {
    OSDeclareDefaultStructors(NativeRamSystemBlock)
    const uint8_t *memory = nullptr;
    uint64_t bytes = 0;
    unsigned objectID = 0;
    bool online = false;
public:
    bool configure(unsigned id, uint64_t physical, uint64_t size) {
        unsigned enabled=0;
        if (!PE_parse_boot_argn("ram-system-probe", &enabled, sizeof(enabled)) || enabled!=1 ||
            memory || id<1 || id>2 || !size || (physical&511) || (size&511) ||
            physical<gPhysBase || size>gPhysSize || physical-gPhysBase>gPhysSize-size) return false;
        memory=reinterpret_cast<const uint8_t *>(ml_static_ptovirt(physical));
        bytes=size; objectID=id;
        return memory!=nullptr;
    }
    bool belongs(unsigned id) const { return __atomic_load_n(&online,__ATOMIC_ACQUIRE) && objectID==id; }
    bool start(IOService *provider) override {
        if (!memory || !bytes || !IOBlockStorageDevice::start(provider)) return false;
        __atomic_store_n(&online,true,__ATOMIC_RELEASE);
        IOLog("RAM_SYSTEM IOKit transport id=%u bytes=%llu readonly=1\n", objectID, bytes);
        registerService();
        return true;
    }
    void stop(IOService *provider) override {
        __atomic_store_n(&online,false,__ATOMIC_RELEASE);
        IOBlockStorageDevice::stop(provider);
    }
    IOReturn doEjectMedia() override { return kIOReturnUnsupported; }
    IOReturn doFormatMedia(UInt64) override { return kIOReturnNotWritable; }
    UInt32 doGetFormatCapacities(UInt64 *, UInt32) const override { return 0; }
    char *getVendorString() override { return const_cast<char *>("UniXNU"); }
    char *getProductString() override { return const_cast<char *>("Verified RAM SystemObjects"); }
    char *getRevisionString() override { return const_cast<char *>("1.0"); }
    char *getAdditionalDeviceInfoString() override { return const_cast<char *>("borrowed RAM; read only"); }
    IOReturn reportBlockSize(UInt64 *v) override { *v=512; return kIOReturnSuccess; }
    IOReturn reportEjectability(bool *v) override { *v=false; return kIOReturnSuccess; }
    IOReturn reportMaxValidBlock(UInt64 *v) override { *v=bytes/512-1; return kIOReturnSuccess; }
    IOReturn reportMediaState(bool *present, bool *changed) override {
        *present=__atomic_load_n(&online,__ATOMIC_ACQUIRE); if(changed)*changed=false; return kIOReturnSuccess;
    }
    IOReturn reportRemovability(bool *v) override { *v=false; return kIOReturnSuccess; }
    IOReturn reportWriteProtection(bool *v) override { *v=true; return kIOReturnSuccess; }
    IOReturn doSynchronizeCache() override { return __atomic_load_n(&online,__ATOMIC_ACQUIRE)?kIOReturnSuccess:kIOReturnNotReady; }
    IOReturn doSynchronize(UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache(); }
    IOReturn getWriteCacheState(bool *v) override { *v=false; return kIOReturnSuccess; }
    IOReturn setWriteCacheState(bool) override { return kIOReturnNotWritable; }
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
        IOStorageAttributes *, IOStorageCompletion *completion) override {
        if (!buffer || !completion || !completion->action) return kIOReturnBadArgument;
        if (!__atomic_load_n(&online,__ATOMIC_ACQUIRE)) return kIOReturnNotReady;
        struct Buffer {
            IOMemoryDescriptor *value;
            uint64_t length() { return value->getLength(); }
            uint64_t copy(const void *source, uint64_t length) { return value->writeBytes(0,source,length); }
        } adapter={buffer};
        uint64_t actual=0;
        const ram_system::Result r=ram_system::read(memory,bytes,block,count,
            buffer->getDirection()==kIODirectionIn,adapter,actual);
        if (r==ram_system::Result::BadArgument) return kIOReturnBadArgument;
        if (r==ram_system::Result::WriteProtected) {
            __atomic_fetch_add(&evidence[objectID-1].rejectedWrites,1,__ATOMIC_RELAXED);
            return kIOReturnNotWritable;
        }
        __atomic_fetch_add(&evidence[objectID-1].reads,1,__ATOMIC_RELAXED);
        __atomic_store_n(&evidence[objectID-1].lastBlock,block,__ATOMIC_RELAXED);
        completion->action(completion->target,completion->parameter,
            r==ram_system::Result::Success?kIOReturnSuccess:kIOReturnIOError,actual);
        return kIOReturnSuccess;
    }
};
OSDefineMetaClassAndStructors(NativeRamSystemBlock, IOBlockStorageDevice)

// The serial pre-userland probe owns these references until reboot. There is no
// personality or automatic publishing from arbitrary device-tree RAM entries.
static NativeRamSystemBlock *devices[2];
extern "C" int fuxi_ram_system_publish(unsigned id, uint64_t physical, uint64_t bytes) {
    if (id<1 || id>2 || devices[id-1]) return EINVAL;
    IOService *provider=IOService::getResourceService();
    NativeRamSystemBlock *disk=new NativeRamSystemBlock;
    if (!disk) return ENOMEM;
    if (!provider || !disk->init(nullptr) || !disk->configure(id,physical,bytes)) { disk->release(); return EINVAL; }
    if (!disk->attach(provider)) { disk->release(); return ENXIO; }
    // Publish ownership before start() schedules original storage matching.
    devices[id-1]=disk;
    if (!disk->start(provider)) {
        devices[id-1]=nullptr; disk->detach(provider); disk->release(); return ENXIO;
    }
    return 0;
}

static int findVolume(unsigned id, char *path, size_t size) {
    OSDictionary *matching=IOService::serviceMatching("IOMedia");
    if (!matching) return ENOMEM;
    OSIterator *iterator=IOService::getMatchingServices(matching);
    matching->release();
    if (!iterator) return ENOENT;
    unsigned found=0;
    char selected[64]={};
    evidence[id-1].media=evidence[id-1].volumes=0;
    while (OSObject *object=iterator->getNextObject()) {
        IOService *media=OSDynamicCast(IOService,object);
        if (!media) continue;
        bool owned=false;
        unsigned depth=0;
        for (IOService *parent=media; parent && depth++<64; parent=parent->getProvider()) {
            if (parent==devices[id-1] && devices[id-1]->belongs(id)) { owned=true; break; }
        }
        if (!owned) continue;
        evidence[id-1].media++;
        strlcpy(evidence[id-1].lastName,media->getName(),sizeof(evidence[id-1].lastName));
        if (!media->metaCast("AppleAPFSVolume")) continue;
        evidence[id-1].volumes++;
        OSString *name=OSDynamicCast(OSString,media->getProperty(kIOBSDNameKey));
        OSString *uuid=OSDynamicCast(OSString,media->getProperty("UUID"));
        OSNumber *major=OSDynamicCast(OSNumber,media->getProperty(kIOBSDMajorKey));
        OSNumber *minor=OSDynamicCast(OSNumber,media->getProperty(kIOBSDMinorKey));
        if (!name || name->getLength()>=sizeof(selected)-6 || !ram_system::volumeName(name->getCStringNoCopy()) ||
            !uuid || !ram_system::uuid(uuid->getCStringNoCopy(),uuid->getLength()) || !major || !minor ||
            major->unsigned64BitValue()>255 || minor->unsigned64BitValue()>0xffffff ||
            media->getProperty("Writable")!=kOSBooleanFalse) continue;
        if (++found>1) break;
        snprintf(selected,sizeof(selected),"/dev/%s",name->getCStringNoCopy());
        IOLog("RAM_SYSTEM IOKit volume id=%u path=%s dev=%x UUID=%s readonly=1\n",id,selected,
            static_cast<unsigned>(makedev(major->unsigned32BitValue(),minor->unsigned32BitValue())),uuid->getCStringNoCopy());
    }
    bool valid=iterator->isValid();
    iterator->release();
    if (!valid) return EAGAIN;
    if (found>1) return EEXIST;
    if (!found) return ENOENT;
    if (strlcpy(path,selected,size)>=size) return ENAMETOOLONG;
    return 0;
}
extern "C" int fuxi_ram_system_volume(unsigned id, char *path, size_t size) {
    if (!path || size<64 || id<1 || id>2 || !devices[id-1]) return EINVAL;
    path[0]=0;
    for (unsigned attempt=0;attempt<300;attempt++) {
        int error=findVolume(id,path,size);
        if (!error || (error!=ENOENT && error!=EAGAIN)) return error;
        IOSleep(100);
    }
    IOLog("RAM_SYSTEM IOKit volume timeout id=%u (30 seconds)\n",id);
    return ETIMEDOUT;
}
extern "C" const char *fuxi_ram_system_status(unsigned id) {
    static char text[224];
    if (id<1 || id>2) return "invalid RAM object";
    const auto &state=evidence[id-1];
    snprintf(text,sizeof(text),"IOKit id=%u reads=%llu last=%llu rejected-writes=%llu\nmedia=%u volumes=%u last-name=%s",
        id,__atomic_load_n(&state.reads,__ATOMIC_RELAXED),__atomic_load_n(&state.lastBlock,__ATOMIC_RELAXED),
        __atomic_load_n(&state.rejectedWrites,__ATOMIC_RELAXED),state.media,state.volumes,state.lastName);
    return text;
}
