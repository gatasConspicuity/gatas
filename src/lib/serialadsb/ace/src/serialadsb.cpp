#include <stdio.h>

#include "pico/stdlib.h"

#include "../serialadsb.hpp"

#include "ace/coreutils.hpp"

void SerialADSB::start()
{
    if (xTaskCreate(serialADSBTask, SerialADSB::NAME.cbegin(), configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, &taskHandle) != pdPASS)
    {
        pioSerial.releaseUnstarted();
        BaseModule::setModuleStatus(NAME, GATAS::PostConstruct::TASK_ERROR);
        return;
    }
    pioSerial.start();
};

GATAS::PostConstruct SerialADSB::postConstruct()
{
    return pioSerial.postConstruct();
}

void SerialADSB::serialADSBTask(void *arg)
{
    SerialADSB *serialAdsb = static_cast<SerialADSB *>(arg);
    serialAdsb->statistics.queueFullErr = 0;
    GATAS::ADSBString sentence;
    while (true)
    {
        uint32_t notifyValue = 0;
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, portMAX_DELAY);
        if (notifyValue)
        {
            if (notifyValue & TaskState::NEW)
            {
                while (serialAdsb->queue.pop(sentence))
                {
                    serialAdsb->statistics.totalReceived += 1;
                    // Finalise implementation to create the binary message here and send
                    //                    serialAdsb->getBus().receive(GATAS::ADSBMessageBinMsg{sentence});
                }
            }
        }
    }
}

void SerialADSB::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"totalReceived:k\":" << statistics.totalReceived;
    stream << ",\"queueFull:err\":" << statistics.queueFullErr;
    stream << "}";
}

void __time_critical_func(SerialADSB::processNewSentence)(const etl::array_view<char> &sentence)
{
    if (!queue.full())
    {
        queue.push(sentence.data());
    }
    else
    {
        statistics.queueFullErr += 1;
    }

    // To reduce some FreeRTOS switches only send when queue is nearly full
    // Since this is a continues streem, this should be fine
    if (queue.size() > QUEUE_SIZE - 2)
    {
        xTaskNotifyFromISR(taskHandle, TaskState::NEW, eSetBits, nullptr);
    }
}
