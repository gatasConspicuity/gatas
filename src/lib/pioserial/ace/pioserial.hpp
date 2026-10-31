#pragma once

/* System. */
#include <stdint.h>

/* PICO. */
#include "pico/stdlib.h"
#include "uart_rx.pio.h"
#include "uart_rx_f.h"
#include "uart_tx.pio.h"

#include "hardware/irq.h"

/* GATAS. */
#include "ace/constants.hpp"
#include "ace/models.hpp"
#include "ace/coreutils.hpp"

#include "etl/array.h"
#include "etl/array_view.h"
#include "etl/delegate.h"

class PioSerial
{
    public:
    using CallBackFunction = etl::delegate<void(const etl::array_view<char>&)>;

private:
    static constexpr etl::array commonBaudrates{ 115200, 9600, 19200, 38400, 57600 };


    static void pio0_irq0_func_handler()
    {
        pio_irq_func(0);
    }

    static void pio0_irq1_func_handler()
    {
        pio_irq_func(1);
    }

    static void pio1_irq0_func_handler()
    {
        pio_irq_func(2);
    }

    static void pio1_irq1_func_handler()
    {
        pio_irq_func(3);
    }

    // We have PIO's and each PIO has 8 state machine
    // Note: We properly need to put these in a struct or something
    static etl::array<PioSerial*, 4> interruptHandlers;

    uint8_t handlerIdx=0;

    const uint8_t rxPin;
    const uint8_t txPin;
    const uint32_t baudrate;

    PIO rxPio;
    int rxSmIndx;
    uint rxOffset;
    uint8_t charIndex;

    PIO txPio;
    int txSmIndx;
    uint txOffset;

    irq_handler_t handler;
    etl::array<char, GATAS::NMEA_MAX_LENGTH + 1> buffer;
    CallBackFunction callback;

    bool enableRx();
    void disableRx();
public:
    PioSerial(const GATAS::PinTypeMap &pins, uint32_t baudrate_, CallBackFunction callback_) :
        rxPin(CoreUtils::pinValue(pins, GATAS::PinType::RX)),
        txPin(CoreUtils::pinValue(pins, GATAS::PinType::TX)),
        baudrate(baudrate_),
        rxPio(nullptr),
        rxSmIndx(UINT8_MAX),
        rxOffset(0),
        charIndex(0),
        txPio(nullptr),
        txSmIndx(UINT8_MAX),
        txOffset(0),
        handler(nullptr),
        callback(callback_)
    {
    }

    virtual ~PioSerial() = default;

    GATAS::PostConstruct postConstruct();

    // Release resources allocated by postConstruct before start has been called.
    void releaseUnstarted();

    void start() ;
    void stop() ;


    /**
     * IRQ called when the pio fifo is not empty, i.e. there are some characters on the uart
     * When a NMEA string is found, it will send a message using FReeRTOS
     * The message is guaranteed to be zero terminated
    */
    static void pio_irq_func(uint8_t irqHandlerIndex);

    bool enableTx(uint32_t givenBaudRate);
    /**
     * Send that to the uart using blocking IO
     * At least one PIO needs to be available
     * Note: Don't use this for continues sending of data. This is only for sending a few bytes occasionally,
     * use an hardware uart for that.
     * If it's really needed we should add the code for continues sending of data, but it will claim a PIO for that
     *
     * @param data
     * @param length
     * @return true if the data was sent
    */
    void sendBlocking(const uint8_t *data, uint16_t length);
    void sendBlocking(const etl::string_view &data);
    void disableTx();

    bool setBaudRate(uint32_t baudRate);
    /**
     * Validate if the uart is receiving any valid data at the given baudrate
    */
    bool testUartAtBaudrate(uint32_t testBaudRate, uint32_t maximumScanTimeMs, uint16_t numcharsConsideringValid=48);

    /**
     * Find a buadrate where the uart is sending data on
    */
    uint32_t findBaudRate(uint32_t maxTimeOutMs=1000);

    /**
     * Flush the RX Buffers up to a timeout in ms
     */
    void rxFlush();

};
