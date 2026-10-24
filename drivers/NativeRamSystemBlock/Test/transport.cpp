#include "IOKitShim.h"
#include <sys/mman.h>
#include "../NativeRamSystemBlock.cpp"
extern "C" { unsigned long gPhysBase=0,gPhysSize=0; }
static unsigned completions=0;
static IOReturn lastStatus=0;
static UInt64 lastActual=0;
static void completed(void *,void *,IOReturn status,UInt64 actual) {
    completions++;lastStatus=status;lastActual=actual;
}
int main() {
    const uint64_t bytes=6077546496; // Real OS slice is larger than 4 GiB.
    uint8_t *ram=static_cast<uint8_t *>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(ram!=MAP_FAILED);
    memset(ram,0x31,512);memset(ram+bytes-512,0x79,512);
    gPhysBase=reinterpret_cast<uintptr_t>(ram);gPhysSize=bytes;
    NativeRamSystemBlock disk;
    IOMemoryDescriptor buffer;
    IOStorageCompletion completion={nullptr,nullptr,completed};
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==kIOReturnNotReady);
    bootEnabled=false;assert(!disk.configure(1,gPhysBase,bytes));bootEnabled=true;
    assert(!disk.configure(0,gPhysBase,bytes));assert(!disk.configure(3,gPhysBase,bytes));
    { NativeRamSystemBlock profile2; bootProfile=2;
      assert(profile2.configure(1,gPhysBase,bytes)); }
    // Refusal must be tested on a fresh object, otherwise the already-configured
    // guard would mask a broken profile gate.
    { NativeRamSystemBlock profile3; bootProfile=3;
      assert(!profile3.configure(1,gPhysBase,bytes)); }
    bootProfile=1;
    assert(!disk.configure(1,gPhysBase-512,bytes));assert(!disk.configure(1,gPhysBase+512,bytes));
    assert(!disk.configure(1,gPhysBase,bytes+512));assert(!disk.configure(1,gPhysBase,bytes-1));
    assert(!disk.configure(1,gPhysBase+1,bytes-512));assert(!disk.configure(1,gPhysBase,0));
    assert(disk.configure(1,gPhysBase,bytes));assert(!disk.configure(1,gPhysBase,bytes));
    assert(disk.start(IOService::getResourceService()));
    bool flag=false;UInt64 value=0;
    assert(disk.reportWriteProtection(&flag)==0 && flag);
    assert(disk.reportMaxValidBlock(&value)==0 && value==bytes/512-1);
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==0);
    assert(completions==1 && lastStatus==0 && lastActual==512 && buffer.output[0]==0x31);
    assert(disk.doAsyncReadWrite(&buffer,bytes/512-1,1,nullptr,&completion)==0);
    assert(completions==2 && lastActual==512 && buffer.output[511]==0x79);
    buffer.limit=17;
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==0);
    assert(completions==3 && lastStatus==kIOReturnIOError && lastActual==17);
    for (unsigned direction : {0u,kIODirectionOut,kIODirectionIn|kIODirectionOut}) {
        buffer.direction=direction;
        assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==kIOReturnNotWritable);
        assert(completions==3 && ram[0]==0x31);
    }
    buffer.direction=kIODirectionIn;
    for (auto request : std::vector<std::pair<uint64_t,uint64_t>>{{0,0},{bytes/512,1},{bytes/512-1,2},{UINT64_MAX,1},{0,UINT64_MAX}}) {
        assert(disk.doAsyncReadWrite(&buffer,request.first,request.second,nullptr,&completion)==kIOReturnBadArgument);
        assert(completions==3);
    }
    buffer.length=511;
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==kIOReturnBadArgument);
    buffer.length=512;
    assert(disk.doAsyncReadWrite(nullptr,0,1,nullptr,&completion)==kIOReturnBadArgument);
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,nullptr)==kIOReturnBadArgument);
    assert(disk.doFormatMedia(0)==kIOReturnNotWritable && disk.setWriteCacheState(true)==kIOReturnNotWritable);
    disk.stop(IOService::getResourceService());
    assert(disk.doAsyncReadWrite(&buffer,0,1,nullptr,&completion)==kIOReturnNotReady);
    assert(fuxi_ram_system_publish(0,gPhysBase,bytes)==EINVAL);
    assert(fuxi_ram_system_publish(1,gPhysBase,bytes)==0);
    assert(fuxi_ram_system_publish(1,gPhysBase,bytes)==EINVAL);
    IOService container,volume,foreign,impostor;
    container.provider=devices[0];volume.provider=&container;volume.isVolume=true;
    OSString name("disk12s1"),uuid("12345678-1234-1234-1234-123456789ABC");
    OSNumber major(14),minor(9);
    volume.properties={{kIOBSDNameKey,&name},{kIOBSDMajorKey,&major},{kIOBSDMinorKey,&minor},{"UUID",&uuid},{"Writable",kOSBooleanFalse}};
    char path[64]={};
    assert(fuxi_ram_system_volume(2,path,sizeof(path))==EINVAL);
    registry={&volume};assert(fuxi_ram_system_volume(1,path,sizeof(path))==0 && !strcmp(path,"/dev/disk12s1"));
    volume.provider=&foreign;assert(findVolume(1,path,sizeof(path))==ENOENT);volume.provider=&container;
    volume.isVolume=false;assert(findVolume(1,path,sizeof(path))==ENOENT);volume.isVolume=true;
    volume.properties["Writable"]=kOSBooleanTrue;assert(findVolume(1,path,sizeof(path))==ENOENT);
    volume.properties["Writable"]=kOSBooleanFalse;
    uuid.value="malformed";assert(findVolume(1,path,sizeof(path))==ENOENT);uuid.value="12345678-1234-1234-1234-123456789ABC";
    for (const char *bad : {"","d","disk","diskXs1","disk1s0","disk1s2","disk1s1s1","disk1s1/../../disk0","md1s1"}) {
        name.value=bad;assert(findVolume(1,path,sizeof(path))==ENOENT);
    }
    name.value="disk12s1";major.value=256;assert(findVolume(1,path,sizeof(path))==ENOENT);major.value=14;
    minor.value=0x1000000;assert(findVolume(1,path,sizeof(path))==ENOENT);minor.value=9;
    impostor=volume;registry={&volume,&impostor};assert(findVolume(1,path,sizeof(path))==EEXIST);
    registry={&volume};iteratorValid=false;assert(findVolume(1,path,sizeof(path))==EAGAIN);iteratorValid=true;
    registry.clear();assert(fuxi_ram_system_volume(1,path,sizeof(path))==ETIMEDOUT && slept==30000 && !path[0]);
    assert(completions==3 && ram[0]==0x31 && ram[bytes-1]==0x79);
    for (auto *device : devices) delete device;
    assert(munmap(ram,bytes)==0);
    puts("PASS: actual transport: >4GiB final-sector reads, short read, writes, overflow, completion, ownership, identity, ambiguity, timeout");
}
