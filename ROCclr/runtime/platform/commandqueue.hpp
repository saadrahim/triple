//
// Copyright 2012 Advanced Micro Devices, Inc. All rights reserved.
//

/*! \file commandqueue.hpp
 *  \brief  Declarations CommandQueue object.
 *
 * \author Laurent Morichetti (laurent.morichetti@amd.com)
 * \date   October 2008
 */

#ifndef COMMAND_QUEUE_HPP_
#define COMMAND_QUEUE_HPP_

#include "thread/thread.hpp"
#include "platform/object.hpp"
#include "platform/command.hpp"
/*! \brief Holds commands that will be executed on a specific device.
 *
 *  \details A command queue is created on a specific device in
 *  a Context. A new virtual device will be instantiated from the given
 *  device and an execution environment (a thread) will be created to run
 *  the CommandQueue::loop() function.
 */

namespace amd {

class HostQueue;
class DeviceQueue;

class CommandQueue : public RuntimeObject
{
public:
    struct Properties
    {
        typedef cl_command_queue_properties value_type;
        const value_type mask_;
        value_type value_;

        Properties(value_type mask, value_type value) :
            mask_(mask), value_(value & mask)
        { }

        bool set(value_type bits) {
            if ((mask_ & bits) != bits) {
                return false;
            }
            value_ |= bits;
            return true;
        }

        bool clear(value_type bits) {
            if ((mask_ & bits) != bits) {
                return false;
            }
            value_ &= ~bits;
            return true;
        }

        bool test(value_type bits) const {
            return (value_ & bits) != 0;
        }
    };

    //! Return the context this command queue is part of.
    Context& context() const { return context_(); }

    //! Return the device for this command queue.
    Device& device() const { return device_; }

    //! Return the command queue properties.
    Properties properties() const { return properties_; }
    Properties& properties() { return properties_; }

    //! Returns the base class object
    CommandQueue* asCommandQueue() { return this; }

    virtual ~CommandQueue() {}

    //! Returns TRUE if the object was successfully created
    virtual bool create() = 0;

    //! RTTI internal implementation
    virtual ObjectType objectType() const { return ObjectTypeQueue; }

    //! Rturns HostQueue object
    virtual HostQueue* asHostQueue() { return NULL; }

    //! Returns DeviceQueue object
    virtual DeviceQueue* asDeviceQueue() { return NULL; }

protected:
    //! CommandQueue constructor is protected
    //! to keep the CommandQueue class as a virtual interface
    CommandQueue(
        Context&    context,    //!< Context object
        Device&     device,     //!< Device object
        cl_command_queue_properties properties, //!< Queue properties
        cl_command_queue_properties propMask    //!< Queue properties mask
        )
        : properties_(propMask, properties)
        , queueLock_("CommandQueue::queueLock")
        , device_(device)
        , context_(context) {}

    Properties  properties_;    //!< Queue properties
    Monitor     queueLock_;     //!< Lock protecting the queue
    Device&     device_;        //!< The device
    SharedReference<Context> context_;  //!< The context of this command queue

private:
    //! Disable copy constructor
    CommandQueue(const CommandQueue&);

    //! Disable assignment
    CommandQueue& operator=(const CommandQueue&);
};


class HostQueue : public CommandQueue
{
    class Thread : public amd::Thread
    {
    public:
        //! True if this command queue thread is accepting commands.
        volatile bool acceptingCommands_;

        //! Create a new thread
        Thread() : amd::Thread("Command Queue Thread", CQ_THREAD_STACK_SIZE),
            acceptingCommands_(false), virtualDevice_(NULL)
        { }

        //! The command queue thread entry point.
        void run(void *data) {
            HostQueue* queue = static_cast<HostQueue*>(data);

            bool interopQueue = (0 != (queue->context().info().flags_ & (Context::GLDeviceKhr | Context::D3D10DeviceKhr | Context::D3D11DeviceKhr)));

            virtualDevice_ = queue->device().createVirtualDevice(
                queue->properties().test(CL_QUEUE_PROFILING_ENABLE),
                interopQueue
#if cl_amd_open_video
                , queue->calVideoProperties_
#endif // cl_amd_open_video
                );
            if (virtualDevice_ != NULL) {
                queue->loop(virtualDevice_);
                if (virtualDevice_->terminate()) {
                    delete virtualDevice_;
                }
            }
            else {
                acceptingCommands_ = false;
                queue->flush();
            }
        }

        //! Get virtual device for the current thread
        device::VirtualDevice* vdev() const { return virtualDevice_; }

    private:
        device::VirtualDevice* virtualDevice_;  //!< Virtual device for this thread

    } thread_; //!< The command queue thread instance.

private:
    ConcurrentLinkedQueue<Command*> queue_; //!< The queue.

    //! Await commands and execute them as they become ready.
    void loop(device::VirtualDevice* virtualDevice);

protected:
    virtual bool terminate();

#if cl_amd_open_video
    void* calVideoProperties_;
#endif // cl_amd_open_video

public:
    /*! \brief Construct a new host queue.
     *
     * \note A new virtual device instance will be created from the
     * given device.
     */
    HostQueue(
        Context& context,
        Device& device,
        cl_command_queue_properties properties
#if cl_amd_open_video
        , void* calVideoProperties = NULL
#endif // cl_amd_open_video
    );

    //! Returns TRUE if this command queue can accept commands.
    virtual bool create() { return thread_.acceptingCommands_; }

    //! Append the given command to the queue.
    void append(Command& command);

    //! Return the thread object running the command loop.
    const Thread& thread() const { return thread_; }

    //! Signal to start processing the commands in the queue.
    void flush  () { ScopedLock sl(queueLock_); queueLock_.notify(); }

    //! Finish all queued commands
    void finish();

    //! Get virtual device for the current command queue
    device::VirtualDevice* vdev() const { return thread_.vdev(); }

    //! Return the current queue as the HostQueue
    virtual HostQueue* asHostQueue() { return this; }
};


class DeviceQueue : public CommandQueue
{
public:
    DeviceQueue(
        Context&    context,    //!< Context object
        Device&     device,     //!< Device object
        cl_command_queue_properties properties, //!< Queue properties
        uint        size        //!< Device queue size
        )
        : CommandQueue(context, device, properties, device.info().queueOnDeviceProperties_
            | CL_QUEUE_ON_DEVICE | CL_QUEUE_ON_DEVICE_DEFAULT)
        , size_(size)
        , virtualDevice_(NULL) {}

    virtual ~DeviceQueue();

    //! Returns TRUE if device queue was successfully created
    virtual bool create();

    //! Return the current queue as the DeviceQueue
    virtual DeviceQueue* asDeviceQueue() { return this; }

    //! Returns the size of device queue
    uint    size() const { return size_; }

    //! Returns virtual device for this device queue
    device::VirtualDevice* vDev() const { return virtualDevice_; }

    //! Returns the queue lock
    Monitor& lock() { return queueLock_; }

private:
    uint        size_;          //!< Device queue size
    device::VirtualDevice* virtualDevice_;  //!< Virtual device for this queue
};
} //namespace amd

#endif //COMMAND_QUEUE_HPP_