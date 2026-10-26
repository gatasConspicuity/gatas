#include <stdio.h>

#include "../bluetooth.hpp"
#include "ace/semaphoreguard.hpp"
#include "ace/recursiveguard.hpp"
#include "ace/measure.hpp"
#include "ace/coreutils.hpp"
#include "ace/debug.hpp"
#include "gatas_gatt.h"

#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "pico/btstack_flash_bank.h"

#include "hci_event_builder.h"
#include "hci_dump_embedded_stdout.h"

#include "etl/algorithm.h"

GATAS::PostConstruct Bluetooth::postConstruct()
{
    bufferMutex = xSemaphoreCreateMutex();
    if (bufferMutex == nullptr)
    {
        return GATAS::PostConstruct::MUTEX_ERROR;
    }
    GATAS_REGISTER_MUTEX(instance->bufferMutex, "Bluetooth_bufferMutex");

    return GATAS::PostConstruct::OK;
}

void Bluetooth::start()
{
    l2cap_init();

    // setup SM: Display only
    sm_init();

    // setup ATT server
    att_server_init(profile_data, nullptr, attWriteCallback);

    // setup GATT Client
    gatt_client_init();
    gatt_client_mtu_enable_auto_negotiation(0);

    // setup advertisements
    uint16_t adv_int_min = 6;  // 0x0030; change dto 6/12 for possible fix Android very quick disconnect
    uint16_t adv_int_max = 12; // 0x0030;
    uint8_t adv_type = 0;
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(advertiseData.size(), advertiseData.data());
    gap_scan_response_set_data(scanResponseData.size(), scanResponseData.data());
    gap_advertisements_enable(1);

    gap_set_local_name(localName.c_str());

    smEventCallback.callback = &smPacketHandler;
    sm_add_event_handler(&smEventCallback);

    instance->heartbeat.process = &heartbeat_handler;
    btstack_run_loop_set_timer(&instance->heartbeat, IDLE_HEARTBEAT_MS);
    btstack_run_loop_add_timer(&instance->heartbeat);

    // Enable mandatory authentication for GATT Client
    // - if un-encrypted connections are not supported, e.g. when connecting to own device, this enforces authentication
    // gatt_client_set_required_security_level(LEVEL_2);

    // LE Legacy Pairing, Just Works
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0 | SM_AUTHREQ_BONDING);

    // register for ATT events
    att_server_register_packet_handler(attPacketHandler);

    // register for HCI events
    hciEventCallback.callback = &hciPacketHandler;
    hci_add_event_handler(&hciEventCallback);

    // Initialize HCI dump to log HCI packets to stdout via printf
#if defined(ENABLE_LOG_INFO) || defined(ENABLE_LOG_DEBUG) || defined(ENABLE_LOG_ERROR)
    hci_dump_init(hci_dump_embedded_stdout_get_instance());
#endif
    hci_power_control(HCI_POWER_ON);

    getBus().subscribe(*this);
};

void Bluetooth::getData(etl::string_stream &stream, const etl::string_view path) const
{
    stream << "{";
    stream << "\"nmeaPortMsgMissed:err\":" << statistics.nmeaPortMsgMissedErr;
    stream << ",\"cobsMsgMissed:err\":" << statistics.cobsMsgMissedErr;
    stream << ",\"connections\":[";
    bool first = true;
    for (auto &it : connections)
    {
        if (!it.inUse)
        {
            continue;
        }
        if (!first)
        {
            stream << ",";
        }
        it.getData(stream, path);
        first = false;
    }
    stream << "]}\n";
}

void Bluetooth::createAdvData()
{
    // clang-format off
    static const uint8_t hm10UartServiceUUID[2] = {0xe0, 0xff};
    // clang-format on
    static constexpr size_t adFieldHeaderSize = 2;

    advertiseData.clear();
    advertiseData.push_back(2); // length
    advertiseData.push_back(BLUETOOTH_DATA_TYPE_FLAGS);
    // https://tinyurl.com/yvvw6avx
    // bit 0 LE Limited Discoverable Mode
    // bit 1 LE General Discoverable Mode
    // bit 2 LE BR/EDR Not Supported. Bit 37 of LMP Feature Mask Definitions (Page 0)
    // bit 3 LE Simultaneous LE and BR/EDR to Same Device Capable (Controller). Bit 49 of LMP Feature Mask Definitions (Page 0)
    // bit 4 Previously Used
    advertiseData.push_back(0x06);

    advertiseData.push_back(1 + sizeof(hm10UartServiceUUID));
    advertiseData.push_back(BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS);
    advertiseData.insert(advertiseData.end(), std::begin(hm10UartServiceUUID), std::end(hm10UartServiceUUID));

    const size_t remainingNameBytes = advertiseData.max_size() - advertiseData.size() - adFieldHeaderSize;
    const size_t advertisedNameLength = etl::min(remainingNameBytes, localName.size());
    if (advertisedNameLength > 0)
    {
        advertiseData.push_back(static_cast<uint8_t>(1 + advertisedNameLength)); // length = type + name length
        advertiseData.push_back(advertisedNameLength == localName.size() ? BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME : BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME);
        advertiseData.insert(advertiseData.end(), localName.begin(), localName.begin() + advertisedNameLength);
    }
}

void Bluetooth::createScanResponseData()
{
    // clang-format off
    static const uint8_t nordicUartServiceUUID[16] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e};
    // clang-format on
    static constexpr size_t adFieldHeaderSize = 2;
    const size_t serviceFieldSize = sizeof(nordicUartServiceUUID) + adFieldHeaderSize;
    const size_t maxNameLength = scanResponseData.max_size() - serviceFieldSize - adFieldHeaderSize;
    const size_t scanResponseNameLength = etl::min(maxNameLength, localName.size());

    scanResponseData.clear();
    scanResponseData.push_back(1 + sizeof(nordicUartServiceUUID));
    scanResponseData.push_back(BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS);
    scanResponseData.insert(scanResponseData.end(), std::begin(nordicUartServiceUUID), std::end(nordicUartServiceUUID));

    if (scanResponseNameLength > 0)
    {
        scanResponseData.push_back(static_cast<uint8_t>(1 + scanResponseNameLength)); // length = type + name length
        scanResponseData.push_back(scanResponseNameLength == localName.size() ? BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME : BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME);
        scanResponseData.insert(scanResponseData.end(), localName.begin(), localName.begin() + scanResponseNameLength);
    }
}

/**
 * Receive dataport messages and send it to all clients
 * Only process data if there are any actual clients and when all client
 * buffers are empty, send a trigger to the BT thread to process the data.
 */
void Bluetooth::on_receive(const GATAS::DataPortMsg &msg)
{
    if (auto guard = SemaphoreGuard(1000, bufferMutex))
    {
        for (auto &ctx : connections)
        {
            if (!ctx.inUse)
            {
                continue;
            }
            if (!ctx.nmeaWriteBuffer.setString(msg.sentence))
            {
                ctx.nmeaWriteBufferErr += 1;
            }
            // Ensure to always setdirty on incomming data
            ctx.txDirty = true;
        }
    }
    else
    {
        statistics.nmeaPortMsgMissedErr += 1;
    }
}

void Bluetooth::on_receive(const GATAS::GatasConnectTx &msg)
{
    if (!msg.output.usesBluetooth())
    {
        return;
    }

    if (auto guard = SemaphoreGuard(1000, bufferMutex))
    {
        etl::span<const uint8_t> payload(msg.cobsMessage.get(), msg.length);
        for (auto &ctx : connections)
        {
            if (!ctx.inUse)
            {
                continue;
            }
            if (!ctx.cobsWriteBuffer.push(reinterpret_cast<const char *>(payload.data()), payload.size()))
            {
                ctx.cobsWriteBufferErr += 1;
            }
            // Ensure to always setdirty on incomming data
            ctx.txDirty = true;
        }
    }
    else
    {
        statistics.cobsMsgMissedErr += 1;
    }
}
void Bluetooth::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

bool Bluetooth::createConnection(hci_con_handle_t handle, uint16_t mtu)
{
    auto it = instance->freeCtx();
    if (it != instance->connections.end())
    {
        it->activate(handle, mtu);
        return true;
    }
    return false;
}

void Bluetooth::removeConnection(uint16_t hciHandle)
{
    auto it = instance->ctxByHandle(hciHandle);
    if (it != instance->connections.end())
    {
        it->deactivate();
    }
}

bool Bluetooth::sendNMEABuffer(BtContext &ctx)
    {
    if (ctx.nmeaAttrHandle == 0 || !ctx.inUse)
            {
        return false;
    }

    etl::span<uint8_t> data;
    if (auto guard = SemaphoreGuard(1000, instance->bufferMutex))
    {
        ctx.nmeaWriteBuffer.read(data, ctx.mtu);
    }
    else
    {
        return false;
    }

    if (data.size() == 0)
    {
        return false;
    }

    const uint8_t sendStatus = att_server_notify(ctx.hciHandle, ctx.nmeaAttrHandle, data.data(), data.size());

    if (sendStatus == ERROR_CODE_SUCCESS) {
        if (auto guard = SemaphoreGuard(1000, instance->bufferMutex))
        {
            ctx.nmeaWriteBuffer.compact();
            ctx.nmeaWriteBuffer.used();
        }
    }

    GATAS_VERIFY(sendStatus == ERROR_CODE_SUCCESS, "Bluetooth: Send Failed");
    return true;
}

bool Bluetooth::sendCobsBuffer(BtContext &ctx)
{
    if (ctx.binaryAttrHandle == 0 || !ctx.inUse)
    {
        return false;
    }

    CircularBuffer<CONNECTIONS_BUFFER_SIZE>::PeekResult peek{};
    if (auto guard = SemaphoreGuard(1000, instance->bufferMutex))
    {
        peek = ctx.cobsWriteBuffer.peek();
    }
    else
    {
        return false;
    }

    if (peek.size == 0 || peek.part == nullptr)
    {
        return false;
    }

    const size_t sendSize = etl::min(static_cast<size_t>(ctx.mtu), peek.size);
    const uint8_t sendStatus = att_server_notify(ctx.hciHandle,  ctx.binaryAttrHandle,  reinterpret_cast<const uint8_t *>(peek.part), sendSize);

    if (sendStatus == ERROR_CODE_SUCCESS)
    {
        if (auto guard = SemaphoreGuard(1000, instance->bufferMutex))
        {
            ctx.cobsWriteBuffer.accepted(sendSize);
        }
    }

    GATAS_VERIFY(sendStatus == ERROR_CODE_SUCCESS, "Bluetooth: Send Failed");
    return true;
}

void Bluetooth::receiveNMEA(BtContext &ctx, uint8_t *buffer, uint16_t bufferSize)
{
    ctx.nmeaGulp.setRef(etl::span<uint8_t>(buffer, bufferSize));

    etl::span<uint8_t> sentence;
    while (ctx.nmeaGulp.pop_into(sentence))
    {
        if (sentence.empty())
        {
            continue;
        }
        instance->statistics.nmeaMsgReceived += 1;
        GATAS::NMEAString nmeaSentence;
        const auto length = etl::min(sentence.size(), static_cast<size_t>(GATAS::NMEA_MAX_LENGTH - 3));
        const auto *begin = reinterpret_cast<const char *>(sentence.data());
        nmeaSentence.assign(begin, begin + length);
        GATAS_MEASURE("GATAS::DataPortMsg", 100);
        instance->getBus().receive(GATAS::DataPortMsg{nmeaSentence});
    }
}

bool Bluetooth::hasPendingData(const BtContext &ctx)
{
    const size_t minimumSendSize = etl::min(static_cast<size_t>(MINIMUM_BLE_PACKET_SIZE), static_cast<size_t>(ctx.mtu));

    if (auto guard = SemaphoreGuard(1000, instance->bufferMutex))
    {
        if (ctx.binaryAttrHandle != 0 && ctx.cobsWriteBuffer.length() >= minimumSendSize)
        {
            return true;
        }

        if (ctx.nmeaAttrHandle != 0 && ctx.nmeaWriteBuffer.used() >= minimumSendSize)
        {
            return true;
        }
    }
    return false;
}

void Bluetooth::requestSendIfPending(BtContext &ctx)
{
    if (!ctx.inUse)
    {
        return;
    }

    if (hasPendingData(ctx))
    {
        // Call BTstack outside the buffer mutex: it may invoke the callback immediately.
        att_server_request_to_send_notification(&ctx.attCallback, ctx.hciHandle);
    }
}

// RecursiveGuard: attContextCallback
// assertion "pxQueue->uxItemSize == 0" failed: file "/Volumes/ext/pico/FreeRTOS-Kernel/queue.c", line 1351, function: xQueueGiveFromISR

void Bluetooth::attContextCallback(void *context)
{
    auto btContext = static_cast<Bluetooth::BtContext *>(context);

    // When calling att_server_request_to_send_notification, it might be it will directly call back om
    // this functions, when that happens we only allow 8 recursive calls else there will be stack overflow issues.

    // 10-05-2026 Lower from 8 to 7 after adding B lueTooth CObs MEssages
    auto rGuard = RecursiveGuard<6>(btContext->guardCounter, "RecursiveGuard: attContextCallback");
    if (!rGuard)
    {
        return;
    }

    bool sent = sendCobsBuffer(*btContext);
    if (!sent)
    {
        sent = sendNMEABuffer(*btContext);
    }

    if (!sent)
    {
        return;
    }

    requestSendIfPending(*btContext);
}

void Bluetooth::heartbeat_handler(struct btstack_timer_source *ts)
{
    (void)ts;

    uint32_t delay = IDLE_HEARTBEAT_MS;
    for (auto &ctx : instance->connections)
    {
        if (!ctx.inUse)
        {
            continue;
        }

        delay = ACTIVE_HEARTBEAT_MS;
        // txDirty is a best-effort trigger for streaming data.
        // If one edge is missed, next heartbeat/new data will pick it up.
        if (ctx.txDirty)
        {
            ctx.txDirty = false;
            requestSendIfPending(ctx);
        }
    }

    btstack_run_loop_set_timer(&instance->heartbeat, delay);
    btstack_run_loop_add_timer(&instance->heartbeat);
}

void Bluetooth::attPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    switch (packet_type)
    {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet))
        {
        case ATT_EVENT_CONNECTED:
        {
            auto handle = att_event_connected_get_handle(packet);
            auto mtu = att_server_get_mtu(handle) - 4;
            if (createConnection(handle, mtu))
            {
                GATAS_INFO("ATT_EVENT_CONNECTED Handle:%d MTU:%d\n", handle, mtu);
                gatt_client_send_mtu_negotiation(gattClientPacketHandler, handle);
                // Only re-advertise when it's possible to accept new connections
                if (instance->hasFreeConnectionSlot())
                {
                    hci_send_cmd(&hci_le_set_advertise_enable, 1);
                }
            }
            else
            {
                gap_disconnect(handle);
            }
        }
        break;
        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        {
            // clang-format off
            Bluetooth::withHandle(att_event_mtu_exchange_complete_get_handle(packet),
                etl::delegate<void(BtContext &)>::create([packet](BtContext &ctx)
                {
                    // We remove minus 16 because of additional header data that needs to fit
                    // This is different from what I was reading in the documentation, but this worked for us.
                    ctx.mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 16;
                })
            );
            // clang-format on
        }
        break;

        case ATT_EVENT_DISCONNECTED:
        {
            GATAS_INFO("ATT_EVENT_DISCONNECTED");
            Bluetooth::removeConnection(att_event_disconnected_get_handle(packet));
        }
        break;

        default:
            break;
        }
        break;
    default:
        break;
    }
}

void Bluetooth::gattClientPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != GATT_EVENT_MTU)
    {
        return;
    }

    const auto handle = gatt_event_mtu_get_handle(packet);
    const auto mtu = gatt_event_mtu_get_MTU(packet);
    GATAS_INFO("GATT_EVENT_MTU Handle:%d MTU:%d\n", handle, mtu);

    Bluetooth::withHandle(handle,
        etl::delegate<void(BtContext &)>::create([mtu](BtContext &ctx)
        {
            // Retain the established compatibility margin used after peer-initiated MTU exchange.
            ctx.mtu = mtu - 16;
        }));
}

void Bluetooth::hciPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet))
    {
    case HCI_EVENT_META_GAP:
        switch (hci_event_gap_meta_get_subevent_code(packet))
        {
        case GAP_SUBEVENT_LE_CONNECTION_COMPLETE:
        {
            hci_con_handle_t con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
            // request min con interval 15 ms for iOS 11+
            gap_request_connection_parameter_update(con_handle, 12, 12, 4, 0x0048);
            break;
        }
        default:
            break;
        }
        break;
    default:
        break;
    }
}

int Bluetooth::attWriteCallback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    UNUSED(offset);
    UNUSED(buffer_size);
    if (transaction_mode != ATT_TRANSACTION_MODE_NONE || transaction_mode == ATT_TRANSACTION_MODE_CANCEL)
    {
        return 0;
    }
    switch (att_handle)
    {
    case ATT_CHARACTERISTIC_FFE1_01_CLIENT_CONFIGURATION_HANDLE:
    case ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_CLIENT_CONFIGURATION_HANDLE:
    {
        uint16_t nmeaValueHandle = ATT_CHARACTERISTIC_FFE1_01_VALUE_HANDLE;
        if (att_handle == ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_CLIENT_CONFIGURATION_HANDLE)
        {
            nmeaValueHandle = ATT_CHARACTERISTIC_6E400003_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE;
        }

        // clang-format off
        Bluetooth::withHandle(con_handle,
            etl::delegate<void(BtContext &)>::create([buffer, nmeaValueHandle](BtContext &ctx)
            {
                const bool enabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
                if (enabled)
                {
                    ctx.nmeaAttrHandle = nmeaValueHandle;
                    att_server_request_to_send_notification(&ctx.attCallback, ctx.hciHandle);
                }
                else if (ctx.nmeaAttrHandle == nmeaValueHandle)
                {
                    ctx.nmeaAttrHandle = 0;
                }
            }));
        // clang-format on
    }
    break;

    // Binary COBS CCCD: track whether the remote side enabled notifications for the GatasConnect stream.
    case ATT_CHARACTERISTIC_FFE2_01_CLIENT_CONFIGURATION_HANDLE:
    {
        // GATAS_INFO("ATT_CHARACTERISTIC_0000ffe2_0000_1000_8000_00805f9b34fb_01_CLIENT_CONFIGURATION_HANDLE");
        // clang-format off
        Bluetooth::withHandle(con_handle,
            etl::delegate<void(BtContext &)>::create([buffer](BtContext &ctx)
            {
                const bool enabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
                if (enabled)
                {
                    ctx.binaryAttrHandle = ATT_CHARACTERISTIC_FFE2_01_VALUE_HANDLE;
                }
                else if (ctx.binaryAttrHandle == ATT_CHARACTERISTIC_FFE2_01_VALUE_HANDLE)
                {
                    ctx.binaryAttrHandle = 0;
                }
            }));
        // clang-format on
    }
    break;

    // Binary COBS value: receive framed GatasConnect payloads and forward them into the message bus.
    case ATT_CHARACTERISTIC_FFE2_01_VALUE_HANDLE:
    {
        // GATAS_INFO("ATT_CHARACTERISTIC_0000ffe2_0000_1000_8000_00805f9b34fb_01_VALUE_HANDLE");
        // clang-format off
        Bluetooth::withHandle(con_handle,
            etl::delegate<void(BtContext &)>::create([buffer, buffer_size](BtContext &ctx)
            {
                ctx.binaryGulp.setRef(etl::span<uint8_t>(buffer, buffer_size));

                etl::span<uint8_t> payload;
                while (ctx.binaryGulp.pop_into(payload))
                {
                    if (payload.empty())
                    {
                        continue;
                    }

                    auto &pool = BaseModule::getGlobalPool();
                    // TODO: We need to add in gatasCompanion a extra 0 to be send
                    auto *copy = static_cast<uint8_t *>(pool.alloc(payload.size() + 1));
                    if (copy == nullptr)
                    {
                        Bluetooth::instance->statistics.cobsMsgMissedErr += 1;
                        continue;
                    }
                    Bluetooth::instance->statistics.cobsMsgReceived += 1;
                    memcpy(copy, payload.data(), payload.size());
                    // Adding a null terminator because it's expected downstream
                    copy[payload.size()] = 0;
                    Bluetooth::instance->getBus().receive(GATAS::GatasConnectRx(pool, GATAS::GatasConnectTransport::Bluetooth, copy, payload.size() + 1));
                }
            }));
        // clang-format on
    }
    break;

    // Nordic UART RX and HM-10 NMEA values share the same plain-text DataPort handling.
    case ATT_CHARACTERISTIC_FFE1_01_VALUE_HANDLE:
    case ATT_CHARACTERISTIC_6E400002_B5A3_F393_E0A9_E50E24DCCA9E_01_VALUE_HANDLE:
    {
        // clang-format off
        Bluetooth::withHandle(con_handle,
            etl::delegate<void(BtContext &)>::create([buffer, buffer_size](BtContext &ctx)
            {
                Bluetooth::receiveNMEA(ctx, buffer, buffer_size);
            }));
        // clang-format on
    }
    break;
    default:
        break;
    }
    return 0;
}

/*
 * @section Security Manager Packet Handler
 *
 * @text The packet handler is used to handle Security Manager events
 */

/* LISTING_START(packetHandler): Security Manager Packet Handler */
void Bluetooth::smPacketHandler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    bd_addr_t addr;
    bd_addr_type_t addr_type;

    switch (hci_event_packet_get_type(packet))
    {
    case SM_EVENT_JUST_WORKS_REQUEST:
        // printf("Just Works requested\n");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
        // printf("Confirming numeric comparison: %lu\n", sm_event_numeric_comparison_request_get_passkey(packet));
        sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
        // printf("Display Passkey: %lu\n", sm_event_passkey_display_number_get_passkey(packet));
        break;
    case SM_EVENT_IDENTITY_CREATED:
        sm_event_identity_created_get_identity_address(packet, addr);
        // printf("Identity created: type %u address %s\n", sm_event_identity_created_get_identity_addr_type(packet), bd_addr_to_str(addr));
        break;
    case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
        sm_event_identity_resolving_succeeded_get_identity_address(packet, addr);
        // printf("Identity resolved: type %u address %s\n", sm_event_identity_resolving_succeeded_get_identity_addr_type(packet), bd_addr_to_str(addr));
        break;
    case SM_EVENT_IDENTITY_RESOLVING_FAILED:
        sm_event_identity_created_get_address(packet, addr);
        // printf("Identity resolving failed\n");
        break;
    // case SM_EVENT_PAIRING_STARTED:
    //     printf("Pairing started\n");
    //     break;
    // case SM_EVENT_PAIRING_COMPLETE:
    //     switch (sm_event_pairing_complete_get_status(packet))
    //     {
    //     case ERROR_CODE_SUCCESS:
    //         printf("Pairing complete, success\n");
    //         break;
    //     case ERROR_CODE_CONNECTION_TIMEOUT:
    //         printf("Pairing failed, timeout\n");
    //         break;
    //     case ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION:
    //         printf("Pairing failed, disconnected\n");
    //         break;
    //     case ERROR_CODE_AUTHENTICATION_FAILURE:
    //         printf("Pairing failed, authentication failure with reason = %u\n", sm_event_pairing_complete_get_reason(packet));
    //         break;
    //     default:
    //         break;
    //     }
    //     break;
    case SM_EVENT_REENCRYPTION_STARTED:
        sm_event_reencryption_complete_get_address(packet, addr);
        GATAS_INFO("Bonding information exists for addr type %u, identity addr %s -> re-encryption started\n", sm_event_reencryption_started_get_addr_type(packet), bd_addr_to_str(addr));
        break;
    case SM_EVENT_REENCRYPTION_COMPLETE:
        switch (sm_event_reencryption_complete_get_status(packet))
        {
        // case ERROR_CODE_SUCCESS:
        //     printf("Re-encryption complete, success\n");
        //     break;
        // case ERROR_CODE_CONNECTION_TIMEOUT:
        //     printf("Re-encryption failed, timeout\n");
        //     break;
        // case ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION:
        //     printf("Re-encryption failed, disconnected\n");
        //     break;
        case ERROR_CODE_PIN_OR_KEY_MISSING:
            GATAS_INFO("Re-encryption failed, bonding information missing");
            GATAS_INFO("Assuming remote lost bonding information");
            GATAS_INFO("Deleting local bonding information to allow for new pairing...");
            sm_event_reencryption_complete_get_address(packet, addr);
            addr_type = (bd_addr_type_t)(sm_event_reencryption_started_get_addr_type(packet));
            gap_delete_bonding(addr_type, addr);
            break;
        default:
            break;
        }
        break;
    // case GATT_EVENT_QUERY_COMPLETE:
    // {
    //     auto status = gatt_event_query_complete_get_att_status(packet);
    //     switch (status)
    //     {
    //     case ATT_ERROR_INSUFFICIENT_ENCRYPTION:
    //         printf("GATT Query failed, Insufficient Encryption\n");
    //         break;
    //     case ATT_ERROR_INSUFFICIENT_AUTHENTICATION:
    //         printf("GATT Query failed, Insufficient Authentication\n");
    //         break;
    //     case ATT_ERROR_BONDING_INFORMATION_MISSING:
    //         printf("GATT Query failed, Bonding Information Missing\n");
    //         break;
    //     case ATT_ERROR_SUCCESS:
    //         printf("GATT Query successful\n");
    //         break;
    //     default:
    //         printf("GATT Query failed, status 0x%02x\n", gatt_event_query_complete_get_att_status(packet));
    //         break;
    //     }
    // }
    // break;
    default:
        break;
    }
}
