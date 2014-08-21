//
// Copyright (c) 2009 Advanced Micro Devices, Inc. All rights reserved.
//

#ifndef GPU_HPP_
#define GPU_HPP_

#include "top.hpp"
#include "device/device.hpp"
#include "platform/command.hpp"
#include "platform/program.hpp"
#include "platform/perfctr.hpp"
#include "platform/threadtrace.hpp"
#include "platform/memory.hpp"
#include "utils/concurrent.hpp"
#include "thread/thread.hpp"
#include "thread/monitor.hpp"
#include "device/gpu/gpuvirtual.hpp"
#include "device/gpu/gpumemory.hpp"
#include "device/gpu/gpudefs.hpp"
#include "device/gpu/gpusettings.hpp"
#include "device/gpu/gpuappprofile.hpp"


#include "acl.h"
#include "vaminterface.h"

/*! \addtogroup GPU
 *  @{
 */

//! GPU Device Implementation
namespace gpu {

//! A nil device object
class NullDevice : public amd::Device
{
protected:
    static aclCompiler* compiler_;
    static aclCompiler* hsaCompiler_;
public:
    aclCompiler* compiler() const { return compiler_; }
    aclCompiler* hsaCompiler() const { return hsaCompiler_; }

public:
    static bool init(void);

    //! Construct a new identifier
    NullDevice();

    //! Creates an offline device with the specified target
    bool create(
        CALtarget target        //!< GPU device identifier
        );

    virtual cl_int createSubDevices(
        device::CreateSubDevicesInfo& create_info,
        cl_uint num_entries,
        cl_device_id* devices,
        cl_uint* num_devices) {
            return CL_INVALID_VALUE;
    }

    //! Instantiate a new virtual device
    virtual device::VirtualDevice* createVirtualDevice(
        bool    profiling,
        bool    interopQueue
#if cl_amd_open_video
        , void* calVideoProperties = NULL
#endif // cl_amd_open_video
        , uint  deviceQueueSize = 0
        ) { return NULL; }

    //! Compile the given source code.
    virtual device::Program* createProgram(int oclVer = 120);

    //! Just returns NULL for the dummy device
    virtual device::Memory* createMemory(amd::Memory& owner) const { return NULL; }

    //! Sampler object allocation
    virtual bool createSampler(
        const amd::Sampler& owner,  //!< abstraction layer sampler object
        device::Sampler**   sampler //!< device sampler object
        ) const
    {
        ShouldNotReachHere();
        return true;
    }

    //! Just returns NULL for the dummy device
    virtual device::Memory* createView(
        amd::Memory& owner,             //!< Owner memory object
        const device::Memory& parent    //!< Parent device memory object for the view
        ) const { return NULL; }

    //! Reallocates the provided buffer object
    virtual bool reallocMemory(amd::Memory& owner) const { return true; }

    //! Acquire external graphics API object in the host thread
    //! Needed for OpenGL objects on CPU device

    virtual bool bindExternalDevice(
        intptr_t type, void* pDevice, void* pContext, bool validateOnly) { return true; }

    virtual bool unbindExternalDevice(
        intptr_t type, void* pDevice, void* pContext, bool validateOnly) { return true; }

    //! Gets a pointer to a region of host-visible memory for use as the target
    //! of a non-blocking map for a given memory object
    virtual void* allocMapTarget(
        amd::Memory&    mem,        //!< Abstraction layer memory object
        const amd::Coord3D& origin, //!< The map location in memory
        const amd::Coord3D& region, //!< The map region in memory
        size_t* rowPitch = NULL,    //!< Row pitch for the mapped memory
        size_t* slicePitch = NULL   //!< Slice for the mapped memory
        ) { return NULL; }

    //! Releases non-blocking map target memory
    virtual void freeMapTarget(amd::Memory& mem, void* target) {}

    CALtarget calTarget() const { return calTarget_; }

    const AMDDeviceInfo* hwInfo() const { return hwInfo_; }

    //! Empty implementation on Null device
    virtual bool globalFreeMemory(size_t* freeMemory) const { return false; }

    //! Get GPU device settings
    const gpu::Settings& settings() const
        { return reinterpret_cast<gpu::Settings&>(*settings_); }
    virtual void* svmAlloc(amd::Context& context, size_t size, size_t alignment, cl_svm_mem_flags flags) const {return NULL;}
    virtual void svmFree(void* ptr) const {return;}

protected:
    CALtarget   calTarget_;         //!< GPU device identifier
    const AMDDeviceInfo* hwInfo_;   //!< Device HW info structure
};

//! Forward declarations
class Command;
class Device;
class GpuCommand;
class Heap;
class HeapBlock;
class Program;
class Kernel;
class Memory;
class Resource;
class VirtualDevice;
class PrintfDbg;
class ThreadTrace;

class Sampler : public device::Sampler
{
public:
    //! Constructor
    Sampler(const Device& dev): dev_(dev) {}

    //! Default destructor for the device memory object
    virtual ~Sampler();

    //! Creates a device sampler from the OCL sampler state
    bool create(
        uint32_t oclSamplerState    //!< OCL sampler state
        );

    const void* hwState() const { return hwState_; }

private:
    //! Disable default copy constructor
    Sampler& operator=(const Sampler&);

    //! Disable operator=
    Sampler(const Sampler&);

    const Device&   dev_;       //!< Device object associated with the sampler
    address         hwState_;   //!< GPU HW state (\todo legacy path)
};

//! A GPU device ordinal (physical GPU device)
class Device : public NullDevice, public CALGSLDevice
{
public:
    //! Locks any access to the virtual GPUs
    class ScopedLockVgpus : public amd::StackObject {
    public:
        //! Default constructor
        ScopedLockVgpus(const Device& dev);

        //! Destructor
        ~ScopedLockVgpus();

    private:
        const Device&   dev_;       //! Device object
    };

    //! Interop emulation flags
    enum InteropEmulationFlags
    {
        D3D10Device         = 0x00000001,
        GLContext           = 0x00000002,
    };

    class Engines : public amd::EmbeddedObject
    {
    public:
        //! Default constructor
        Engines() { memset(desc_, 0xff, sizeof(desc_)); }

        //! Creates engine descriptor for this class
        void create(uint num, gslEngineDescriptor* desc, uint maxNumComputeRings);

        //! Gets engine type mask
        uint getMask(gslEngineID id) const { return (1 << id); }

        //! Gets a descriptor for the requested engines
        uint getRequested(uint engines, gslEngineDescriptor* desc) const;

        //! Returns the number of available compute rings
        uint numComputeRings() const { return numComputeRings_; }

    private:
        uint numComputeRings_;
        gslEngineDescriptor desc_[GSL_ENGINEID_MAX];    //!< Engine descriptor
    };

    //! Transfer buffers
    class XferBuffers : public amd::HeapObject
    {
    public:
        static const size_t MaxXferBufListSize = 8;

        //! Default constructor
        XferBuffers(const Device& device, Resource::MemoryType type, size_t bufSize)
            : type_(type)
            , bufSize_(bufSize)
            , acquiredCnt_(0)
            , gpuDevice_(device)
            {}

        //! Default destructor
        ~XferBuffers();

        //! Creates the xfer buffers object
        bool create();

        //! Acquires an instance of the transfer buffers
        Resource& acquire();

        //! Releases transfer buffer
        void release(
            VirtualGPU& gpu,    //!< Virual GPU object used with the buffer
            Resource& buffer    //!< Transfer buffer for release
            );

        //! Returns the buffer's size for transfer
        size_t  bufSize() const { return bufSize_; }

    private:
        //! Disable copy constructor
        XferBuffers(const XferBuffers&);

        //! Disable assignment operator
        XferBuffers& operator=(const XferBuffers&);

        //! Get device object
        const Device& dev() const { return gpuDevice_; }

        Resource::MemoryType    type_;          //!< The buffer's type
        size_t                  bufSize_;       //!< Staged buffer size
        std::list<Resource*>    freeBuffers_;   //!< The list of free buffers
        amd::Atomic<uint>       acquiredCnt_;   //!< The total number of acquired buffers
        amd::Monitor            lock_;          //!< Stgaed buffer acquire/release lock
        const Device&           gpuDevice_;     //!< GPU device object
    };

    //! Virtual address cache entry
    struct VACacheEntry : public amd::HeapObject
    {
        void*   startAddress_;  //!< Start virtual address
        void*   endAddress_;    //!< End virtual address
        Memory* memory_;        //!< GPU memory, associated with the range

        //! Constructor
        VACacheEntry(
            void*   startAddress,   //!< Start virtual address
            void*   endAddress,     //!< End virtual address
            Memory* memory          //!< GPU memory object
            ): startAddress_(startAddress), endAddress_(endAddress), memory_(memory) {}

    private:
        //! Disable default constructor
        VACacheEntry();
    };

    struct ScratchBuffer : public amd::HeapObject
    {
        uint    regNum_;    //!< The number of used scratch registers
        std::vector<Memory*>   memObjs_;   //!< Memory objects for scratch buffers
        uint    offset_;    //!< Offset from the global scratch store
        uint    size_;      //!< Scratch buffer size on this queue

        //! Default constructor
        ScratchBuffer(uint numMems): regNum_(0), memObjs_(numMems), offset_(0) {}

        //! Default constructor
        ~ScratchBuffer();

        //! Destroys memory objects
        void destroyMemory();
    };


    class SrdManager : public amd::HeapObject {
    public:
        SrdManager(const Device& dev, uint srdSize, uint bufSize)
            : dev_(dev)
            , numFlags_(bufSize / (srdSize * MaskBits))
            , srdSize_(srdSize)
            , bufSize_(bufSize) {}
        ~SrdManager();

        //! Allocates a new SRD slot for a resource
        uint64_t allocSrdSlot(address* cpuAddr);

        //! Frees a SRD slot
        void freeSrdSlot(uint64_t addr);

        // Fills the resource list for VidMM KMD
        void fillResourceList(std::vector<const Resource*>&   memList);

    private:
        //! Disable copy constructor
        SrdManager(const SrdManager&);

        //! Disable assignment operator
        SrdManager& operator=(const SrdManager&);

        struct Chunk {
            Memory* buf_;
            uint*   flags_;
            Chunk(): buf_(NULL), flags_(NULL) {}
        };

        static const uint MaskBits = 32;
        const Device&   dev_;       //!< GPU device for the chunk manager
        amd::Monitor    ml_;        //!< Global lock for the SRD manager
        std::vector<Chunk>  pool_;  //!< Pool of SRD buffers
        uint            numFlags_;  //!< Total number of flags in array
        uint            srdSize_;   //!< SRD size
        uint            bufSize_;   //!< Buffer size that holds SRDs
    };

    //! Initialise the whole GPU device subsystem (CAL init, device enumeration, etc).
    static bool init();

    //! Shutdown the whole GPU device subsystem (CAL shutdown).
    static void tearDown();

    //! Construct a new physical GPU device
    Device();

    //! Initialise a device (i.e. all parts of the constructor that could
    //! potentially fail)
    bool create(
        CALuint ordinal     //!< GPU device ordinal index. Starts from 0
        );

    //! Destructor for the physical GPU device
    virtual ~Device();

    //! Reallocates current global heap
    bool reallocHeap(
        size_t  size,           //!< requested size for reallocation
        bool    remoteAlloc     //!< allocate the new heap in remote memory
        );

    //! Instantiate a new virtual device
    device::VirtualDevice* createVirtualDevice(
        bool    profiling,
        bool    interopQueue
#if cl_amd_open_video
        , void* calVideoProperties = NULL
#endif // cl_amd_open_video
        , uint  deviceQueueSize = 0
        );

    //! Memory allocation
    virtual device::Memory* createMemory(
        amd::Memory&    owner   //!< abstraction layer memory object
        ) const;

    //! Sampler object allocation
    virtual bool createSampler(
        const amd::Sampler& owner,  //!< abstraction layer sampler object
        device::Sampler**   sampler //!< device sampler object
        ) const;

    //! Reallocates the provided buffer object
    virtual bool reallocMemory(
        amd::Memory&    owner   //!< Buffer for reallocation
        ) const;

    //! Allocates a view object from the device memory
    virtual device::Memory* createView(
        amd::Memory&      owner,        //!< Owner memory object
        const device::Memory&   parent  //!< Parent device memory object for the view
        ) const;

    //! Create the device program.
    virtual device::Program* createProgram(int oclVer = 120);

    //! Attempt to bind with external graphics API's device/context
    virtual bool bindExternalDevice(
        intptr_t type,
        void* pDevice,
        void* pContext,
        bool validateOnly);

    //! Attempt to unbind with external graphics API's device/context
    virtual bool unbindExternalDevice(
        intptr_t type,
        void* pDevice,
        void* pContext,
        bool validateOnly);

    //! Validates kernel before execution
    virtual bool validateKernel(
        const amd::Kernel& kernel,      //!< AMD kernel object
        const device::VirtualDevice* vdev
        );

    //! Gets a pointer to a region of host-visible memory for use as the target
    //! of a non-blocking map for a given memory object
    virtual void* allocMapTarget(
        amd::Memory&    mem,        //!< Abstraction layer memory object
        const amd::Coord3D& origin, //!< The map location in memory
        const amd::Coord3D& region, //!< The map region in memory
        size_t* rowPitch = NULL,    //!< Row pitch for the mapped memory
        size_t* slicePitch = NULL   //!< Slice for the mapped memory
        );

    //! Retrieves information about free memory on a GPU device
    virtual bool globalFreeMemory(size_t* freeMemory) const;

    //! Returns a GPU memory object from AMD memory object
    gpu::Memory* getGpuMemory(
        amd::Memory* mem    //!< Pointer to AMD memory object
        ) const;

    //! Gets the GPU resource associated with the global heap
    const Resource& globalMem() const { return heap_->resource(); }

    //! Gets the global heap object
    const Heap* heap() const { return heap_; }

    //! Allocates a heap block from the global heap
    HeapBlock* allocHeapBlock(
        size_t size             //!< The heap block size for allocation
        ) const;

    //! Gets the memory object for the dummy page
    amd::Memory* dummyPage() const { return dummyPage_; }

    amd::Monitor& lockAsyncOps() const { return *lockAsyncOps_; }

    //! Returns the lock object for the virtual gpus list
    amd::Monitor* vgpusAccess() const { return vgpusAccess_; }

    //! Returns the number of virtual GPUs allocated on this device
    uint    numOfVgpus() const { return numOfVgpus_; }
    uint    numOfVgpus_;        //!< The number of virtual GPUs (lock protected)

    typedef std::vector<VirtualGPU*> VirtualGPUs;

    //! Returns the list of all virtual GPUs running on this device
    const VirtualGPUs vgpus() const { return vgpus_; }
    VirtualGPUs     vgpus_; //!< The list of all running virtual gpus (lock protected)

    //! Scratch buffer allocation
    gpu::Memory* createScratchBuffer(
        size_t size         //!< Size of buffer
        ) const;

    //! Returns transfer buffer object
    XferBuffers& xferWrite() const { return *xferWrite_; }

    //! Returns transfer buffer object
    XferBuffers& xferRead() const { return *xferRead_; }

    //! Adds GPU memory to the VA cache list
    void addVACache(Memory* memory) const;

    //! Removes GPU memory from the VA cache list
    void removeVACache(const Memory* memory) const;

    //! Finds GPU memory from virtual address
    Memory* findMemoryFromVA(const void* ptr, size_t* offset) const;

    //! Finds an appropriate map target
    amd::Memory* findMapTarget(size_t size) const;

    //! Adds a map target to the cache
    bool addMapTarget(amd::Memory* memory) const;

    //! Returns resource cache object
    ResourceCache& resourceCache() const { return *resourceCache_; }

    //! Returns engines object
    const Engines& engines() const { return engines_; }

    //! Returns engines object
    const device::BlitManager& xferMgr() const { return xferQueue_->blitMgr(); }

    VirtualGPU* xferQueue() const { return xferQueue_; }

    //! Retrieves the internal format from the OCL format
    CalFormat getCalFormat(
        const amd::Image::Format& format    //! OCL image format
        ) const;

    //! Retrieves the OCL format from the internal image format
    amd::Image::Format getOclFormat(
        const CalFormat& format         //! Internal image format
        ) const;

    const ScratchBuffer* scratch(uint idx) const { return scratch_[idx]; }

    //! Returns the global scratch buffer
    Memory* globalScratchBuf() const { return globalScratchBuf_; };

    //! Destroys scratch buffer memory
    void destroyScratchBuffers();

    //! Initialize heap resources if uninitialized
    bool    initializeHeapResources();

    //! Set GSL sampler to the specified state
    void    fillHwSampler(
        uint32_t    state,              //!< Sampler's OpenCL state
        void*       hwState,            //!< Sampler's HW state
        uint32_t    hwStateSize         //!< Size of sampler's HW state
        ) const;

    //! host memory alloc
    virtual void* hostAlloc(size_t size, size_t alignment, bool atomics = false) const;

    //! SVM allocation
    virtual void* svmAlloc(amd::Context& context, size_t size, size_t alignment, cl_svm_mem_flags flags) const;

    //! Free host SVM memory
    void hostFree(void* ptr, size_t size) const;

    //! SVM free
    virtual void svmFree(void* ptr) const;

    //! Returns SRD manger object
    SrdManager& srds() const { return *srdManager_; }

private:
    //! Disable copy constructor
    Device(const Device&);

    //! Disable assignment
    Device& operator=(const Device&);

    //! Sends the stall command to all queues
    bool stallQueues();

    //! Fills OpenCL device info structure
    void fillDeviceInfo(
        const CALdeviceattribs& calAttr,    //!< CAL device attributes info
        const CALdevicestatus&  calStatus   //!< CAL device status
#if cl_amd_open_video
        ,
        const CALdeviceVideoAttribs& calVideoAttr   //!< -"- video attrib. info
#endif //cl_amd_open_video
        );

    //! Buffer allocation from static heap (no VM mode only)
    gpu::Memory* createBufferFromHeap(
        amd::Memory&    owner           //!< Abstraction layer memory object
        ) const;

    //! Buffer allocation
    gpu::Memory* createBuffer(
        amd::Memory&    owner,          //!< Abstraction layer memory object
        bool            directAccess,   //!< Use direct host memory access
        bool            bufferAlloc     //!< If TRUE, then don't use heap
        ) const;

    //! Image allocation
    gpu::Memory* createImage(
        amd::Memory&    owner,          //!< Abstraction layer memory object
        bool            directAccess    //!< Use direct host memory access
        ) const;

    //! Allocates/reallocates the scratch buffer, according to the usage
    bool allocScratch(
        uint regNum,                //!< Number of the scratch registers
        const VirtualGPU* vgpu      //!< Virtual GPU for the allocation
        );

    amd::Context*   context_;       //!< A dummy context for internal allocations
    size_t      heapSize_;          //!< The global heap size
    Heap*       heap_;              //!< GPU heap manager
    amd::Memory*    dummyPage_;     //!< A dummy page for NULL pointer

    amd::Monitor*   lockAsyncOps_;  //!< Lock to serialise all async ops on this device
    amd::Monitor*   lockAsyncOpsForInitHeap_;  //!< Lock to serialise all async ops on initialization heap operation
    amd::Monitor*   vgpusAccess_;   //!< Lock to serialise virtual gpu list access
    amd::Monitor*   scratchAlloc_;  //!< Lock to serialise scratch allocation
    amd::Monitor*   mapCacheOps_;   //!< Lock to serialise cache for the map resources

    XferBuffers*    xferRead_;      //!< Transfer buffers read
    XferBuffers*    xferWrite_;     //!< Transfer buffers write

    amd::Monitor*   vaCacheAccess_; //!< Lock to serialize VA caching access
    std::list<VACacheEntry*>*   vaCacheList_; //!< VA cache list
    std::vector<amd::Memory*>*  mapCache_;  //!< Map cache info structure
    ResourceCache*  resourceCache_; //!< CAL resource cache
    Engines         engines_;       //!< Available engines on device
    bool            heapInitComplete_;  //!< Keep track of initialization status of heap resources
    VirtualGPU*     xferQueue_;     //!< Transfer queue
    std::vector<ScratchBuffer*> scratch_;   //!< Scratch buffers for kernels
    Memory*         globalScratchBuf_;  //!< Global scratch buffer
    SrdManager*     srdManager_;    //!< SRD manager object

    static AppProfile appProfile_; //!< application profile
};

/*@}*/} // namespace gpu

#endif /*GPU_HPP_*/
