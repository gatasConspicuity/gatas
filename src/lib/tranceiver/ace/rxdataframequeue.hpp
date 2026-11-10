#pragma once

#include <stdint.h>

#include "ace/messages.hpp"
#include "ace/models.hpp"

#include "ace/bitutils.hpp"
#include "etl/queue_mpmc_mutex.h"

/**
 * @brief THis was created to handle the raw Lora/GFSK frames from the radio tranceivers such that the tranceiver is quickly ofloaded.
 *        by doing this, each protocol does not have the need for each own tasks anymore and as such this will free up memory and
 *        removes a few tasks
 */
class RxDataFrameQueue : public BaseModule, public etl::message_router<RxDataFrameQueue>
{

private:
    friend class message_router;

    struct DataSourceMatch
    {
        GATAS::DataSource dataSource;
        uint8_t bitsToShift;
        uint8_t frameLength;
    };

    struct
    {
        uint32_t totalQueued = 0;
        uint32_t queueFull = 0;
    } statistics;

    TaskHandle_t taskHandle;
    etl::queue_mpmc_mutex<GATAS::DataFrame, 2, etl::memory_model::MEMORY_MODEL_SMALL> dataQueue;

    static void radioQueueTaskTrampoline(void *arg);
    void radioQueueTask(void *arg);

    // ******************** Message bus receive handlers ********************
    void on_receive_unknown(const etl::imessage &msg);

    static uint32_t CRCsyndrome(uint8_t Bit);
    static uint8_t FindCRCsyndrome(uint32_t Syndr);

public:
    static constexpr const etl::string_view NAME = "RxDataFrameQueue";

    RxDataFrameQueue(etl::imessage_bus &bus, const Configuration &config) : BaseModule(bus, NAME), taskHandle(nullptr), dataQueue()
    {
        (void)config;
    }
    virtual ~RxDataFrameQueue() = default;

    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    bool push(GATAS::DataFrame &&frame);

    DataSourceMatch decideDataSource(GATAS::DataSource ds, uint32_t frame[], uint8_t frameLength);
};
