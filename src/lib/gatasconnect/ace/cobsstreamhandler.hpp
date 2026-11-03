#pragma once
#include <cstddef>
#include "ace/cobs.hpp"
#include "ace/debug.hpp"
#include "ace/gulp.hpp"
#include "ace/binarymessages.hpp"
#include "ace/messages.hpp"
#include "ace/ownshipstate.hpp"
#include "etl/span.h"
#include "etl/vector.h"

/**
 * Handle incoming COBS messages and call the correct function the handle the datasets
 */
class CobsStreamHandler
{
private:
    etl::imessage_bus &bus;
    GATAS::OwnshipState &ownshipState;
    // gulpBuffer needs to be at least the size of one array of length ending with DelimiterBitmap
    etl::vector<uint8_t, 64> gulpBuffer;
    Gulp gulp;
    GATAS::GatasConnectTransport lastTransport = GATAS::GatasConnectTransport::UDP;

public:
    CobsStreamHandler(etl::imessage_bus &bus_, GATAS::OwnshipState &ownshipState_) : bus(bus_), ownshipState(ownshipState_), gulp(gulpBuffer, DelimiterBitmap::Null())
    {
    }

    void handle(float ownShipLat, float ownShipLon, etl::span<uint8_t> cobsBuffer, GATAS::GatasConnectTransport transport = GATAS::GatasConnectTransport::UDP, uint8_t bootId = 0, bool allowNonDeviceActionFrames = true)
    {
        // Never finish a buffered frame using another transport.
        // Bluetooth already assembles complete frames separately per connection.
        if (lastTransport != transport)
        {
            gulp.erase();
            lastTransport = transport;
        }
        gulp.setRef(cobsBuffer);
        etl::vector<GATAS::AircraftPositionInfo, GATAS::IngressAircraftPositionsMsg::MAX_POSITIONS> positionMessages;
        auto addPosition = [&](const GATAS::AircraftPositionInfo &position)
        {

#if GATAS_DEBUG == 1
            if (position.dataSource != GATAS::DataSource::ADSB && position.dataSource != GATAS::DataSource::MLAT)
            {
                GATAS_WARN("CobsStreamHandler: ignoring unexpected aircraft data source %u",
                           static_cast<unsigned int>(position.dataSource));
                return;
            }
#endif
            if (positionMessages.full())
            {
                bus.receive(GATAS::IngressAircraftPositionsMsg(positionMessages));
                positionMessages.clear();
            }
            positionMessages.push_back(position);
        };

        etl::span<uint8_t> data;
        bool cobsMessageProcessed = false;
        (void) cobsMessageProcessed;
        while (gulp.pop_into(data))
        {
            cobsMessageProcessed = true;
            const size_t decodedSize = decodeCOBS_inplace(data);
            if (decodedSize == 0)
            {
                continue;
            }
            data = data.first(decodedSize);
            uint8_t frameType = data[0];
            etl::bit_stream_reader reader(data, etl::endian::big);

            if (frameType == BinaryMessages::DataType::DEVICE_ACTION_REQUEST_V1)
            {
                if (transport == GATAS::GatasConnectTransport::Bluetooth)
                {
                    if (const auto request = BinaryMessages::deserializeDeviceActionRequestV1(data))
                    {
                        if (request->bootId == bootId)
                        {
                            bus.receive(GATAS::DeviceActionRequestMsg{request->action});
                        }
                    }
                }
                continue;
            }
            // Device actions above bypass the configuration-update holdback.
            if (!allowNonDeviceActionFrames)
            {
                continue;
            }

            if (frameType == BinaryMessages::DataType::OWNSHIP_PRESSURE_V1)
            {
                const auto pressure = BinaryMessages::deserializeOwnshipPressureV1(reader);
                if (pressure)
                {
                    // Store missing values too, so consumers can invalidate a previous sample.
                    ownshipState.updateBarometricPressure(GATAS::BarometricSource::External, pressure.value(), CoreUtils::msSinceEpoch());
                    bus.receive(GATAS::BarometricPressureMsg{});
                }
                continue;
            }

            /**
             * Handle a aircraft that's received
             */
            if (frameType == BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V1) {
                auto aircraftPosition = BinaryMessages::deserializeAircraftPositionV1(ownShipLat, ownShipLon, reader);
                if (!aircraftPosition.has_value())
                {
                    GATAS_WARN("Ignoring malformed binary V1 aircraft position");
                    continue;
                }
                addPosition(aircraftPosition.value());
            }

            if (frameType == BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V2)
            {
                auto aircraftPosition = BinaryMessages::deserializeAircraftPositionV2(ownShipLat, ownShipLon, reader);
                if (!aircraftPosition.has_value())
                {
                    GATAS_WARN("Ignoring malformed binary V2 aircraft position");
                    continue;
                }
                addPosition(aircraftPosition.value());
            }

            if (frameType == BinaryMessages::DataType::AIRCRAFT_POSITION_TYPE_V3)
            {
                auto aircraftPosition = BinaryMessages::deserializeAircraftPositionV3(ownShipLat, ownShipLon, reader);
                if (!aircraftPosition.has_value())
                {
                    GATAS_WARN("Ignoring malformed binary V3 aircraft position");
                    continue;
                }
                addPosition(aircraftPosition.value());
            }


            /**
             * Handle change of current selected aircraft
             */
            if (frameType == BinaryMessages::DataType::SET_ICAO_ADDRESS_V1)
            {
                auto icaoAddress = BinaryMessages::deserializeSetIcaoAddressV1(reader);
                if (icaoAddress != 0)
                {
                    bus.receive(GATAS::SelectAircraftRequestMsg{icaoAddress});
                }
            }

            if (frameType == BinaryMessages::DataType::SET_WIFI_MODE_V1)
            {
                GATAS::WifiMode wifiMode = GATAS::WifiMode::NC;
                if (BinaryMessages::deserializeSetWifiModeV1(reader, wifiMode))
                {
                    bus.receive(GATAS::WifiModeRequestMsg{wifiMode});
                }
            }
        }

        bool fullAndNothingProcessed = !cobsMessageProcessed && gulpBuffer.full();

        // If we are full and nothing processed, there is a resobale chance that we won't ever process anything, so we need to start picking up again
        // therefor we clear the buffers
        if (fullAndNothingProcessed)
        {
            GATAS_WARN("CobsStreamHandler: Gulp buffer full without processing a COBS message");
            gulp.erase();
        }


        // Send the left over if any
        if (!positionMessages.empty())
        {
            bus.receive(GATAS::IngressAircraftPositionsMsg(positionMessages));
        }
    }
};
