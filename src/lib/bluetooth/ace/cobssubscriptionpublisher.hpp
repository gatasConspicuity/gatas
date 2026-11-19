#pragma once

#include "btstack_run_loop.h"
#include "etl/delegate.h"

/** Read and publish the current subscription state only on the Bluetooth task. */
class CobsSubscriptionPublisher
{
public:
    using Publish = etl::delegate<void()>;

private:
    Publish publishCurrentState;
    btstack_context_callback_registration_t callback{};

    static void publish(void *context)
    {
        auto *self = static_cast<CobsSubscriptionPublisher *>(context);
        self->publishCurrentState();
    }

public:
    explicit CobsSubscriptionPublisher(Publish publishCurrentState_) : publishCurrentState(publishCurrentState_)
    {
        callback.callback = &CobsSubscriptionPublisher::publish;
        callback.context = this;
    }

    void changed()
    {
        // BTstack coalesces requests for this record. Capture no state on the caller's task.
        btstack_run_loop_execute_on_main_thread(&callback);
    }

    CobsSubscriptionPublisher(const CobsSubscriptionPublisher &) = delete;
    CobsSubscriptionPublisher &operator=(const CobsSubscriptionPublisher &) = delete;
    CobsSubscriptionPublisher(CobsSubscriptionPublisher &&) = delete;
    CobsSubscriptionPublisher &operator=(CobsSubscriptionPublisher &&) = delete;
};
