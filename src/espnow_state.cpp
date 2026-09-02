#include "espnow_state.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <esp_now.h>
#include <lvgl.h>

#include "app_state.h"

namespace
{
    bool s_enabled = false;

    SM_KnownDevice s_known_devices[kMaxKnownDevices];
    int s_known_device_count = 0;

    constexpr uint16_t kFirmwareVersionMajor = 0;
    constexpr uint16_t kFirmwareVersionMinor = 1;

    const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    uint16_t s_next_sequence = 0;

    bool s_have_pending_request = false;
    SM_IncomingPairRequest s_pending_request;
    uint32_t s_pending_request_ms = 0;
    // Slightly longer than the FN bridge's own hold timeout (see
    // M5AtomS3-FN-Bridge/src/main.cpp's kHoldTimeoutMs) - by the time this
    // elapses the requester has already given up waiting and resumed
    // sweeping on its own, so there's no need to send SM_PROVISION_REJECTED,
    // just quietly stop showing the stale request.
    constexpr uint32_t kPendingRequestTimeoutMs = 35000;

    // Tracks whether the credentials sent by the most recent
    // espnow_accept_pair_request() were actually confirmed - see
    // SM_PairOutcome in espnow_state.h.
    SM_PairOutcome s_pair_outcome = SM_PAIR_NONE;
    uint8_t s_awaiting_ack_mac[6] = {};
    uint32_t s_awaiting_ack_nonce = 0;
    uint32_t s_awaiting_ack_deviceID = 0;
    uint32_t s_awaiting_ack_since_ms = 0;
    constexpr uint32_t kAckTimeoutMs = 5000;

    constexpr const char *kPairedNamespace = "fn_paired";
    constexpr const char *kPairedKey = "list";
    SM_PairedBridge s_paired_bridges[kMaxPairedBridges];
    int s_paired_bridge_count = 0;

    bool s_have_last_ping = false;
    SM_LastPing s_last_ping = {};

    bool s_have_last_fn_tx_status = false;
    SM_FnTxStatus s_last_fn_tx_status = {};

    SM_TrafficLogEntry s_traffic_log[kMaxTrafficLogEntries];
    int s_traffic_log_count = 0; // entries filled so far, caps at kMaxTrafficLogEntries
    int s_traffic_log_next = 0;  // ring buffer write cursor

    // Forward declarations - defined further down (near find_paired_bridge_index,
    // which format_peer_label_by_mac depends on), but send_to() below needs
    // to log every outgoing message as soon as it's sent.
    void format_peer_label_by_mac(const uint8_t mac[6], char *out, size_t outSize);
    void log_traffic(bool outgoing, uint8_t messageType, const char *peerLabel);

    void fill_local_identity(SM_DeviceIdentity &out)
    {
        out.deviceID = espnow_local_device_id();
        // TODO: pick the SM_DeviceType (espnow_protocol.h) that actually
        // matches what this device does, or add a new enum value for it.
        out.deviceType = SM_DEVICE_GENERIC;
        out.protocolVersion = 1;
        out.firmwareVersionMajor = kFirmwareVersionMajor;
        out.firmwareVersionMinor = kFirmwareVersionMinor;
        out.firmwareVersionPatch = 0;
        strncpy(out.friendlyName, g_config.espnow_friendly_name.c_str(), sizeof(out.friendlyName) - 1);
        out.friendlyName[sizeof(out.friendlyName) - 1] = '\0';
    }

    // Builds header+payload into one buffer and sends it to `mac`. ESP-NOW
    // caps a packet at 250 bytes total; every payload struct in
    // espnow_protocol.h is sized to leave room for the 14-byte header
    // within that, so this doesn't separately re-check the limit.
    bool send_to(const uint8_t mac[6], SM_MessageType type, const void *payload, uint16_t payloadLen)
    {
        uint8_t buf[250];
        if (sizeof(SM_Header) + payloadLen > sizeof(buf))
            return false;

        SM_Header header;
        header.version = 1;
        header.messageType = type;
        header.sourceID = espnow_local_device_id();
        header.destinationID = 0xFFFFFFFF; // recipient identity isn't tracked by MAC-only messages
        header.sequence = s_next_sequence++;
        header.payloadLength = payloadLen;

        memcpy(buf, &header, sizeof(header));
        if (payloadLen > 0)
            memcpy(buf + sizeof(header), payload, payloadLen);

        bool ok = esp_now_send(mac, buf, sizeof(header) + payloadLen) == ESP_OK;

        char peerLabel[24];
        format_peer_label_by_mac(mac, peerLabel, sizeof(peerLabel));
        log_traffic(/*outgoing=*/true, type, peerLabel);

        return ok;
    }

    bool send_broadcast(SM_MessageType type, const void *payload, uint16_t payloadLen)
    {
        return send_to(kBroadcastMac, type, payload, payloadLen);
    }

    void send_discover()
    {
        send_broadcast(SM_DISCOVER, nullptr, 0);
    }

    void send_announce()
    {
        SM_DeviceIdentity identity;
        fill_local_identity(identity);
        send_broadcast(SM_ANNOUNCE, &identity, sizeof(identity));
    }

    void send_heartbeat()
    {
        SM_Heartbeat hb;
        fill_local_identity(hb.identity);
        hb.uptimeSeconds = millis() / 1000;
        hb.statusFlags = 0;
        send_broadcast(SM_HEARTBEAT, &hb, sizeof(hb));
    }

    void poll_heartbeat(lv_timer_t *)
    {
        send_heartbeat();
    }

    // Ensures `mac` is registered as an ESP-NOW peer before unicasting to
    // it. Deliberately unencrypted - see espnow_protocol.h's note on why
    // the earlier encrypted-peer design didn't work reliably.
    void ensure_peer(const uint8_t mac[6])
    {
        if (esp_now_is_peer_exist(mac))
            return;

        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0; // whatever channel the STA interface is already on
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    void load_paired_bridges()
    {
        Preferences prefs;
        prefs.begin(kPairedNamespace, /*readOnly=*/true);
        size_t len = prefs.getBytesLength(kPairedKey);
        int count = static_cast<int>(len / sizeof(SM_PairedBridge));
        if (count > kMaxPairedBridges)
            count = kMaxPairedBridges;
        if (count > 0)
            prefs.getBytes(kPairedKey, s_paired_bridges, count * sizeof(SM_PairedBridge));
        s_paired_bridge_count = count;
        prefs.end();
        Serial.printf("[paired-bridges] loaded %d entr%s from NVS (raw bytes=%u, sizeof(SM_PairedBridge)=%u)\n",
                      count, count == 1 ? "y" : "ies", static_cast<unsigned>(len), static_cast<unsigned>(sizeof(SM_PairedBridge)));
        for (int i = 0; i < s_paired_bridge_count; i++)
            Serial.printf("  [%d] %02X:%02X:%02X:%02X:%02X:%02X \"%s\"\n", i,
                          s_paired_bridges[i].mac[0], s_paired_bridges[i].mac[1], s_paired_bridges[i].mac[2],
                          s_paired_bridges[i].mac[3], s_paired_bridges[i].mac[4], s_paired_bridges[i].mac[5],
                          s_paired_bridges[i].friendlyName);
    }

    void save_paired_bridges()
    {
        Preferences prefs;
        prefs.begin(kPairedNamespace, /*readOnly=*/false);
        bool ok;
        if (s_paired_bridge_count == 0)
        {
            // putBytes() with a zero-length buffer doesn't reliably clear an
            // existing key - it can just no-op, leaving the last-saved
            // (pre-Forget) blob sitting in NVS to reload on the next boot.
            // Remove the key outright instead.
            ok = prefs.remove(kPairedKey);
        }
        else
        {
            size_t written = prefs.putBytes(kPairedKey, s_paired_bridges, s_paired_bridge_count * sizeof(SM_PairedBridge));
            ok = (written == s_paired_bridge_count * sizeof(SM_PairedBridge));
        }
        prefs.end();
        Serial.printf("[paired-bridges] saved %d entr%s to NVS (%s)\n",
                      s_paired_bridge_count, s_paired_bridge_count == 1 ? "y" : "ies", ok ? "ok" : "FAILED");
    }

    // Looks up a friendlyName for `deviceID` in the known-devices table -
    // used to label an incoming SM_PING for UI display. Returns nullptr if
    // this device hasn't sent an SM_ANNOUNCE/SM_HEARTBEAT yet.
    const char *find_known_device_name(uint32_t deviceID)
    {
        for (int i = 0; i < s_known_device_count; i++)
            if (s_known_devices[i].deviceID == deviceID)
                return s_known_devices[i].friendlyName;
        return nullptr;
    }

    // Looks up a deviceType for `deviceID` in the known-devices table -
    // used to make sure an SM_STATUS payload is only interpreted as
    // FN-pod-specific (SM_FnTxStatusPayload) when it actually came from
    // one. Returns false if this device hasn't sent an
    // SM_ANNOUNCE/SM_HEARTBEAT yet (e.g. its status races ahead of its
    // first announce right after boot).
    bool find_known_device_type(uint32_t deviceID, uint8_t *outType)
    {
        for (int i = 0; i < s_known_device_count; i++)
            if (s_known_devices[i].deviceID == deviceID)
            {
                *outType = s_known_devices[i].deviceType;
                return true;
            }
        return false;
    }

    int find_paired_bridge_index(const uint8_t mac[6])
    {
        for (int i = 0; i < s_paired_bridge_count; i++)
            if (memcmp(s_paired_bridges[i].mac, mac, 6) == 0)
                return i;
        return -1;
    }

    // Labels a peer for the traffic log: broadcast -> "ALL", a known paired
    // bridge -> its friendly name, otherwise a formatted MAC address.
    void format_peer_label_by_mac(const uint8_t mac[6], char *out, size_t outSize)
    {
        if (memcmp(mac, kBroadcastMac, 6) == 0)
        {
            strncpy(out, "ALL", outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
        int idx = find_paired_bridge_index(mac);
        if (idx >= 0)
        {
            strncpy(out, s_paired_bridges[idx].friendlyName, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
        snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Same, but for an incoming message where the sender's deviceID (not
    // just its MAC) is known - prefers the known-devices table, which is
    // populated by heartbeats/announces from any ShackMate device, not
    // just paired bridges.
    void format_peer_label_by_device(uint32_t deviceID, const uint8_t mac[6], char *out, size_t outSize)
    {
        const char *name = find_known_device_name(deviceID);
        if (name != nullptr)
        {
            strncpy(out, name, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
        format_peer_label_by_mac(mac, out, outSize);
    }

    void log_traffic(bool outgoing, uint8_t messageType, const char *peerLabel)
    {
        SM_TrafficLogEntry &e = s_traffic_log[s_traffic_log_next];
        e.outgoing = outgoing;
        e.messageType = messageType;
        strncpy(e.peerLabel, peerLabel, sizeof(e.peerLabel) - 1);
        e.peerLabel[sizeof(e.peerLabel) - 1] = '\0';
        e.timestampMs = millis();

        s_traffic_log_next = (s_traffic_log_next + 1) % kMaxTrafficLogEntries;
        if (s_traffic_log_count < kMaxTrafficLogEntries)
            s_traffic_log_count++;
    }

    // Updates the in-RAM paired-bridge list only - deliberately does NOT
    // call save_paired_bridges() itself. That's an NVS flash write, which
    // on real hardware takes 10-50ms+ and can starve the ESP-NOW receive
    // path for its duration (SPI flash writes briefly disable the flash
    // cache, which any interrupt-driven code not fully resident in IRAM -
    // including parts of the WiFi/ESP-NOW receive path - needs to keep
    // running). The pod's SM_PROVISION_ACK arrives in a similarly narrow
    // window right after this same accept action, so a synchronous save
    // here on the CYD's side has a real chance of blocking the exact
    // moment the ACK needs to be received - real hardware testing showed
    // the ACK consistently never arriving even after earlier fixes to the
    // *ordering* of when the awaiting-ACK state got armed, which pointed
    // at this instead: a timing collision between two flash writes (this
    // one and the pod's own save_channel()), not a software race. See the
    // caller (espnow_accept_pair_request()) for the deferred save.
    void add_or_update_paired_bridge(const uint8_t mac[6], const SM_IncomingPairRequest &req)
    {
        for (int i = 0; i < s_paired_bridge_count; i++)
        {
            if (memcmp(s_paired_bridges[i].mac, mac, 6) == 0)
            {
                s_paired_bridges[i].deviceID = req.deviceID;
                strncpy(s_paired_bridges[i].friendlyName, req.friendlyName, sizeof(s_paired_bridges[i].friendlyName) - 1);
                s_paired_bridges[i].friendlyName[sizeof(s_paired_bridges[i].friendlyName) - 1] = '\0';
                s_paired_bridges[i].deviceType = req.deviceType;
                return;
            }
        }

        if (s_paired_bridge_count >= kMaxPairedBridges)
        {
            // Full and this is a new device - FIFO evict the oldest-paired
            // entry (index 0) rather than tracking last-seen times for a
            // list that otherwise never changes on its own.
            for (int i = 1; i < kMaxPairedBridges; i++)
                s_paired_bridges[i - 1] = s_paired_bridges[i];
            s_paired_bridge_count = kMaxPairedBridges - 1;
        }

        SM_PairedBridge &p = s_paired_bridges[s_paired_bridge_count++];
        memcpy(p.mac, mac, 6);
        p.deviceID = req.deviceID;
        strncpy(p.friendlyName, req.friendlyName, sizeof(p.friendlyName) - 1);
        p.friendlyName[sizeof(p.friendlyName) - 1] = '\0';
        p.deviceType = req.deviceType;
    }

    // One-shot timer callback (see espnow_accept_pair_request()) - fires
    // well after the pod's own ACK-burst-and-restart sequence has finished,
    // so this flash write can't collide with the CYD needing to receive
    // SM_PROVISION_ACK.
    void save_paired_bridges_deferred_cb(lv_timer_t *)
    {
        save_paired_bridges();
    }

    void on_espnow_recv(const uint8_t *mac, const uint8_t *data, int len)
    {
        if (len < static_cast<int>(sizeof(SM_Header)))
            return;

        SM_Header header;
        memcpy(&header, data, sizeof(header));
        if (header.version != 1)
            return; // unknown protocol version - ignore rather than misparse

        // Don't note ourselves - shouldn't normally happen (a device
        // doesn't receive its own broadcasts), but cheap to guard anyway.
        if (header.sourceID == espnow_local_device_id())
            return;

        const uint8_t *payload = data + sizeof(header);
        int payloadLen = len - static_cast<int>(sizeof(header));

        {
            char peerLabel[24];
            format_peer_label_by_device(header.sourceID, mac, peerLabel, sizeof(peerLabel));
            log_traffic(/*outgoing=*/false, header.messageType, peerLabel);
        }

        switch (header.messageType)
        {
        case SM_ANNOUNCE:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_DeviceIdentity)))
                return;
            SM_DeviceIdentity identity;
            memcpy(&identity, payload, sizeof(identity));
            identity.friendlyName[sizeof(identity.friendlyName) - 1] = '\0';
            espnow_note_device_seen(identity.deviceID, identity.friendlyName, identity.deviceType);

            // Primary pairing confirmation signal - see the pod's
            // enter_joined_mode()/send_announce() comment for why this
            // replaces relying on SM_PROVISION_ACK: real hardware testing
            // this session found that unicast ACK never gets a confirmed
            // delivery from the pod's specific call context, no matter what
            // else got fixed, while every broadcast (this SM_ANNOUNCE
            // included) has been reliable all session. The pod announces
            // immediately upon rejoining on its newly-assigned channel, so
            // this is both faster and more trustworthy than waiting on the
            // ACK - it directly proves the pod is alive and on the mesh,
            // not just that it once tried to send a packet.
            if (s_pair_outcome == SM_PAIR_WAITING && identity.deviceID == s_awaiting_ack_deviceID)
                s_pair_outcome = SM_PAIR_CONFIRMED;
            break;
        }
        case SM_HEARTBEAT:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_Heartbeat)))
                return;
            SM_Heartbeat hb;
            memcpy(&hb, payload, sizeof(hb));
            hb.identity.friendlyName[sizeof(hb.identity.friendlyName) - 1] = '\0';
            espnow_note_device_seen(hb.identity.deviceID, hb.identity.friendlyName, hb.identity.deviceType);
            break;
        }
        case SM_DISCOVER:
            // Someone's asking who's out there - let them (and everyone
            // else) know immediately rather than waiting for our next
            // periodic heartbeat.
            send_announce();
            break;
        case SM_PROVISION_REQUEST:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_ProvisionRequest)))
                return;
            SM_ProvisionRequest req;
            memcpy(&req, payload, sizeof(req));
            req.identity.friendlyName[sizeof(req.identity.friendlyName) - 1] = '\0';

            // Already a known/paired device (e.g. it forgot its channel
            // and is sweeping again, or just rebooted before finishing its
            // own save) - no need to bother the user again, just resend
            // the channel assignment immediately.
            if (find_paired_bridge_index(mac) >= 0)
            {
                Serial.printf("<- SM_PROVISION_REQUEST from already-paired %02X:%02X:%02X:%02X:%02X:%02X \"%s\" - resending channel\n",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], req.identity.friendlyName);
                SM_ChannelAssignment assignment{req.nonce, static_cast<uint8_t>(WiFi.channel())};
                ensure_peer(mac);
                send_to(mac, SM_PROVISION_CHANNEL, &assignment, sizeof(assignment));
                break;
            }

            // Busy with a different pairing session already - ignore until
            // that one is resolved (accepted, rejected, or the requester
            // gives up and stops sweeping).
            if (s_have_pending_request && memcmp(s_pending_request.mac, mac, 6) != 0)
                break;

            Serial.printf("<- SM_PROVISION_REQUEST from %02X:%02X:%02X:%02X:%02X:%02X \"%s\" "
                          "type=%u fw=%u.%u.%u nonce=0x%08X\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], req.identity.friendlyName,
                          req.identity.deviceType, req.identity.firmwareVersionMajor,
                          req.identity.firmwareVersionMinor, req.identity.firmwareVersionPatch, req.nonce);

            s_have_pending_request = true;
            s_pending_request_ms = millis();
            memcpy(s_pending_request.mac, mac, 6);
            s_pending_request.deviceID = req.identity.deviceID;
            strncpy(s_pending_request.friendlyName, req.identity.friendlyName, sizeof(s_pending_request.friendlyName) - 1);
            s_pending_request.friendlyName[sizeof(s_pending_request.friendlyName) - 1] = '\0';
            s_pending_request.deviceType = req.identity.deviceType;
            s_pending_request.firmwareVersionMajor = req.identity.firmwareVersionMajor;
            s_pending_request.firmwareVersionMinor = req.identity.firmwareVersionMinor;
            s_pending_request.firmwareVersionPatch = req.identity.firmwareVersionPatch;
            s_pending_request.nonce = req.nonce;

            // Tell it to stop sweeping and wait right here - a human still
            // has to confirm the pairing dialog, which can take far longer
            // than the requester's per-channel dwell time.
            ensure_peer(mac);
            SM_ProvisionNonce holdMsg{req.nonce};
            send_to(mac, SM_PROVISION_HOLD, &holdMsg, sizeof(holdMsg));
            break;
        }
        case SM_PROVISION_ACK:
        {
            uint32_t nonce = 0;
            if (payloadLen >= static_cast<int>(sizeof(SM_ProvisionNonce)))
            {
                SM_ProvisionNonce ackMsg;
                memcpy(&ackMsg, payload, sizeof(ackMsg));
                nonce = ackMsg.nonce;
            }
            Serial.printf("<- SM_PROVISION_ACK from %02X:%02X:%02X:%02X:%02X:%02X nonce=0x%08X - it will reboot and rejoin\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], nonce);

            if (s_pair_outcome == SM_PAIR_WAITING && nonce == s_awaiting_ack_nonce &&
                memcmp(mac, s_awaiting_ack_mac, 6) == 0)
                s_pair_outcome = SM_PAIR_CONFIRMED;
            break;
        }
        case SM_STATUS:
        {
            // SM_STATUS is "device-specific" (espnow_protocol.h) - only
            // interpret it as an FN pod's SM_FnTxStatusPayload if the
            // sender is actually known to be one, so a future non-FN
            // ShackMate device's own SM_STATUS use can't be misread here.
            uint8_t deviceType = 0;
            if (payloadLen < static_cast<int>(sizeof(SM_FnTxStatusPayload)) ||
                !find_known_device_type(header.sourceID, &deviceType) ||
                deviceType != SM_DEVICE_FN_2WIRE_POD)
                break;

            SM_FnTxStatusPayload status;
            memcpy(&status, payload, sizeof(status));
            s_have_last_fn_tx_status = true;
            s_last_fn_tx_status.deviceID = header.sourceID;
            s_last_fn_tx_status.txMode = status.txMode;
            s_last_fn_tx_status.receivedMs = millis();
            break;
        }
        case SM_PING:
        {
            if (payloadLen < static_cast<int>(sizeof(SM_PingPayload)))
                return;
            SM_PingPayload ping;
            memcpy(&ping, payload, sizeof(ping));
            // A ping can arrive from a device this session hasn't unicast
            // to yet (esp_now_send() silently fails to an unregistered
            // peer - a broadcast ping doesn't need one to be *received*,
            // but the unicast reply back does).
            ensure_peer(mac);
            send_to(mac, SM_PONG, &ping, sizeof(ping));

            s_have_last_ping = true;
            s_last_ping.deviceID = header.sourceID;
            const char *name = find_known_device_name(header.sourceID);
            if (name != nullptr)
            {
                strncpy(s_last_ping.friendlyName, name, sizeof(s_last_ping.friendlyName) - 1);
                s_last_ping.friendlyName[sizeof(s_last_ping.friendlyName) - 1] = '\0';
            }
            else
            {
                snprintf(s_last_ping.friendlyName, sizeof(s_last_ping.friendlyName), "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
            s_last_ping.receivedMs = millis();

            Serial.printf("<- SM_PING id=0x%08X from \"%s\" - replied SM_PONG\n", ping.pingID, s_last_ping.friendlyName);
            break;
        }
        default:
            break; // not yet handled (capabilities/values/commands/etc.)
        }
    }
}

bool espnow_init()
{
    s_enabled = (esp_now_init() == ESP_OK);
    load_paired_bridges();
    return s_enabled;
}

bool espnow_is_enabled()
{
    return s_enabled;
}

void espnow_start_messaging()
{
    if (!s_enabled)
        return;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = 0; // use whatever channel the STA interface is already on
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    esp_now_register_recv_cb(on_espnow_recv);

    send_discover();
    lv_timer_create(poll_heartbeat, 10000, nullptr);
}

void espnow_reestablish_after_wifi_change()
{
    if (!s_enabled)
        return; // espnow_init() hasn't run yet (e.g. app_wifi_apply()'s boot-time call) - nothing to restore

    // esp_now_init() on an already-alive instance just returns
    // ESP_ERR_ESPNOW_EXIST - harmless. If Wi-Fi's radio bounce did knock it
    // out, this brings it back; either way it's safe to call unconditionally
    // rather than trying to detect which case we're in.
    esp_now_init();

    // A deinit wipes the peer table and receive callback too - restore both
    // rather than waiting for something to notice. Paired bridges' peers
    // aren't re-added here: ensure_peer() re-adds them lazily on next send.
    if (!esp_now_is_peer_exist(kBroadcastMac))
    {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, kBroadcastMac, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }
    esp_now_register_recv_cb(on_espnow_recv);
}

uint32_t espnow_local_device_id()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return (static_cast<uint32_t>(mac[2]) << 24) | (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) | static_cast<uint32_t>(mac[5]);
}

const char *espnow_local_friendly_name()
{
    return g_config.espnow_friendly_name.c_str();
}

void espnow_note_device_seen(uint32_t deviceID, const char *friendlyName, uint8_t deviceType)
{
    for (int i = 0; i < s_known_device_count; i++)
    {
        if (s_known_devices[i].deviceID == deviceID)
        {
            strncpy(s_known_devices[i].friendlyName, friendlyName, sizeof(s_known_devices[i].friendlyName) - 1);
            s_known_devices[i].friendlyName[sizeof(s_known_devices[i].friendlyName) - 1] = '\0';
            s_known_devices[i].deviceType = deviceType;
            s_known_devices[i].lastSeenMs = millis();
            return;
        }
    }

    int slot;
    if (s_known_device_count < kMaxKnownDevices)
    {
        slot = s_known_device_count;
        s_known_device_count++;
    }
    else
    {
        // Table full and this is a new device - evict whichever entry was
        // seen longest ago.
        slot = 0;
        for (int i = 1; i < kMaxKnownDevices; i++)
        {
            if (s_known_devices[i].lastSeenMs < s_known_devices[slot].lastSeenMs)
                slot = i;
        }
    }

    s_known_devices[slot].deviceID = deviceID;
    strncpy(s_known_devices[slot].friendlyName, friendlyName, sizeof(s_known_devices[slot].friendlyName) - 1);
    s_known_devices[slot].friendlyName[sizeof(s_known_devices[slot].friendlyName) - 1] = '\0';
    s_known_devices[slot].deviceType = deviceType;
    s_known_devices[slot].lastSeenMs = millis();
}

const char *espnow_device_type_name(uint8_t deviceType)
{
    switch (deviceType)
    {
    case SM_DEVICE_C64_ULTIMATE_GATEWAY:
        return "C64 Ultimate Gateway";
    case SM_DEVICE_ANTENNA_ROTOR:
        return "Antenna Rotor";
    case SM_DEVICE_AUX_POWER_CONTROLLER:
        return "Aux Power Controller";
    case SM_DEVICE_SWR_METER:
        return "SWR Meter";
    case SM_DEVICE_WEATHER_STATION:
        return "Weather Station";
    case SM_DEVICE_MESHTASTIC_INTERFACE:
        return "Meshtastic Interface";
    case SM_DEVICE_STATUS_DISPLAY:
        return "Status Display";
    case SM_DEVICE_BUTTON_PANEL:
        return "Button Panel";
    case SM_DEVICE_FN_2WIRE_POD:
        return "FN 2-Wire Pod";
    default:
        return "Generic";
    }
}

int espnow_known_device_count()
{
    return s_known_device_count;
}

const SM_KnownDevice *espnow_known_device(int index)
{
    if (index < 0 || index >= s_known_device_count)
        return nullptr;
    return &s_known_devices[index];
}

bool espnow_has_incoming_pair_request()
{
    if (s_have_pending_request && millis() - s_pending_request_ms >= kPendingRequestTimeoutMs)
        s_have_pending_request = false; // requester has already given up and resumed sweeping on its own
    return s_have_pending_request;
}

const SM_IncomingPairRequest *espnow_incoming_pair_request()
{
    return s_have_pending_request ? &s_pending_request : nullptr;
}

void espnow_accept_pair_request()
{
    if (!s_have_pending_request)
        return;

    SM_ChannelAssignment assignment{s_pending_request.nonce, static_cast<uint8_t>(WiFi.channel())};

    ensure_peer(s_pending_request.mac);
    send_to(s_pending_request.mac, SM_PROVISION_CHANNEL, &assignment, sizeof(assignment));

    // Arm the waiting-for-ACK state before anything else below.
    memcpy(s_awaiting_ack_mac, s_pending_request.mac, 6);
    s_awaiting_ack_nonce = s_pending_request.nonce;
    s_awaiting_ack_deviceID = s_pending_request.deviceID;
    s_awaiting_ack_since_ms = millis();
    s_pair_outcome = SM_PAIR_WAITING;

    // Updates the in-RAM list immediately (so Diagnostics reflects it right
    // away) but deliberately does NOT persist to NVS here - see
    // add_or_update_paired_bridge()'s comment for why a synchronous flash
    // write in this exact spot was the real reason SM_PROVISION_ACK kept
    // going unreceived on real hardware, even after this function already
    // armed the waiting-for-ACK state first. Persisting is deferred below,
    // safely past the pod's ACK-burst window.
    add_or_update_paired_bridge(s_pending_request.mac, s_pending_request);
    lv_timer_t *deferred_save = lv_timer_create(save_paired_bridges_deferred_cb, 750, nullptr);
    lv_timer_set_repeat_count(deferred_save, 1);

    s_have_pending_request = false;
}

void espnow_reject_pair_request()
{
    if (!s_have_pending_request)
        return;

    SM_ProvisionNonce rejectMsg{s_pending_request.nonce};
    send_to(s_pending_request.mac, SM_PROVISION_REJECTED, &rejectMsg, sizeof(rejectMsg));
    s_have_pending_request = false;
}

int espnow_paired_bridge_count()
{
    return s_paired_bridge_count;
}

const SM_PairedBridge *espnow_paired_bridge(int index)
{
    if (index < 0 || index >= s_paired_bridge_count)
        return nullptr;
    return &s_paired_bridges[index];
}

void espnow_forget_paired_bridge(int index)
{
    if (index < 0 || index >= s_paired_bridge_count)
        return;

    uint8_t forgottenMac[6];
    memcpy(forgottenMac, s_paired_bridges[index].mac, 6);

    // ESP-NOW is connectionless - there's no "connection" for Forget to
    // break, so without telling the pod, it just keeps broadcasting and
    // replying on its saved channel exactly as before, appearing to "still
    // connect" even though the CYD no longer lists it. This is a single
    // best-effort radio packet with no protocol-level ACK/retry, so plain
    // packet loss can silently drop it even when the pod is otherwise
    // perfectly reachable (confirmed via testing - a lone attempt missed
    // while ping/pong kept working fine) - send it a few times, a few ms
    // apart, since a repeated FACTORY_RESET is harmless (idempotent) and
    // this multiplies the odds at least one copy lands. Still not a
    // guarantee: if the pod's genuinely out of range or powered off, none
    // of these arrive, and it keeps running on its old channel until
    // manually reset (its own 5s button hold) or the CYD sends this again.
    for (int attempt = 0; attempt < 4; attempt++)
    {
        espnow_send_command(index, "FACTORY_RESET");
        delay(20);
    }

    for (int i = index + 1; i < s_paired_bridge_count; i++)
        s_paired_bridges[i - 1] = s_paired_bridges[i];
    s_paired_bridge_count--;

    save_paired_bridges();

    // Discard a same-device pending request rather than leaving it
    // acceptable - the only path that can re-add a paired bridge is
    // espnow_accept_pair_request(), so without this a request that
    // happened to arrive moments before this Forget (dialog possibly
    // still open) could silently undo it on the next tap, which isn't a
    // *new* pair request as far as the user watching the UI is concerned.
    if (s_have_pending_request && memcmp(s_pending_request.mac, forgottenMac, 6) == 0)
        s_have_pending_request = false;
}

SM_PairOutcome espnow_pair_outcome()
{
    if (s_pair_outcome == SM_PAIR_WAITING && millis() - s_awaiting_ack_since_ms >= kAckTimeoutMs)
        s_pair_outcome = SM_PAIR_TIMED_OUT;
    return s_pair_outcome;
}

void espnow_clear_pair_outcome()
{
    s_pair_outcome = SM_PAIR_NONE;
}

const SM_LastPing *espnow_last_ping()
{
    return s_have_last_ping ? &s_last_ping : nullptr;
}

const SM_FnTxStatus *espnow_last_fn_tx_status()
{
    return s_have_last_fn_tx_status ? &s_last_fn_tx_status : nullptr;
}

const char *espnow_message_type_name(uint8_t messageType)
{
    switch (messageType)
    {
    case SM_DISCOVER:
        return "DISCOVER";
    case SM_ANNOUNCE:
        return "ANNOUNCE";
    case SM_HEARTBEAT:
        return "HEARTBEAT";
    case SM_CAPABILITIES:
        return "CAPABILITIES";
    case SM_CAPABILITIES_REQUEST:
        return "CAPABILITIES_REQUEST";
    case SM_PROVISION_REQUEST:
        return "PROVISION_REQUEST";
    case SM_PROVISION_HOLD:
        return "PROVISION_HOLD";
    case SM_PROVISION_CHANNEL:
        return "PROVISION_CHANNEL";
    case SM_PROVISION_REJECTED:
        return "PROVISION_REJECTED";
    case SM_PROVISION_ACK:
        return "PROVISION_ACK";
    case SM_STATUS:
        return "STATUS";
    case SM_VALUE:
        return "VALUE";
    case SM_SUBSCRIBE:
        return "SUBSCRIBE";
    case SM_UNSUBSCRIBE:
        return "UNSUBSCRIBE";
    case SM_PING:
        return "PING";
    case SM_PONG:
        return "PONG";
    case SM_SET_VALUE:
        return "SET_VALUE";
    case SM_COMMAND:
        return "COMMAND";
    case SM_ACK:
        return "ACK";
    case SM_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

int espnow_traffic_log_count()
{
    return s_traffic_log_count;
}

const SM_TrafficLogEntry *espnow_traffic_log_entry(int indexFromNewest)
{
    if (indexFromNewest < 0 || indexFromNewest >= s_traffic_log_count)
        return nullptr;
    // s_traffic_log_next is the *next write* slot, so the most recent
    // entry is one before it; walk backward (with wraparound) from there.
    int idx = (s_traffic_log_next - 1 - indexFromNewest + kMaxTrafficLogEntries * 2) % kMaxTrafficLogEntries;
    return &s_traffic_log[idx];
}

void espnow_send_test_ping()
{
    SM_PingPayload ping{millis()};
    send_broadcast(SM_PING, &ping, sizeof(ping));
}

bool espnow_send_command(int pairedBridgeIndex, const char *commandName, int32_t argument)
{
    const SM_PairedBridge *bridge = espnow_paired_bridge(pairedBridgeIndex);
    if (bridge == nullptr)
        return false;

    ensure_peer(bridge->mac);

    SM_CommandPayload cmd = {};
    strncpy(cmd.commandName, commandName, sizeof(cmd.commandName) - 1);
    cmd.argument = argument;

    return send_to(bridge->mac, SM_COMMAND, &cmd, sizeof(cmd));
}
