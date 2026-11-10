#include "../rxdataframequeue.hpp"
#include "ace/manchester.hpp"
#include "ace/moreutils.hpp"
#include "ace/poolallocator.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "etl/algorithm.h"

GATAS::PostConstruct RxDataFrameQueue::postConstruct()
{
    // Task must be lower compared to any SX1262 task
    if (xTaskCreate(radioQueueTaskTrampoline, RxDataFrameQueue::NAME.cbegin(), configMINIMAL_STACK_SIZE + 1024, this, tskIDLE_PRIORITY + 2, &taskHandle) != pdPASS)
    {
        return GATAS::PostConstruct::TASK_ERROR;
    }

    return GATAS::PostConstruct::OK;
}

void RxDataFrameQueue::start()
{
    getBus().subscribe(*this);
};

bool RxDataFrameQueue::push(GATAS::DataFrame &&frame)
{
    if (!dataQueue.push(etl::move(frame)))
    {
        statistics.queueFull += 1;
        return false;
    }

    xTaskNotifyGive(taskHandle);
    return true;
}

void RxDataFrameQueue::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"totalQueued:k\":\"" << statistics.totalQueued << "\",";
    stream << "\"queueFull:err\":\"" << statistics.queueFull << "\"";
    stream << "}";
}

void RxDataFrameQueue::radioQueueTaskTrampoline(void *arg)
{
    RxDataFrameQueue *rxQueue = static_cast<RxDataFrameQueue *>(arg);
    rxQueue->radioQueueTask(arg);
}

//*********************** Tuner tasks ***********************
void RxDataFrameQueue::radioQueueTask(void *arg)
{
    (void)arg;
    GATAS::DataFrame rxFrame;
    while (true)
    {
        if (!dataQueue.pop(rxFrame))
        {
            uint32_t notifyValue = 0;
            xTaskNotifyWait(pdFALSE, ULONG_MAX, &notifyValue, portMAX_DELAY);
            continue;
        }

        statistics.totalQueued += 1;
        if (rxFrame.config->manchester)
        {
            size_t byteLength = rxFrame.length >> 1;
            if (auto errorFrame = static_cast<uint8_t *>(getGlobalPool().alloc(byteLength)))
            {
                auto msg = GATAS::RadioRxManchesterMsg{
                    etl::move(rxFrame.frame),
                    getGlobalPool(),
                    errorFrame,
                    byteLength,
                    rxFrame.epochSeconds,
                    rxFrame.frequency,
                    rxFrame.config->dataSource(),
                    rxFrame.rssidBm};

                manchesterDecodeInline(msg.frame.get(), msg.error.get(), rxFrame.length);

                // Handle multi protocol situations
                // auto ds = decideDataSource(rxFrame.config->dataSource(), msg.frame32(), rxFrame.length );
                // if (ds.dataSource < GATAS::DataSource::_TRANSPROTOCOLS)
                // {
                //     bitShift(
                //         msg.frame,
                //         msg.lengthBytes,
                //         ds.bitsToShift);

                //     bitShift(
                //         msg.error,
                //         msg.lengthBytes,
                //         ds.bitsToShift);

                //     msg.lengthBytes = ds.frameLength;
                //     msg.dataSource = ds.dataSource;
                // }

                getBus().receive(msg);
            }
        }
        else
        {
            getBus().receive(GATAS::RadioRxMsg{
                etl::move(rxFrame.frame),
                rxFrame.length,
                rxFrame.epochSeconds,
                rxFrame.frequency,
                rxFrame.config->dataSource(),
                rxFrame.rssidBm});
        }
    }
}

// ******************** Message bus receive handlers ********************
void RxDataFrameQueue::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

RxDataFrameQueue::DataSourceMatch RxDataFrameQueue::decideDataSource(GATAS::DataSource ds, uint32_t frame[], uint8_t frameLengthBytes) // resolve multi-system receptions into unique types
{
    if (ds == GATAS::DataSource::ADSLOGN) // if OGN+ADSL SYNC
    {
        const uint32_t SignADSL32[] = {0x0080B124};
        const uint32_t MaskADSL32[] = {0x00F0FFFF};
        const uint32_t SignOGN32[] = {0x00602B9B};
        const uint32_t MaskOGN32[] = {0x00F8FFFF};

        if (diffBits<1>(frame, SignADSL32, MaskADSL32) <= 1)
        {
            return {
                .dataSource = GATAS::DataSource::ADSLM,
                .bitsToShift = 20,
                .frameLength = 25 /*CountryRegulations::PROTOCOL_ADSL.packetLength */};
        }
        else if (diffBits<1>(frame, SignOGN32, MaskOGN32) <= 1)
        {
            return {
                .dataSource = GATAS::DataSource::OGN,
                .bitsToShift = 21,
                .frameLength = 26 /*CountryRegulations::PROTOCOL_OGN.packetLength*/};
        }
    }
    if (ds == GATAS::DataSource::ADSLFLARM) // if FLARM+ADSL SYNC
    {
        const uint32_t SignADSL32[] = {0x003096E4};
        const uint32_t MaskADSL32[] = {0x00FEFFFF};
        const uint32_t SignFLR32[] = {0xA22C0000};
        const uint32_t MaskFLR32[] = {0xFFFE0000};
        if (diffBits<1>(frame, SignADSL32, MaskADSL32) <= 1)
        {
            return {
                .dataSource = GATAS::DataSource::ADSLM,
                .bitsToShift = 6,
                .frameLength = 26 /*CountryRegulations::PROTOCOL_ADSL.packetLength*/};
        }
        else if (diffBits<1>(frame, SignFLR32, MaskFLR32) <= 1)
        {
            return {
                .dataSource = GATAS::DataSource::FLARM,
                .bitsToShift = 15,
                .frameLength = 26 /*CountryRegulations::PROTOCOL_FLARM.packetLength*/};
        }
    }

    return {
        .dataSource = ds,
        .bitsToShift = 0,
        .frameLength = frameLengthBytes};
}

uint32_t RxDataFrameQueue::CRCsyndrome(uint8_t Bit)
{
    constexpr uint16_t PacketBytes = 24;
    constexpr uint16_t PacketBits = PacketBytes * 8;
    const uint32_t Syndrome[PacketBits] =
        {
            0x7ABEE1, 0xC2A574, 0x6152BA, 0x30A95D, 0xE7AEAA, 0x73D755, 0xC611AE, 0x6308D7,
            0xCE7E6F, 0x98C533, 0xB3989D, 0xA6364A, 0x531B25, 0xD67796, 0x6B3BCB, 0xCA67E1,
            0x9AC9F4, 0x4D64FA, 0x26B27D, 0xECA33A, 0x76519D, 0xC4D2CA, 0x626965, 0xCECEB6,
            0x67675B, 0xCC49A9, 0x99DED0, 0x4CEF68, 0x2677B4, 0x133BDA, 0x099DED, 0xFB34F2,
            0x7D9A79, 0xC13738, 0x609B9C, 0x304DCE, 0x1826E7, 0xF3E977, 0x860EBF, 0xBCFD5B,
            0xA184A9, 0xAF3850, 0x579C28, 0x2BCE14, 0x15E70A, 0x0AF385, 0xFA83C6, 0x7D41E3,
            0xC15AF5, 0x9F577E, 0x4FABBF, 0xD82FDB, 0x93EDE9, 0xB60CF0, 0x5B0678, 0x2D833C,
            0x16C19E, 0x0B60CF, 0xFA4A63, 0x82DF35, 0xBE959E, 0x5F4ACF, 0xD05F63, 0x97D5B5,
            0xB410DE, 0x5A086F, 0xD2FE33, 0x96851D, 0xB4B88A, 0x5A5C45, 0xD2D426, 0x696A13,
            0xCB4F0D, 0x9A5D82, 0x4D2EC1, 0xD96D64, 0x6CB6B2, 0x365B59, 0xE4D7A8, 0x726BD4,
            0x3935EA, 0x1C9AF5, 0xF1B77E, 0x78DBBF, 0xC397DB, 0x9E31E9, 0xB0E2F0, 0x587178,
            0x2C38BC, 0x161C5E, 0x0B0E2F, 0xFA7D13, 0x82C48D, 0xBE9842, 0x5F4C21, 0xD05C14,
            0x682E0A, 0x341705, 0xE5F186, 0x72F8C3, 0xC68665, 0x9CB936, 0x4E5C9B, 0xD8D449,
            0x939020, 0x49C810, 0x24E408, 0x127204, 0x093902, 0x049C81, 0xFDB444, 0x7EDA22,
            0x3F6D11, 0xE04C8C, 0x702646, 0x381323, 0xE3F395, 0x8E03CE, 0x4701E7, 0xDC7AF7,
            0x91C77F, 0xB719BB, 0xA476D9, 0xADC168, 0x56E0B4, 0x2B705A, 0x15B82D, 0xF52612,
            0x7A9309, 0xC2B380, 0x6159C0, 0x30ACE0, 0x185670, 0x0C2B38, 0x06159C, 0x030ACE,
            0x018567, 0xFF38B7, 0x80665F, 0xBFC92B, 0xA01E91, 0xAFF54C, 0x57FAA6, 0x2BFD53,
            0xEA04AD, 0x8AF852, 0x457C29, 0xDD4410, 0x6EA208, 0x375104, 0x1BA882, 0x0DD441,
            0xF91024, 0x7C8812, 0x3E4409, 0xE0D800, 0x706C00, 0x383600, 0x1C1B00, 0x0E0D80,
            0x0706C0, 0x038360, 0x01C1B0, 0x00E0D8, 0x00706C, 0x003836, 0x001C1B, 0xFFF409,
            0x800000, 0x400000, 0x200000, 0x100000, 0x080000, 0x040000, 0x020000, 0x010000,
            0x008000, 0x004000, 0x002000, 0x001000, 0x000800, 0x000400, 0x000200, 0x000100,
            0x000080, 0x000040, 0x000020, 0x000010, 0x000008, 0x000004, 0x000002, 0x000001};
    return Syndrome[Bit];
}

uint8_t RxDataFrameQueue::FindCRCsyndrome(uint32_t Syndr) // quick search for a single-bit CRC syndrome
{
    constexpr uint16_t PacketBytes = 24;
    constexpr uint16_t PacketBits = PacketBytes * 8;
    const uint32_t Syndrome[] =
        {
            0x000001BF, 0x000002BE, 0x000004BD, 0x000008BC, 0x000010BB, 0x000020BA, 0x000040B9, 0x000080B8,
            0x000100B7, 0x000200B6, 0x000400B5, 0x000800B4, 0x001000B3, 0x001C1BA6, 0x002000B2, 0x003836A5,
            0x004000B1, 0x00706CA4, 0x008000B0, 0x00E0D8A3, 0x010000AF, 0x01856788, 0x01C1B0A2, 0x020000AE,
            0x030ACE87, 0x038360A1, 0x040000AD, 0x049C816D, 0x06159C86, 0x0706C0A0, 0x080000AC, 0x0939026C,
            0x099DED1E, 0x0AF3852D, 0x0B0E2F5A, 0x0B60CF39, 0x0C2B3885, 0x0DD44197, 0x0E0D809F, 0x100000AB,
            0x1272046B, 0x133BDA1D, 0x15B82D7E, 0x15E70A2C, 0x161C5E59, 0x16C19E38, 0x1826E724, 0x18567084,
            0x1BA88296, 0x1C1B009E, 0x1C9AF551, 0x200000AA, 0x24E4086A, 0x2677B41C, 0x26B27D12, 0x2B705A7D,
            0x2BCE142B, 0x2BFD538F, 0x2C38BC58, 0x2D833C37, 0x304DCE23, 0x30A95D03, 0x30ACE083, 0x34170561,
            0x365B594D, 0x37510495, 0x38132373, 0x3836009D, 0x3935EA50, 0x3E44099A, 0x3F6D1170, 0x400000A9,
            0x457C2992, 0x4701E776, 0x49C81069, 0x4CEF681B, 0x4D2EC14A, 0x4D64FA11, 0x4E5C9B66, 0x4FABBF32,
            0x531B250C, 0x56E0B47C, 0x579C282A, 0x57FAA68E, 0x58717857, 0x5A086F41, 0x5A5C4545, 0x5B067836,
            0x5F4ACF3D, 0x5F4C215E, 0x609B9C22, 0x6152BA02, 0x6159C082, 0x62696516, 0x6308D707, 0x67675B18,
            0x682E0A60, 0x696A1347, 0x6B3BCB0E, 0x6CB6B24C, 0x6EA20894, 0x70264672, 0x706C009C, 0x726BD44F,
            0x72F8C363, 0x73D75505, 0x76519D14, 0x78DBBF53, 0x7A930980, 0x7ABEE100, 0x7C881299, 0x7D41E32F,
            0x7D9A7920, 0x7EDA226F, 0x800000A8, 0x80665F8A, 0x82C48D5C, 0x82DF353B, 0x860EBF26, 0x8AF85291,
            0x8E03CE75, 0x91C77F78, 0x93902068, 0x93EDE934, 0x96851D43, 0x97D5B53F, 0x98C53309, 0x99DED01A,
            0x9A5D8249, 0x9AC9F410, 0x9CB93665, 0x9E31E955, 0x9F577E31, 0xA01E918C, 0xA184A928, 0xA476D97A,
            0xA6364A0B, 0xADC1687B, 0xAF385029, 0xAFF54C8D, 0xB0E2F056, 0xB3989D0A, 0xB410DE40, 0xB4B88A44,
            0xB60CF035, 0xB719BB79, 0xBCFD5B27, 0xBE959E3C, 0xBE98425D, 0xBFC92B8B, 0xC1373821, 0xC15AF530,
            0xC2A57401, 0xC2B38081, 0xC397DB54, 0xC4D2CA15, 0xC611AE06, 0xC6866564, 0xCA67E10F, 0xCB4F0D48,
            0xCC49A919, 0xCE7E6F08, 0xCECEB617, 0xD05C145F, 0xD05F633E, 0xD2D42646, 0xD2FE3342, 0xD677960D,
            0xD82FDB33, 0xD8D44967, 0xD96D644B, 0xDC7AF777, 0xDD441093, 0xE04C8C71, 0xE0D8009B, 0xE3F39574,
            0xE4D7A84E, 0xE5F18662, 0xE7AEAA04, 0xEA04AD90, 0xECA33A13, 0xF1B77E52, 0xF3E97725, 0xF526127F,
            0xF9102498, 0xFA4A633A, 0xFA7D135B, 0xFA83C62E, 0xFB34F21F, 0xFDB4446E, 0xFF38B789, 0xFFF409A7};

    uint16_t Bot = 0;
    uint16_t Top = PacketBits;
    uint32_t MidSyndr = 0;
    for (;;)
    {
        uint16_t Mid = (Bot + Top) >> 1;
        MidSyndr = Syndrome[Mid] >> 8;
        if (Syndr == MidSyndr)
        {
            return (uint8_t)Syndrome[Mid];
        }
        if (Mid == Bot)
        {
            break;
        }
        if (Syndr < MidSyndr)
        {
            Top = Mid;
        }
        else
        {
            Bot = Mid;
        }
    }
    return 0xFF;
}
