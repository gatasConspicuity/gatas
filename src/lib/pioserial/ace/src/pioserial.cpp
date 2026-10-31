#include <stdio.h>

#include "../pioserial.hpp"
#include "hardware/pio.h"

#include "pico/binary_info.h"

#include "ace/picopio.hpp"

etl::array<PioSerial*, 4> PioSerial::interruptHandlers;

GATAS::PostConstruct PioSerial::postConstruct()
{
    if (rxPin == UINT8_MAX || txPin == UINT8_MAX)
    {
        return GATAS::PostConstruct::HARDWARE_NOT_CONFIGURED;
    }

    handlerIdx = 0;
    while (handlerIdx < interruptHandlers.size() && interruptHandlers[handlerIdx] != nullptr)
    {
        ++handlerIdx;
    }

    if (handlerIdx >= interruptHandlers.size())
    {
        return GATAS::PostConstruct::HARDWARE_ERROR;
    }
    interruptHandlers[handlerIdx] = this;

    // Set tx to out to prevent it from floating. Attached devices might receive random data
    gpio_init(txPin);
    gpio_set_dir(txPin, GPIO_OUT);
    gpio_put(txPin, 1);

    // Enable Rx
    if (!enableRx())
    {
        releaseUnstarted();
        return GATAS::PostConstruct::HARDWARE_ERROR;
    }

    GATAS_ASSERT(PIO0_IRQ_1 == PIO0_IRQ_0 + 1 && PIO1_IRQ_1 == PIO1_IRQ_0 + 1, "Incorrect IRQ definitions");
    uint8_t pio_irq = (rxPio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0; // pio_irq will become 7,8,9,10
    if (irq_get_exclusive_handler(pio_irq))
    {
        pio_irq += 1;
        if (irq_get_exclusive_handler(pio_irq))
        {
            releaseUnstarted();
            return GATAS::PostConstruct::HARDWARE_ERROR;
        }
    }

    switch (pio_irq)
    {
    case PIO0_IRQ_0:
        handler = pio0_irq0_func_handler;
        break;
    case PIO0_IRQ_1:
        handler = pio0_irq1_func_handler;
        break;
    case PIO1_IRQ_0:
        handler = pio1_irq0_func_handler;
        break;
    case PIO1_IRQ_1:
        handler = pio1_irq1_func_handler;
        break;
    default:
        releaseUnstarted();
        return GATAS::PostConstruct::HARDWARE_NOT_FOUND;
    }

    bi_decl(bi_2pins_with_func(static_cast<uint32_t>(txPin), static_cast<uint32_t>(rxPin), GPIO_FUNC_UART));
    return GATAS::PostConstruct::OK;
}

void PioSerial::releaseUnstarted()
{
    disableRx();
    if (handlerIdx < interruptHandlers.size() && interruptHandlers[handlerIdx] == this)
    {
        interruptHandlers[handlerIdx] = nullptr;
    }
}

void PioSerial::start()
{
    // Enable interrupt
    uint8_t pio_irq = (rxPio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0; // pio_irq will become 7,8,9,10
    uint8_t irq_index = pio_irq - ((rxPio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0);
    irq_add_shared_handler(pio_irq, handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);                                     // Add a shared IRQ handler
    irq_set_enabled(pio_irq, true);                                                                                               // Enable the IRQ
    pio_set_irqn_source_enabled(rxPio, irq_index, static_cast<pio_interrupt_source>(pis_sm0_rx_fifo_not_empty + rxSmIndx), true); // Set pio to tell us when the FIFO is NOT empty
};

bool PioSerial::enableRx()
{

    if (rxPio == nullptr)
    {
        // Set up the state machine to use to use
        if (!add_pio_program(&uart_rx_program, &rxPio, &rxSmIndx, &rxOffset))
        {
            return false;
        }
        uart_rx_program_init(rxPio, rxSmIndx, rxOffset, rxPin, baudrate);
    }
    return true;
}

void PioSerial::disableRx()
{

    if (rxPio != nullptr)
    {
        // Cleanup Pio
        pio_sm_set_enabled(rxPio, rxSmIndx, false);
        pio_remove_program(rxPio, &uart_rx_program, rxOffset);
        pio_sm_unclaim(rxPio, rxSmIndx);

        // Remove the handler
        rxSmIndx = -1;
        rxOffset = 0;
        rxPio = nullptr;
    }
}

/**
 * IRQ called when the pio fifo is not empty, i.e. there are some characters on the uart
 * When a NMEA string is found, it will send a message using FReeRTOS
 * The message is guaranteed to be zero terminated
 */
void __isr __time_critical_func(PioSerial::pio_irq_func)(uint8_t irqHandlerIndex)
{
    if (irqHandlerIndex >= interruptHandlers.size())
    {
        GATAS_WARN("irqHandlerIndex >= numEntries");
        return;
    }

    PioSerial *pioSerial = interruptHandlers[irqHandlerIndex];
    if (pioSerial == nullptr)
    {
        GATAS_WARN("pioSerial == nullptr");
        return;
    }

    while (!pio_sm_is_rx_fifo_empty(pioSerial->rxPio, pioSerial->rxSmIndx))
    {
        uint32_t data = pio_sm_get(pioSerial->rxPio, pioSerial->rxSmIndx);
        char *bytePtr = (char *)&data;
        for (int i = 0; i < 4; i++)
        {
            char c = bytePtr[i];
            if (c == '\n' || c == '\r')
            {
                // Ignore any newline or return characters on index 0
                if (pioSerial->charIndex > 0 && pioSerial->charIndex < pioSerial->buffer.size() - 1)
                {
                    pioSerial->buffer[pioSerial->charIndex] = '\0';
                    pioSerial->callback({pioSerial->buffer.data(), pioSerial->charIndex});
                }
                pioSerial->charIndex = 0;
            }
            else
            {
                if (pioSerial->charIndex < pioSerial->buffer.size() - 1) // && c >= 32 && c <= 126
                {
                    pioSerial->buffer[pioSerial->charIndex++] = c;
                }
                else
                {
                    pioSerial->charIndex = 0;
                }
            }
        }
    }
}

void PioSerial::sendBlocking(const uint8_t *data, uint16_t length)
{
    uart_tx_program_put(txPio, txSmIndx, data, length);
}

void PioSerial::sendBlocking(const etl::string_view &data)
{
    uart_tx_program_put(txPio, txSmIndx, (uint8_t *)data.cbegin(), data.size());
}

bool PioSerial::enableTx(uint32_t givenBaudRate)
{
    if (txPio == nullptr)
    {
        if (!add_pio_program(&uart_tx_program, &txPio, &txSmIndx, &txOffset))
        {
            GATAS_WARN("failed to setup pio for tx");
            return false;
        }
    }
    uart_tx_program_init(txPio, txSmIndx, txOffset, txPin, givenBaudRate);
    return true;
}

void PioSerial::disableTx()
{
    if (txPio != nullptr)
    {
        pio_sm_set_enabled(txPio, txSmIndx, false);
        pio_remove_program(txPio, &uart_tx_program, txOffset);
        pio_sm_unclaim(txPio, txSmIndx);

        txPio = nullptr;
        txOffset = 0;
        txSmIndx = 0;
    }
}

bool PioSerial::setBaudRate(uint32_t baudRate)
{
    if (rxPio != nullptr)
    {
        pio_sm_set_enabled(rxPio, rxSmIndx, false);
        uart_rx_program_init(rxPio, rxSmIndx, rxOffset, rxPin, baudRate);
        return true;
    }
    return false;
}

/**
 * Validate if the uart is receiving any valid data at the given baudrate
 */
bool PioSerial::testUartAtBaudrate(uint32_t testBaudRate, uint32_t maximumScanTimeMs, uint16_t numcharsConsideringValid)
{
    if (rxPio != nullptr)
    {
        setBaudRate(testBaudRate);
//        printf("Baud: tx:%d rx:%d %ld ", txPin, rxPin, testBaudRate);
        uint8_t status = uart_rx_program_test(rxPio, rxSmIndx, 0x0a, 0x80, maximumScanTimeMs, numcharsConsideringValid);
//        printf(" uart: %d\n", status);
        setBaudRate(baudrate);
        return status == 0;
    }
    return false;
}

/**
 * Find a baudrate where the uart is sending data on
 */
uint32_t PioSerial::findBaudRate(uint32_t maxTimeOutMs)
{
    for (uint32_t baudRate : commonBaudrates)
    {
        //  printf("Scanning %ldBd\n", baudRate);
        if (testUartAtBaudrate(baudRate, maxTimeOutMs))
        {
            return baudRate;
        }
    }
    return 0;
}

void PioSerial::rxFlush()
{
    uart_rx_flush(rxPio, rxSmIndx);
}
