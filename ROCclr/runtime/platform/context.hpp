//
// Copyright (c) 2008 Advanced Micro Devices, Inc. All rights reserved.
//

#ifndef CONTEXT_HPP_
#define CONTEXT_HPP_

#include "top.hpp"
#include "device/device.hpp"
#include "platform/object.hpp"
#include "platform/agent.hpp"

#include <vector>
#include <map>

namespace amd {

/*! \addtogroup Runtime
 *  @{
 *
 *  \addtogroup Contexts
 *  @{
 */

class GLFunctions;
class DeviceQueue;

class Context : public RuntimeObject
{
    std::vector<Device*> devices_;

public:
    enum DeviceFlagIdx
    {
        GLDeviceKhrIdx = 0,     //!< GL
        D3D10DeviceKhrIdx,      //!< D3D10
        OfflineDevicesIdx,      //!< Offline devices
        CommandInterceptIdx,    //!< Command intercept
        D3D11DeviceKhrIdx,      //!< D3D11
        InteropUserSyncIdx,     //!< Interop user sync enabled
        D3D9DeviceKhrIdx,       //!< d3d9 device
        D3D9DeviceEXKhrIdx,     //!< d3d9EX device
        D3D9DeviceVAKhrIdx,     //!< d3d9VA device
        EGLDeviceKhrIdx,        //!< EGL device
        LastDeviceFlagIdx
    };

    enum Flags
    {
        GLDeviceKhr         = 1 << GLDeviceKhrIdx,      //!< GL
        D3D10DeviceKhr      = 1 << D3D10DeviceKhrIdx,   //!< D3D10
        OfflineDevices      = 1 << OfflineDevicesIdx,   //!< Offline devices
        CommandIntercept    = 1 << CommandInterceptIdx, //!< Command intercept
        D3D11DeviceKhr      = 1 << D3D11DeviceKhrIdx,   //!< D3D11
        InteropUserSync     = 1 << InteropUserSyncIdx,  //!< Interop user sync enabled
        D3D9DeviceKhr       = 1 << D3D9DeviceKhrIdx,    //!< d3d9 device
        D3D9DeviceEXKhr     = 1 << D3D9DeviceEXKhrIdx,  //!< d3d9EX device
        D3D9DeviceVAKhr     = 1 << D3D9DeviceVAKhrIdx,  //!< d3d9VA device
        EGLDeviceKhr        = 1 << EGLDeviceKhrIdx,     //!< EGL device
    };

    //! Context info structure
    struct Info
    {
        uint        flags_;         //!< Context info flags
        void*       hDev_[LastDeviceFlagIdx];    //!< Device object reference
        void*       hCtx_;          //!< Context object reference
        size_t      propertiesSize_;//!< Size of the original properties in bytes
        cl_int      (CL_CALLBACK * commandIntercept_)(cl_event, cl_int *);
    };

    struct DeviceQueueInfo
    {
        DeviceQueue*    defDeviceQueue_;    //!< Default device queue
        uint            deviceQueueCnt_;    //!< The number of device queues
        DeviceQueueInfo(): defDeviceQueue_(NULL), deviceQueueCnt_(0) {}
    };

private:
    // Copying a Context is not allowed
    Context(const Context&);
    Context& operator = (const Context&);

protected:
    bool terminate() {
        if (Agent::shouldPostContextEvents()) {
            Agent::postContextFree(as_cl(this));
        }
        return true;
    }
    //! Context destructor
    ~Context();

public:
    /*! \brief Helper function to check the context properties and initialize
     *  context info structure
     *
     *  \return An errcode if invalid, CL_SUCCESS if valid
     */
    static int checkProperties(
        const cl_context_properties* properties,    //!< Properties
        Info*                        info           //!< Info structure
        );

    //! Default constructor
    Context(
        const std::vector<Device*>& devices,    //!< List of all devices
        const Info&         info                //!< Context info structure
        );

    //! Compare two Context instances.
    bool operator == (const Context& rhs) const { return this == &rhs; }
    bool operator != (const Context& rhs) const { return !(*this == rhs); }

    /*! Creates the context
     *
     *  \return An errcode if runtime fails the context creation,
     *          CL_SUCCESS otherwise
     */
    int create(
        const intptr_t* properties  //!< Original context properties
        );

    /**
     * Allocate host memory using either a custom device allocator or a generic
     * OS allocator
     *
     * @param size Allocation size, in bytes
     * @param alignment Desired alignment, in bytes
     * @param atomics The buffer should support platform (SVM) atomics
     */
    void* hostAlloc(size_t size, size_t alignment, bool atomics = false) const;

    /**
     * Release host memory
     * @param ptr Pointer allocated using ::hostAlloc. If the pointer has been
     * allocated elsewhere, the behavior is undefined
     */
    void hostFree(void* ptr) const;

    /**
     * Allocate SVM buffer
     *
     * @param size Allocation size, in bytes
     * @param alignment Desired alignment, in bytes
     * @param flags The flags to create a svm space
     */
    void* svmAlloc(size_t size, size_t alignment, cl_svm_mem_flags flags = CL_MEM_READ_WRITE);

    /**
     * Release SVM buffer
     * @param ptr Pointer allocated using ::svmAlloc. If the pointer has been
     * allocated elsewhere, the behavior is undefined
     */
    void svmFree(void* ptr) const;

    //! Return the devices associated with this context.
    const std::vector<Device*>& devices() const { return devices_; }

    //! Return the SVM capable devices associated with this context.
    const std::vector<Device*>& svmDevices() const { return svmAllocDevice_; }

    //! Returns true if the given device is associated with this context.
    bool containsDevice(const Device* device) const;

    //! Returns the context info structure
    const Info& info() const { return info_; }

    //! Returns a pointer to the original properties
    const cl_context_properties* properties() const { return properties_; }

    //! Returns a pointer to the OpenGL context
    GLFunctions* glenv() const { return glenv_; }

    //! RTTI internal implementation
    virtual ObjectType objectType() const { return ObjectTypeContext; }

    //! Returns context lock for the serialized access to the context
    Monitor&    lock() { return ctxLock_; }

    //! Returns TRUE if runtime succesfully added a device queue
    DeviceQueue* defDeviceQueue(const Device& dev) const;

    //! Returns TRUE if runtime succesfully added a device queue
    bool isDevQueuePossible(const Device& dev);

    //! Returns TRUE if runtime succesfully added a device queue
    void addDeviceQueue(
        const Device& dev,      //!< Device object
        DeviceQueue* queue,     //!< Device queue
        bool defDevQueue        //!< Added device queue will be the default queue
        );

    //! Removes a device queue from the list of queues
    void removeDeviceQueue(
        const Device& dev,      //!< Device object
        DeviceQueue* queue      //!< Device queue
        );

private:
    const Info  info_;                  //!< Context info structure
    cl_context_properties* properties_; //!< Original properties
    GLFunctions* glenv_;                //!< OpenGL context
    Device* customHostAllocDevice_;     //!< Device responsible for host allocations
    std::vector<Device*> svmAllocDevice_;      //!< Devices can support SVM allocations
    std::map<const Device*, DeviceQueueInfo> deviceQueues_; //!< Device queues mapping
    Monitor     ctxLock_;               //!< Lock for the context access
};

/*! @}
 *  @}
 */

} // namespace amd

#endif /*CONTEXT_HPP_*/
