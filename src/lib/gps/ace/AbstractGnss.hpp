#pragma once

#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "etl/string_view.h"

#include "ace/constants.hpp"
#include "ace/messages.hpp"
#include "ace/pioserial.hpp"

#include "etl/queue_spsc_atomic.h"

class AbstractGnss : public BaseModule
{
protected:
    struct
    {
        uint32_t totalReceived = 0;
        uint32_t baudrate = 0;
        uint32_t queueFullErr = 0;
        etl::string<16> status;
    } statistics;

private:
    static constexpr uint8_t QUEUE_SIZE = 6;
     // If you change this, you need to change the baudrate in attached GPS configurations
    static constexpr uint32_t DEFAULT_GPS_BAUDRATE = 115200;
    // When set to true, it dumps once a while the lag between hardware PPS and software PPS
    // Obviously this requires that softPPS is disabled for this device and the PPS pin is connected
    // The usecase is to test the delay when a hardware device has or has not PPS connected
    // Only for developers Look at teh output for Software PPS lags XXXXXXus behind .
    // Round down the value, and use that for the init value of the GPS device
    #define ABSTRACT_GNSS_MEASURE_SOFTPPS_LAG ( 0 )
    friend class message_router;

    enum TaskState : uint32_t
    {
        START = 1 << 0,
        NEW = 1 << 2,
    };

    static void receiveTask(void *arg);

    PioSerial pioSerial;
    const uint8_t ppsPin;
    const bool usePioSerial;
    bool softwarebasedPPS;
    int32_t softPPSlagUs;
    uint32_t measureSoftPPSlag = 0;
    TaskHandle_t taskHandle = nullptr;
    etl::queue_spsc_atomic<GATAS::NMEAString, QUEUE_SIZE, etl::memory_model::MEMORY_MODEL_SMALL> queue;

    static constexpr const etl::string_view NAME = "Gnss";

private:
    void processNewSentence(const etl::array_view<char> &sentence, bool fromISR);

protected:
    bool receiveTaskReady() const
    {
        return taskHandle != nullptr;
    }

    PioSerial &getSerial()
    {
        return pioSerial;
    }
    void processNewSentenceFromISR(const etl::array_view<char> &sentence);
    void processNewSentenceFromTask(const etl::array_view<char> &sentence);

    /** What a implementation needs to override */
    void setStatus(const etl::string_view status)
    {
        statistics.status = status;
    }

    void setStatusBaud(uint32_t baud)
    {
        statistics.baudrate = baud;
    }

    bool isSoftwarePPS() const
    {
        return softwarebasedPPS;
    }

    /*
    This method can be overwritten to cleanup teh sebtence if needed
    */
    virtual bool preProcessSentence(etl::string_view sentence)
    {
        (void)sentence;
        return true;
    }

    virtual bool configureGnss() = 0;

    bool popQueue(GATAS::NMEAString &sentence)
    {
        if (queue.empty())
        {
            return false;
        }
        queue.pop(sentence);
        return true;
    }

public:
    AbstractGnss(etl::imessage_bus &bus, const etl::string_view name, const GATAS::PinTypeMap &pins, bool softPPS, int32_t softPPSLagUs_, bool usePioSerial_ = true)
        : BaseModule(bus, name),
          pioSerial{pins, DEFAULT_GPS_BAUDRATE, PioSerial::CallBackFunction::create<AbstractGnss, &AbstractGnss::processNewSentenceFromISR>(*this)},
          ppsPin(CoreUtils::pinValue(pins, GATAS::PinType::BUSY)),
          usePioSerial(usePioSerial_),
          softwarebasedPPS(softPPS || ABSTRACT_GNSS_MEASURE_SOFTPPS_LAG),
          softPPSlagUs(softPPSLagUs_)
    {
    }

    virtual ~AbstractGnss() = default;

    virtual GATAS::PostConstruct postConstruct() override;

    virtual void start() override;

    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    bool isPpsDetected();
};
