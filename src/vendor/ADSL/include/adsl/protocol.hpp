#pragma once

#include "connector.hpp"
#include "exvalue.hpp"
#include "header.hpp"
#include "networkpayload.hpp"
#include "statuspayload.hpp"
#include "trafficpayload.hpp"
#include "uplinkentry.hpp"
#include "utils.hpp"
#include "print.hpp"
#include "framebuffer.hpp"
#include "utils.hpp"
#include "errorCorrect.hpp"

#include "etl/map.h"
#include "etl/optional.h"
#include "etl/random.h"
#include "etl/variant.h"
#include <cmath>
#include <cassert>

namespace ADSL
{
  using PayloadSerializer = void (*)(const void *payloadCtx, const void *ctx, Connector *, etl::bit_stream_writer &);

  /**
   * @brief Protocol class for handling ADSL communication.
   *
   * This class manages the sending and receiving of ADSL packets, including
   * handling acknowledgments, forwarding, and maintaining a neighbor table.
   */
  class Protocol
  {
    static constexpr uint8_t MAX_NEIGHBORS = 16;

  public:
    static constexpr uint8_t MAX_UPLINK_TARGETS = 10;
    // based on recomendations of 20s Status Payloads, to short and we expire to quickly..
    // to long and we migh send longer on a lower issue version
    static constexpr uint32_t NEIGHBOR_EXPIRE_US = 30'000'000;
    static constexpr float MAX_VERTICAL_DISTANCE = 800.f;
    static constexpr float MAX_HORIZONTAL_DISTANCE = 1000.f;

  public:
    struct RxStatudeCode
    {
      enum enum_type : uint8_t
      {
        OK = 0,
        CRC_FAILED,
        UNSUPORTED_DECRYPTION_KEY,
        UNEXPECTED_BUFFER_SIZE,
        UNSUPORTED_PROTOCOL_VERSION,
        UNSUPORTED_ERROR_CONTROL_FEC,
        UNSUPPORTED_PAYLOAD,
        OTHER_ERROR,
      };

      ETL_DECLARE_ENUM_TYPE(RxStatudeCode, uint8_t)
      ETL_ENUM_TYPE(OK, "Ok")
      ETL_ENUM_TYPE(CRC_FAILED, "CRC Failed")
      ETL_ENUM_TYPE(UNSUPORTED_DECRYPTION_KEY, "Encryption Key Index")
      ETL_ENUM_TYPE(UNEXPECTED_BUFFER_SIZE, "Unexpected buffer size")
      ETL_ENUM_TYPE(UNSUPORTED_PROTOCOL_VERSION, "Unsupported protocol version")
      ETL_ENUM_TYPE(UNSUPORTED_ERROR_CONTROL_FEC, "Unsupported error controle method")
      ETL_ENUM_TYPE(UNSUPPORTED_PAYLOAD, "Unsuported payload")
      ETL_END_ENUM_TYPE
    };

    struct
    {
      float lat;
      float lon;
      int32_t alt;

      float groundSpeed;
      float track;
    } ownship;

    // Do a CRC Check when receiving the data
    bool crcCheckOnReceive = false;

    /**
     * Find the lowest protocol version that we support over all Neigbors,
     * never higher than we can support ourselve
     */
    uint8_t lowestDominotorProtocolVersion() const
    {
      LockGuard guard(connector);
      uint8_t protocolVersion = static_cast<uint8_t>(NetworkPayload::ProtocolVersion::LATEST);
      for (auto it = neighboursProtocolVersion.begin(); it != neighboursProtocolVersion.end(); it++)
      {
        if (it->second.protocolVersion < protocolVersion)
        {
          protocolVersion = it->second.protocolVersion;
        }
      }
      return protocolVersion;
    }

    uint8_t numberOfNeighbours() const
    {
      LockGuard guard(connector);
      return static_cast<uint8_t>(neighboursProtocolVersion.size());
    }

  protected:
    etl::array<uint32_t, 4> keys = {};

    // Random number generator for random times
    etl::random_xorshift random; // XOR-Shift PRNG from ETL

    // Maximum and minimum protocol versions received from status payloads
    ExpiringValue<uint8_t> receivedMaxVersion_{5000};

    struct Neighbour
    {
      uint32_t tickExpire;
      uint8_t protocolVersion;
    };

    etl::map<uint32_t, Neighbour, MAX_NEIGHBORS> neighboursProtocolVersion;

    struct LockGuard
    {
      Connector *conn;
      LockGuard(Connector *c) : conn(c) { conn->adsl_lock(); }
      ~LockGuard() { conn->adsl_unlock(); }
    };

    // Connector for the application, e.g., the interface between the ADSL
    // protocol and the application
    Connector *connector;
    NetworkPayload networkPayload;
    Header header;

    // Track when we need to send a Status package
    uint32_t nextStatusSent = 0;
    bool doSendStatus = false;

    bool decideRequireTrackingOfVersion(const NetworkPayload &tNetworkPayload, const StatusPayload &statusPayload)
    {
      return tNetworkPayload.protocolVersion() < NetworkPayload::ProtocolVersion::LATEST;
    }

    /**
     * Decide if protocol needs to be downgraded based on ADS-L.4.SRD860.B.5
     */
    bool decideRequireTrackingOfVersion(const NetworkPayload &tNetworkPayload, const TrafficPayload &trafficPayload)
    {
      // When received traffic is already operating at a protocolversion we are operating, no need to decide anything
      // THis will safe us some additional calculations.
      if (tNetworkPayload.protocolVersion() == networkPayload.protocolVersion() || tNetworkPayload.protocolVersion() == NetworkPayload::ProtocolVersion::LATEST)
      {
        return false;
      }

      // When the protocol version was already in tracking table, no need to add it again
      // Note: When this was a other aircraft that is outside of rules governed by ADS-L.4.SRD860.B.5
      // then it will be re-added quickly by this aircraft
      if (hasProtocolVersion(tNetworkPayload.protocolVersion()))
      {
        return false;
      }

      // Quick bailout based on altitude
      auto verticalDistance = abs(trafficPayload.altitude() - ownship.alt);
      if (verticalDistance > MAX_VERTICAL_DISTANCE)
      {
        return false;
      }

      auto neDistance = northEastDistance(ownship.lat, ownship.lon, trafficPayload.latitude(), trafficPayload.longitude());

      // 1. The other client is closer than 1 km horizontal and the vertical
      // separation is under MAX_VERTICAL_DISTANCE m.
      auto distance = sqrtf((neDistance.north * neDistance.north) + (neDistance.east * neDistance.east));
      if (distance < MAX_HORIZONTAL_DISTANCE)
      {
        return true;
      }

      // 2.0 The Time to Closest Point of Approach (TCPA) to the other client is
      // less than 30 seconds, and the vertical separation is under 800 m.
      auto tcpa = computeTCPA(neDistance, ownship.track, ownship.groundSpeed, trafficPayload.groundTrack(), trafficPayload.speed());
      if (tcpa < 30.0f)
      {
        return true;
      }

      return false;
    }

    bool hasProtocolVersion(uint8_t hasProtocolVersion)
    {
      LockGuard guard(connector);
      for (auto it = neighboursProtocolVersion.begin(); it != neighboursProtocolVersion.end(); it++)
      {
        if (it->second.protocolVersion == hasProtocolVersion)
        {
          return true;
        }
      }
      return false;
    }

    /**
     * When the given protocolVersion is higher than recorded, take the higher version.
     */
    void trackNeighbourProtocol(uint32_t address, uint8_t maxProtocolVersion)
    {
      auto tickExpire = connector->adsl_getTick() + NEIGHBOR_EXPIRE_US;
      LockGuard guard(connector);
      auto it = neighboursProtocolVersion.find(address);

      if (it != neighboursProtocolVersion.end())
      {
        // refresh expiry and update the stored protocol version only if the
        // newly reported maxProtocolVersion is higher than the current one
        it->second.tickExpire = tickExpire;
        if (it->second.protocolVersion < maxProtocolVersion)
        {
          it->second.protocolVersion = maxProtocolVersion;
        }
      }
      else
      {
        if (!neighboursProtocolVersion.full())
        {
          neighboursProtocolVersion.insert(std::make_pair(address, Neighbour{tickExpire, maxProtocolVersion}));
        }
      }
    }

    void expireNeighbours()
    {
      // remove all neighbors we have not seen for a while
      auto tick = connector->adsl_getTick();
      LockGuard guard(connector);
      for (auto it = neighboursProtocolVersion.begin(); it != neighboursProtocolVersion.end(); it++)
      {
        if (isUsReached(it->second.tickExpire, tick))
        {
          neighboursProtocolVersion.erase(it);
        }
      }
    }

    /**
     * @brief Send a generic payload using the specified serializer.
     * For performance reasons added here.
     * @param type The payload type identifier.
     * @param serializer The function to serialize the payload.
     * @param connector The connector interface for sending the frame.
     */
    void sendGenericPayload(
        const void *payloadCtx,
        const void *ctx,
        bool addPayloadLength,
        NetworkPayload networkPayload,
        Header::PayloadTypeIdentifier type,
        PayloadSerializer serializer,
        Connector *connector,
        size_t sizeBytes)
    {
#ifndef NDEBUG
      if ((sizeBytes % 4) != 0)
      {
        puts("WARNING: Payload not divisible by 4");
      }
#endif

      auto wordSize = (sizeBytes + 3) / 4; // request at least for the full word, then add 1 because of the shift
      auto ptrMemory = connector->adsl_alloc(ctx, (wordSize + 1) * 4);
      if (!ptrMemory)
      {
        puts("WARNING: Failed to allocate memory for sending frame");
        return;
      }

      FrameBuffer frame{etl::span<uint32_t>(reinterpret_cast<uint32_t *>(ptrMemory), wordSize)};
      etl::bit_stream_writer writer(frame.fullSpan(), etl::endian::little);

      // Serialize Header
      Header lCopy = header;
      lCopy.type(type); // Set this after the user's any additional header data to ensure we do the right thing
      lCopy.serialize(writer);

      // Serialize Payload
      serializer(payloadCtx, ctx, connector, writer);
      frame.used_bytes = writer.size_bytes();

      // Flip bytes to match memory layout
      frame.reverseBits();

      // Encrypt header + payload that is now byte aligned (Payload + header is always devisable by 4)
      auto keyIdx = networkPayload.keyIndex();
      // ADS-L.4.SRD860.E.1.3
      if (keyIdx < 3)
      {
        XXTEA_Encrypt_Key0(frame.words(), wordSize - ((NetworkPayload::LENGTH + NetworkPayload::LENGTH_CRC) / 4), 6);
      }

      // Shift one whole word to to optionally make place for a network header and to make the payload start at the same place as the received payload for easier CRC calculation
      frame.shiftOneWord();

      // Add the network header at the beginning, not part of encryption
      // it's not part of scramble, but part of the CRC but scramble needs to be 32bit aligned
      frame[3] = etl::reverse_bits<uint8_t>(networkPayload.asUint8());

      // Finally calculate the CRC of the complete packeted, stoarring at offset. 3, effectivly starting at position 3
      uint32_t crc = ADSL::calcPI(frame.fullSpan().subspan(3, sizeBytes - 3)) & 0xFFFFFF;

      // Add CRC
      frame.fullSpan32()[wordSize] = (crc << 16) | (crc & 0x0000FF00) | (crc >> 16);


      if (addPayloadLength)
      {
        frame[2] = static_cast<uint8_t>(sizeBytes);
        connector->adsl_sendFrame(ctx, static_cast<const uint8_t *>(ptrMemory) + 2, sizeBytes + 1);
      }
      else
      {
        connector->adsl_sendFrame(ctx, static_cast<const uint8_t *>(ptrMemory) + 3, sizeBytes);
      }

    }

    // Static serializer functions
    static void serializeTrafficPayload(const void *payloadCtx, const void *ctx, Connector *connector, etl::bit_stream_writer &writer)
    {
      const auto &tp = *static_cast<const TrafficPayload *>(payloadCtx);
      tp.serialize_issue1(writer);
    }

    static void serializeStatusPayload(const void *payloadCtx, const void *ctx, Connector *connector, etl::bit_stream_writer &writer)
    {

      const auto &tp = *static_cast<const StatusPayload *>(payloadCtx);
      tp.serialize_issue2(writer);
    }

    static void serializeUplinkPayload(const void *payloadCtx, const void *ctx, Connector *connector, etl::bit_stream_writer &writer)
    {
      const auto &entries = *static_cast<const etl::span<const UplinkEntry> *>(payloadCtx);
      size_t totalBytes = 0;
      for (const auto &entry : entries)
      {
        totalBytes += entry.serialize(writer);
      }

      // Pad so that Header::LENGTH + total is divisible by 4 (0–3 bytes)
      const size_t padBytes = (NetworkPayload::LENGTH_CRC - totalBytes) & 0x3; // 3 is length of the CRC
      if (padBytes)
      {
        writer.write_unchecked(0U, 8U * padBytes);
      }
    }

    /**
     * Validates the current buffer before processing
     */
    RxStatudeCode validateBuffer(etl::span<const uint8_t> buffer)
    {
      // Reversebits on first byte that is not XXTEAed
      auto networkPayload = NetworkPayload::fromUint8(etl::reverse_bits<uint8_t>(buffer[0]));

      // Test if this version can be handled
      if (networkPayload.protocolVersion() > NetworkPayload::ProtocolVersion::LATEST)
      {
        return RxStatudeCode::UNSUPORTED_PROTOCOL_VERSION;
      }

      if (networkPayload.errorControlMode() == NetworkPayload::ErrorControlMode::FEC)
      {
        // FEC mode - not supported yet
        return RxStatudeCode::UNSUPORTED_ERROR_CONTROL_FEC;
      }

      // ADS-L.4.SRD860.E.1.3
      if (networkPayload.keyIndex() > 0 && networkPayload.keyIndex() < 3)
      {
        // Only 0 and 3 supported 3 is no encryption
        return RxStatudeCode::UNSUPORTED_DECRYPTION_KEY;
      }

      // When the manchehester has been corrected and validate, there is no need to re-validate the CRC
      // So by default CRC check is disabled. For OBand this is still required because of the lack of manchester
      if (crcCheckOnReceive && ADSL::checkPI(etl::span(buffer.data(), buffer.size())) != 0)
      {
        return RxStatudeCode::CRC_FAILED;
      }

      return RxStatudeCode::OK;
    }

  public:
    /**
     * @brief Constructor for the Protocol class.
     * @param connector_ The connector interface for the application.
     */
    Protocol(Connector *connector_) : connector(connector_) { init(); }
    Protocol() = default;

    void init()
    {
      nextStatusSent = connector->adsl_getTick();
      random.initialise(nextStatusSent);
    }

    void setAddress(uint32_t address)
    {
      header.address(Address(address));
    }

    /**
     * Set the encryption/decryption keys
     */
    void setKeys(etl::span<const uint32_t> k)
    {
      etl::fill(keys.begin(), keys.end(), 0u);
      etl::copy_n(k.begin(), etl::min(k.size(), keys.size()), keys.begin());
    }

    /**
     * @brief Send a Traffic Uplink payload (ADS-L.4.SRD860.G.3) containing up to 10 targets.
     *
     * Targets may originate from ADS-B, Mode-S, ADS-L, RemoteID, or other e-Conspicuity systems.
     * Must be transmitted on the O-Band HDR channel in the Uplink time slot.
     * The age of each target's timestamp may not exceed 10 seconds.
     *
     * @param ctx    Caller-supplied context forwarded verbatim to adsl_alloc and adsl_sendFrame.
     * @param targets Span of TrafficPayload entries (1–MAX_UPLINK_TARGETS).
     */
    void rqSendUplinkPayload(const void *ctx, bool addPayloadLength, const etl::span<const UplinkEntry> entries)
    {
      if (entries.empty() || entries.size() > MAX_UPLINK_TARGETS)
      {
        return;
      }

      auto nPayload = networkPayload;
      nPayload.protocolVersion(NetworkPayload::ProtocolVersion::ISSUE_2);
      const size_t sizeBytes = NetworkPayload::LENGTH + Header::LENGTH + UplinkEntry::calculateSizeWithPadding(entries.size()) + NetworkPayload::LENGTH_CRC;
      sendGenericPayload(&entries, ctx, addPayloadLength, nPayload, Header::PayloadTypeIdentifier::TRAFFICUPLINK, serializeUplinkPayload, connector, sizeBytes);
    }


    /**
     * @brief Request transmission of ownship traffic or a pending status payload.
     *
     * Called periodically by the application to send the current ownship position.
     * If a status transmission is due (doSendStatus == true), a STATUS frame is sent
     * instead and the traffic payload is dropped for this cycle. This ensures the
     * status is sent at least once before the next traffic frame.
     *
     * The protocol version used for TRAFFIC is determined by lowestDominatorProtocolVersion(),
     * which downgrades to ISSUE_1 when a close neighbour that only supports ISSUE_1 is tracked.
     * STATUS frames are always sent at ISSUE_2 regardless of neighbours.
     *
     * @param ctx          Opaque caller context passed through to adsl_sendFrame.
     * @param addPayloadLength  When true, prepends a 1-byte length field before the
     *                     payload required for ADSL on M band only.
     * @param tp           Ownship traffic payload to transmit. Ignored when a STATUS
     *                     frame is sent instead.
     */
    void rqSendTrafficPayload(const void *ctx, bool addPayloadLength, const TrafficPayload &tp)
    {
      auto nPayload = networkPayload;
      if (doSendStatus)
      {
        // A status transmission is pending — send STATUS this cycle instead of TRAFFIC.
        // STATUS always uses ISSUE_2 regardless of neighbour protocol versions.
        doSendStatus = false;
        nPayload.protocolVersion(NetworkPayload::ProtocolVersion::ISSUE_2);
        StatusPayload sp;
        connector->adsl_buildStatusPayload(ctx, sp);
        constexpr size_t totalSize = NetworkPayload::LENGTH + Header::LENGTH + StatusPayload::LENGTH + NetworkPayload::LENGTH_CRC;
        sendGenericPayload(&sp, ctx,addPayloadLength, nPayload, Header::PayloadTypeIdentifier::STATUS, serializeStatusPayload, this->connector, totalSize);
      }
      else
      {
        // Send TRAFFIC at the lowest protocol version supported by all tracked neighbours.
        // Downgrades to ISSUE_1 when any close neighbour only supports ISSUE_1.
        nPayload.protocolVersion(static_cast<NetworkPayload::ProtocolVersion>(lowestDominotorProtocolVersion()));
        constexpr size_t totalSize = NetworkPayload::LENGTH + Header::LENGTH + TrafficPayload::LENGTH + NetworkPayload::LENGTH_CRC;
        sendGenericPayload(&tp, ctx, addPayloadLength, nPayload, Header::PayloadTypeIdentifier::TRAFFIC, serializeTrafficPayload, this->connector, totalSize);
      }
    }

    /**
     * @brief Handle a received ADSL packet, it expect the payload length be already stripped
     * @tparam MAXFRAMESIZE The size of the message payload.
     * @tparam MAXFRAMESIZE The size of the name payload.
     * @param rssddBm The received signal strength in dBm.
     * @param buffer The byte buffer containing the packet data.
     * @return The MessageType
     */
    RxStatudeCode handleRx(int16_t rssddBm, etl::span<uint32_t> wordBuffer)
    {

      if (wordBuffer.size() < 3)
      {
        return RxStatudeCode::UNEXPECTED_BUFFER_SIZE;
      }

      // printf("Received packet size: %d, RSSI: %d dBm\n", wordBuffer.size() * sizeof(uint32_t), rssddBm);
      //      printBufferHex(wordBuffer);
      // Create a view into the word buffer as bytes
      auto buffer = etl::span<uint8_t>(reinterpret_cast<uint8_t *>(wordBuffer.data()), wordBuffer.size() * sizeof(uint32_t));

      auto result = validateBuffer(buffer);
      if (result != RxStatudeCode::OK)
      {
        return result;
      }

      // Move the network payload out, so encryption is alligned on the header
      auto receivedNetworkPayload = NetworkPayload::fromUint8(etl::reverse_bits<uint8_t>(buffer[0]));
      etl::mem_move(&buffer[1], buffer.size() - 4, &buffer[0]);

      assert(((reinterpret_cast<uintptr_t>(buffer.data()) & 0x3) == 0) && "Data not 32-bit aligned");

      auto keyIdx = receivedNetworkPayload.keyIndex();
      // ADS-L.4.SRD860.E.1.3
      if (keyIdx < 3)
      {
        XXTEA_Decrypt_Key0(wordBuffer.data(), wordBuffer.size() - 1, 6);
      }

      // reverse all bytes in the array, this is due to how the memory layout's work on MCU
      // Properly to save some sycles, ADSL was designed to dump mempry layouts rather than specify the bit stream
      for (size_t i = 0; i < buffer.size(); i++)
      {
        buffer[i] = etl::reverse_bits(buffer[i]);
      }
      etl::bit_stream_reader reader(buffer, etl::endian::little);
      auto header = Header::deserialize(reader);

      // Based on header, load the correct payloads from each version that was
      // found in the network layer
      auto accepted = RxStatudeCode::OTHER_ERROR;
      switch (header.type())
      {
      case Header::PayloadTypeIdentifier::STATUS:
      {
        if (receivedNetworkPayload.protocolVersion() == NetworkPayload::ProtocolVersion::ISSUE_2)
        {
          if (wordBuffer.size() == 4)
          {
            auto payload = StatusPayload::deserialize_issue2(reader);
            // Only track naughbours who's protocol are lower then ours
            //      if (decideRequireTrackingOfVersion(tNetworkPayload, payload))
            {
              trackNeighbourProtocol(header.address().asUint(), static_cast<uint8_t>(payload.maxProtocolversion()));
            }
            connector->adsl_receivedStatus(header, payload);
            accepted = RxStatudeCode::OK;
          }
          else
          {
            accepted = RxStatudeCode::UNEXPECTED_BUFFER_SIZE;
          }
        }
        break;
      }

      case Header::PayloadTypeIdentifier::TRAFFIC:
      {
        if (receivedNetworkPayload.protocolVersion() <= NetworkPayload::ProtocolVersion::ISSUE_2)
        {
          if (wordBuffer.size() == 6)
          {
            auto payload = TrafficPayload::deserialize_issue1(reader);

            // Only track the version if this is required by specification
            if (decideRequireTrackingOfVersion(receivedNetworkPayload, payload))
            {
              trackNeighbourProtocol(header.address().asUint(), static_cast<uint8_t>(receivedNetworkPayload.protocolVersion()));
            }
            connector->adsl_receivedTraffic(header, payload);
            accepted = RxStatudeCode::OK;
          }
          else
          {
            accepted = RxStatudeCode::UNEXPECTED_BUFFER_SIZE;
          }
        }
        break;
      }

      break;
      case Header::PayloadTypeIdentifier::RESERVED0:
        accepted = RxStatudeCode::UNSUPPORTED_PAYLOAD;
        break;
      case Header::PayloadTypeIdentifier::RESERVED1:
        accepted = RxStatudeCode::UNSUPPORTED_PAYLOAD;
        break;
      case Header::PayloadTypeIdentifier::TRAFFICUPLINK:
      {
        const size_t payloadBytes = wordBuffer.size() * sizeof(uint32_t) - NetworkPayload::LENGTH - Header::LENGTH - NetworkPayload::LENGTH_CRC;
        const size_t numEntries = payloadBytes / UplinkEntry::LENGTH;
        const size_t remainder = payloadBytes % UplinkEntry::LENGTH;
        if (numEntries >= 1 && numEntries <= MAX_UPLINK_TARGETS)
        {
          etl::vector<UplinkEntry, MAX_UPLINK_TARGETS> entries;
          for (size_t i = 0; i < numEntries; ++i)
          {
            entries.push_back(UplinkEntry::deserialize(reader));
          }
          if (remainder)
          {
            reader.skip(remainder * 8);
          }
          connector->adsl_receivedUplinkTraffic(header, etl::span<const UplinkEntry>(entries.data(), entries.size()));
          accepted = RxStatudeCode::OK;
        }
        else
        {
          accepted = RxStatudeCode::UNEXPECTED_BUFFER_SIZE;
        }
        break;
      }
      case Header::PayloadTypeIdentifier::FISBUPLINK:
        accepted = RxStatudeCode::UNSUPPORTED_PAYLOAD;
        break;
      case Header::PayloadTypeIdentifier::REMOTEID:
        accepted = RxStatudeCode::UNSUPPORTED_PAYLOAD;
        break;
      }

      return accepted;
    }

    /**
     * @brief Handle any packets in the queue.
     *
     * This can be called at regular intervals, or it can be called based on the
     * time returned by this function when it thinks a packet can be sent. This
     * function might not always send a packet even if there are packets in the
     * queue.
     *
     * @return The time in milliseconds until the next packet can be sent.
     */
    void tick(float lat, float lon, int32_t alt, float groundSpeed, float track)
    {
      ownship.lat = lat;
      ownship.lon = lon;
      ownship.alt = alt;
      ownship.groundSpeed = groundSpeed;
      ownship.track = track;

      auto now = connector->adsl_getTick();

      // Decide for next status to be send
      auto diff = static_cast<int32_t>(nextStatusSent - now);
      if (diff <= 0)
      {
        nextStatusSent = now + 20'000'000;
        doSendStatus = true;
        expireNeighbours();
      }
    }

  private:
  };

} // namespace ADSL
