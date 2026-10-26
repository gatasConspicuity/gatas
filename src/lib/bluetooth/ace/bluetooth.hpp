#pragma once

#include <stdint.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"

/* ETLCPP */
#include "etl/message_bus.h"
#include "etl/algorithm.h"
#include "etl/array.h"
#include "etl/string.h"

/* GaTas */
#include "ace/constants.hpp"
#include "ace/basemodule.hpp"
#include "ace/binarymessages.hpp"
#include "ace/gulp.hpp"
#include "ace/messages.hpp"
#include "ace/packetbuffer.hpp"
#include "ace/circularbuffer.hpp"

/* BT Stack*/
#include "btstack.h"

/**
 * Bluetooth transport for GATAS.
 *
 * The Bluetooth module keeps the NMEA and binary COBS traffic separated into
 * two characteristics. Binary payloads are carried through GatasConnectTx /
 * GatasConnectRx while NMEA continues to use DataPortMsg.
 */
class Bluetooth : public BaseModule, public etl::message_router<Bluetooth, GATAS::DataPortMsg, GATAS::GatasConnectTx>
{
    static constexpr uint16_t CONNECTIONS_BUFFER_SIZE = 2048; // TODO: Tune buffer, should be > MTU which is 255 bytes for BLE witj etxnded data length
    static constexpr uint8_t MINIMUM_BLE_PACKET_SIZE = 180;   // Minimum size of a BLE packet, to better use the BLE bandwith
    static constexpr uint32_t IDLE_HEARTBEAT_MS = 2000;
    static constexpr uint32_t ACTIVE_HEARTBEAT_MS = 100;

    inline static Bluetooth *instance;

    // advertisement and scan response data, max 31 bytes each
    etl::vector<uint8_t, 31> advertiseData;
    etl::vector<uint8_t, 31> scanResponseData;

    friend class message_router;
    struct
    {
        uint32_t nmeaPortMsgMissedErr = 0;
        uint32_t cobsMsgMissedErr = 0;
        uint32_t cobsMsgReceived = 0;
        uint32_t nmeaMsgReceived = 0;
    } statistics;

    using TxBuffer = PacketBuffer<CONNECTIONS_BUFFER_SIZE, (CONNECTIONS_BUFFER_SIZE / 32) * 2>;
    using CobsTxBuffer = CircularBuffer<CONNECTIONS_BUFFER_SIZE>;

    struct BtContext
    {
        bool inUse = false;
        // Best-effort "work pending" hint for streaming TX.
        // This is intentionally lossy (not a strict synchronization flag):
        // missing a set/read can delay one cycle, but heartbeat re-checks and
        // new stream data keeps retriggering sends.
        bool txDirty = false;
        uint8_t guardCounter = 0;
        hci_con_handle_t hciHandle = 0;
        uint16_t mtu = 0;
        uint16_t nmeaAttrHandle = 0;
        uint16_t binaryAttrHandle = 0;
        uint32_t nmeaWriteBufferErr = 0;
        uint32_t cobsWriteBufferErr = 0;
        btstack_context_callback_registration_t attCallback;

        // Per-connection NMEA stream splitter; keeps partial sentences across BLE writes.
        etl::vector<uint8_t, GATAS::NMEA_MAX_LENGTH> nmeaGulpBuffer;
        Gulp nmeaGulp{nmeaGulpBuffer, DelimiterBitmap::CRLF()};
        TxBuffer nmeaWriteBuffer;

        // Per-connection COBS stream splitter; keeps partial binary frames across BLE writes.
        etl::vector<uint8_t, BinaryMessages::MAX_COBS_FRAME_SIZE> binaryGulpBuffer;
        Gulp binaryGulp{binaryGulpBuffer, DelimiterBitmap::Null()};
        CobsTxBuffer cobsWriteBuffer;

        BtContext()
        {
        }

        void configureCallbacks(void (*attCallback_)(void *context))
        {
            attCallback.context = this;
            attCallback.callback = attCallback_;
        }

        void activate(hci_con_handle_t hciHandle_, uint16_t mtu_)
        {
            txDirty = false;
            hciHandle = hciHandle_;
            mtu = mtu_;
            nmeaAttrHandle = 0;
            binaryAttrHandle = 0;
            nmeaWriteBufferErr = 0;
            cobsWriteBufferErr = 0;
            guardCounter = 0;
            nmeaGulpBuffer.clear();
            nmeaGulp.setRef({});
            nmeaWriteBuffer.clear();
            binaryGulpBuffer.clear();
            binaryGulp.setRef({});
            cobsWriteBuffer.clear();
            inUse = true;
        }

        void deactivate()
        {
            inUse = false;
            txDirty = false;
            hciHandle = 0;
            mtu = 0;
            nmeaAttrHandle = 0;
            binaryAttrHandle = 0;
            nmeaWriteBufferErr = 0;
            cobsWriteBufferErr = 0;
            guardCounter = 0;
            nmeaGulpBuffer.clear();
            nmeaGulp.setRef({});
            nmeaWriteBuffer.clear();
            binaryGulpBuffer.clear();
            binaryGulp.setRef({});
            cobsWriteBuffer.clear();
        }

        void getData(etl::string_stream &stream, const etl::string_view path) const
        {
            (void)path;
            stream << "{";
            stream << "\"hciHandle\":" << hciHandle;
            stream << ",\"mtu\":" << mtu;
            stream << ",\"nmeaWriteBufferErr\":" << nmeaWriteBufferErr;
            stream << ",\"cobsWriteBufferErr\":" << cobsWriteBufferErr;
            stream << "}";
        }

        // Disallow copy
        BtContext(const BtContext &) = delete;
        BtContext &operator=(const BtContext &) = delete;

        // Disallow move
        BtContext(BtContext &&) = delete;
        BtContext &operator=(BtContext &&) = delete;
    };

private:
    virtual GATAS::PostConstruct postConstruct() override;
    virtual void start() override;

    void on_receive(const GATAS::DataPortMsg &msg);
    void on_receive(const GATAS::GatasConnectTx &msg);
    void on_receive_unknown(const etl::imessage &msg);

    void createAdvData();
    virtual void getData(etl::string_stream &stream, const etl::string_view path) const override;

    // START: methods within this block as running within the BLE task
    static void smPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
    static void attContextCallback(void *context);
    static void attPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
    static void gattClientPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
    static void hciPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
    static int attWriteCallback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);
    // Create a new connection in the connections list
    static bool createConnection(hci_con_handle_t handle, uint16_t mtu);
    // Remove any old connections
    static void removeConnection(uint16_t handle);
    static void heartbeat_handler(struct btstack_timer_source *ts);
    // END: methods within this block as running within the BLE task

    // Lists of bluetooth contexts
    using BluetoothConnections = etl::array<BtContext, GATAS_MAX_BLUETOOTH_CONNECTIONS>;
    BluetoothConnections connections;

    static bool hasFreeConnectionSlot()
    {
        return etl::any_of(instance->connections.begin(), instance->connections.end(),
                           [](const BtContext &ctx)
                           {
                               return !ctx.inUse;
                           });
    }

    /**
     * Get the connections context by Bluetooth handle
     */
    static BluetoothConnections::iterator ctxByHandle(hci_con_handle_t hciHandle)
    {
        // clang-format off
        return etl::find_if(instance->connections.begin(), instance->connections.end(),
            [hciHandle](const BtContext &ctx)
            {
                return ctx.inUse && ctx.hciHandle == hciHandle;
            });
        // clang-format on
    }

    static BluetoothConnections::iterator freeCtx()
    {
        return etl::find_if(instance->connections.begin(), instance->connections.end(),
                            [](const BtContext &ctx)
                            {
                                return !ctx.inUse;
                            });
    }

    /**
     * Call back a provided lambda with the context of the connection if found.
     * ote; Ensure to not call any BT API's or other time consuming calls
     */
    using BtContextCallback = etl::delegate<void(BtContext &)>;
    static void withHandle(hci_con_handle_t conn_handle, BtContextCallback callback)
    {
        auto it = ctxByHandle(conn_handle);
        if (it != instance->connections.end())
        {
            callback(*it);
        }
    }

    btstack_packet_callback_registration_t hciEventCallback;
    btstack_packet_callback_registration_t smEventCallback;
    btstack_timer_source_t heartbeat;
    uint8_t spp_service_buffer[100]; // SPP (Serial Port Profile) Showed as length to 91
    GATAS::SsidOrPasswdStr localName;

    SemaphoreHandle_t bufferMutex;

    static bool sendNMEABuffer(BtContext &ctx);
    static bool sendCobsBuffer(BtContext &ctx);
    static void receiveNMEA(BtContext &ctx, uint8_t *buffer, uint16_t bufferSize);
    static bool hasPendingData(const BtContext &ctx);
    static void requestSendIfPending(BtContext &ctx);
    void createScanResponseData();

public:
    static constexpr const char *NAME = "Bluetooth";
    Bluetooth(etl::imessage_bus &bus, Configuration &config) : BaseModule(bus, NAME), bufferMutex(nullptr)
    {
        instance = this;
        for (auto &ctx : connections)
        {
            ctx.deactivate();
            ctx.configureCallbacks(&Bluetooth::attContextCallback);
        }
        localName = config.strValueByPath("GaTas", NAME, "localName");
        createAdvData();
        createScanResponseData();
    }

    virtual ~Bluetooth() = default;
};
