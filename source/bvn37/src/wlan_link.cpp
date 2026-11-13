// Adapted from the PSP Ad Hoc transport in Stick Figure Badminton 2.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wlan_link.hpp"
#include "game_settings.hpp"

#include <algorithm>
#include <cstring>
#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>

namespace bvn {
namespace {

constexpr char Group[] = "BVN37PSP";
constexpr unsigned short Port = 3731;
constexpr unsigned BufferBytes = 4096;
constexpr unsigned RetryMicros = 200000;
constexpr int Retries = 10;
constexpr int Nonblocking = 1;
constexpr int ScanEvent = 3;
constexpr int Connected = 1;
constexpr int WouldBlock = static_cast<int>(0x80410709U);
constexpr int Disconnected = static_cast<int>(0x8041070cU);
constexpr int TimedOut = static_cast<int>(0x80410715U);
constexpr int Refused = static_cast<int>(0x80410718U);
constexpr std::uint64_t LobbyWait = 45000000;
constexpr std::uint64_t ScanInterval = 1000000;
constexpr std::uint64_t DialInterval = 250000;
constexpr std::uint64_t PeerWait = 12000000;
constexpr std::uint32_t Magic = 0x334e5642;  // BVN3
constexpr std::uint32_t Protocol = 100;
constexpr int Payload = 12;
constexpr unsigned Buttons = PSP_CTRL_START | PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT |
    PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_SQUARE;
constexpr std::uint32_t PacketButtons = Buttons | (Buttons << 16);

std::uint32_t readWord(const std::uint8_t* bytes) {
    std::uint32_t word = 0;
    for (unsigned i = 0; i < 4; ++i) word |= std::uint32_t(bytes[i]) << (8 * i);
    return word;
}

void putWord(std::uint8_t* bytes, std::uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(word >> (8 * i));
}

WlanPad unpack(std::uint32_t value) {
    return {value & 0xffff, value >> 16};
}

}  // namespace

WlanSession::~WlanSession() {
    close();
}

void WlanSession::start(bool host, bool team, std::uint32_t seed, std::uint32_t catalog, std::uint64_t now,unsigned rules) {
    close();
    if (sceWlanGetSwitchState() == 0) {
        fail(WlanStatus::WlanOff);
        return;
    }
    if (!initialize()) {
        return;
    }
    host_ = host;
    team_ = host && team;
    seed_ = seed;
    catalog_ = catalog;
    GameSettings check;
    if(!check.applyWireRules(rules)){fail(WlanStatus::Incompatible);return;}
    rules_=rules;
    deadline_ = now + LobbyWait;
    if (host) {
        const int result = sceNetAdhocctlCreate(Group);
        if (result < 0) {
            fail(WlanStatus::Failed, result);
            return;
        }
        phase_ = Phase::Group;
        status_ = WlanStatus::Waiting;
    } else {
        nextAttempt_ = now;
        phase_ = Phase::Scan;
        status_ = WlanStatus::Searching;
    }
}

bool WlanSession::initialize() {
    productStruct product{0, {'B', 'V', 'N', '3', '7', '0', '0', '0', '1'}, {}};
    int result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if ((commonLoaded_ = result == 0)) {
        result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
        if ((adhocLoaded_ = result == 0)) {
            result = sceNetInit(128 * 1024, 0x2a, 0x1000, 0x2a, 0x1000);
        }
    }
    if (result < 0) {
        fail(WlanStatus::Failed, result);
        return false;
    }
    netInitialized_ = true;
    result = sceNetAdhocInit();
    if ((adhocInitialized_ = result == 0)) {
        result = sceNetAdhocctlInit(0x2000, 0x30, &product);
        if ((ctlInitialized_ = result == 0)) {
            callback_ = result = sceNetAdhocctlAddHandler(&WlanSession::ctlEvent, this);
            if (result >= 0) {
                result = sceWlanGetEtherAddr(localMac_.data());
            }
        }
    }
    if (result < 0) {
        fail(WlanStatus::Failed, result);
        return false;
    }
    return true;
}

void WlanSession::close() {
    if (socket_ >= 0) sceNetAdhocPtpClose(socket_, 0);
    if (listener_ >= 0) sceNetAdhocPtpClose(listener_, 0);
    if (callback_ >= 0) sceNetAdhocctlDelHandler(callback_);
    if (ctlInitialized_) {
        sceNetAdhocctlDisconnect();
        sceNetAdhocctlTerm();
    }
    if (adhocInitialized_) sceNetAdhocTerm();
    if (netInitialized_) sceNetTerm();
    if (adhocLoaded_) sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
    if (commonLoaded_) sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);

    socket_ = listener_ = callback_ = -1;
    ctlInitialized_ = adhocInitialized_ = netInitialized_ = adhocLoaded_ = commonLoaded_ = false;
    scanFinished_.store(false, std::memory_order_relaxed);
    phase_ = Phase::Closed;
    status_ = WlanStatus::Idle;
    team_ = false;
    error_ = 0;
    received_ = 0;
    sent_ = outgoing_.size();
    frame_ = 0;
    nextLocal_ = nextRemote_ = InputDelay;
    local_.fill(0);
    remote_.fill(0);
    localMenu_.fill(0);remoteMenu_.fill(0);
}

void WlanSession::fail(WlanStatus status, int error) {
    close();
    status_ = status;
    error_ = error;
}

void WlanSession::ctlEvent(int event, int error, void* context) {
    if (event == ScanEvent && error == 0) {
        auto& link = *static_cast<WlanSession*>(context);
        link.scanFinished_.store(true, std::memory_order_relaxed);
    }
}

void WlanSession::scan(std::uint64_t now) {
    if (scanFinished_.load(std::memory_order_relaxed)) {
        scanFinished_.store(false, std::memory_order_relaxed);
        std::array<SceNetAdhocctlScanInfo, 16> rooms{};
        int length = sizeof(rooms);
        if (sceNetAdhocctlGetScanInfo(&length, rooms.data()) == 0) {
            const int count = std::min(length / static_cast<int>(sizeof(rooms[0])),
                                       static_cast<int>(rooms.size()));
            for (int i = 0; i < count; ++i) {
                auto& room = rooms[static_cast<std::size_t>(i)];
                if (std::memcmp(room.name, Group, sizeof(room.name)) == 0) {
                    std::memcpy(roomMac_.data(), room.bssid, sizeof(room.bssid));
                    const int result = sceNetAdhocctlJoin(&room);
                    if (result < 0) {
                        fail(WlanStatus::Failed, result);
                        return;
                    }
                    phase_ = Phase::Group;
                    status_ = WlanStatus::Connecting;
                    return;
                }
            }
        }
    }
    int state = -1;
    if (now >= nextAttempt_ && sceNetAdhocctlGetState(&state) == 0 && state == 0) {
        nextAttempt_ = now + ScanInterval;
        sceNetAdhocctlScan();
    }
}

void WlanSession::retryDial(std::uint64_t now) {
    sceNetAdhocPtpClose(socket_, 0);
    socket_ = -1;
    phase_ = Phase::FindPeer;
    nextAttempt_ = now + DialInterval;
    sent_ = outgoing_.size();
    received_ = 0;
}

void WlanSession::connect(std::uint64_t now) {
    if (phase_ == Phase::FindPeer) {
        if (now < nextAttempt_) {
            return;
        }
        // The peer list is an ABI-owned linked list. Its timestamp size varies by SDK.
        alignas(8) std::array<unsigned char, 16 * 160> peers{};
        int length = sizeof(peers);
        if (sceNetAdhocctlGetPeerList(&length, peers.data()) < 0 ||
            length < static_cast<int>(sizeof(SceNetAdhocctlPeerInfo))) {
            return;
        }
        const auto* first = reinterpret_cast<const SceNetAdhocctlPeerInfo*>(peers.data());
        const auto* host = first;
        for (auto* peer = first; peer; peer = peer->next) {
            if (std::memcmp(peer->mac, roomMac_.data(), sizeof(peer->mac)) == 0) {
                host = peer;
                break;
            }
        }
        std::array<unsigned char, 8> mac{};
        std::memcpy(mac.data(), host->mac, sizeof(host->mac));
        socket_ = sceNetAdhocPtpOpen(localMac_.data(), 0, mac.data(), Port, BufferBytes,
                                     RetryMicros, Retries, 0);
        if (socket_ < 0) {
            fail(WlanStatus::Failed, socket_);
            return;
        }
        phase_ = Phase::Dial;
    }
    const int result = sceNetAdhocPtpConnect(socket_, 0, Nonblocking);
    if (result == 0) {
        phase_ = Phase::Welcome;
        queue(Message::Hello);
        putWord(outgoing_.data() + Payload + 4, catalog_);
        outgoing_[Payload+9]=rules_;
    } else if (result == Refused || result == TimedOut || result == Disconnected) {
        retryDial(now);
    } else if (result != WouldBlock) {
        fail(WlanStatus::Failed, result);
    }
}

void WlanSession::pump(std::uint64_t now) {
    if (!busy()) {
        return;
    }
    if (sceWlanGetSwitchState() == 0) {
        fail(WlanStatus::WlanOff);
        return;
    }
    if (now > deadline_) {
        fail(active() ? WlanStatus::PeerLeft : WlanStatus::Timeout);
        return;
    }
    switch (phase_) {
    case Phase::Scan:
        scan(now);
        break;
    case Phase::Group: {
        int state = 0;
        if (sceNetAdhocctlGetState(&state) < 0 || state != Connected) {
            break;
        }
        if (!host_) {
            phase_ = Phase::FindPeer;
            nextAttempt_ = now;
            break;
        }
        listener_ = sceNetAdhocPtpListen(localMac_.data(), Port, BufferBytes, RetryMicros, Retries, 1, 0);
        if (listener_ < 0) {
            fail(WlanStatus::Failed, listener_);
        } else {
            phase_ = Phase::Listen;
        }
        break;
    }
    case Phase::Listen: {
        std::array<unsigned char, 8> mac{};
        unsigned short port = 0;
        const int result = sceNetAdhocPtpAccept(listener_, mac.data(), &port, 0, Nonblocking);
        if (result >= 0) {
            socket_ = result;
            sceNetAdhocPtpClose(listener_, 0);
            listener_ = -1;
            phase_ = Phase::Hello;
            status_ = WlanStatus::Connecting;
            deadline_ = now + PeerWait;
        } else if (result != WouldBlock && result != TimedOut) {
            fail(WlanStatus::Failed, result);
        }
        break;
    }
    case Phase::FindPeer:
    case Phase::Dial:
        connect(now);
        break;
    case Phase::Hello:
    case Phase::Welcome:
    case Phase::Ready:
    case Phase::Play:
        exchange(now);
        break;
    case Phase::Closed:
        break;
    }
}

void WlanSession::queue(Message type) {
    outgoing_.fill(0);
    putWord(outgoing_.data(), Magic);
    putWord(outgoing_.data() + 4, Protocol);
    outgoing_[8] = static_cast<std::uint8_t>(type);
    sent_ = 0;
}

void WlanSession::exchange(std::uint64_t now) {
    if (sent_ < outgoing_.size()) {
        int length = static_cast<int>(outgoing_.size() - sent_);
        const int result = sceNetAdhocPtpSend(socket_, outgoing_.data() + sent_, &length, 0, Nonblocking);
        if (result == 0) {
            sent_ += static_cast<std::size_t>(length);
        } else if (result != WouldBlock && result != TimedOut) {
            if (phase_ == Phase::Welcome && result == Disconnected) {
                retryDial(now);
            } else {
                fail(WlanStatus::PeerLeft, result);
            }
            return;
        }
    }
    // A single outgoing packet also holds the pending handshake response.
    if (!active() && sent_ < outgoing_.size()) {
        return;
    }
    for (unsigned read = 0; read < InputSlots && busy(); ++read) {
        int length = static_cast<int>(incoming_.size() - received_);
        const int result = sceNetAdhocPtpRecv(socket_, incoming_.data() + received_, &length, 0, Nonblocking);
        if (result != 0 || length <= 0) {
            if (result != WouldBlock && result != TimedOut) {
                if (phase_ == Phase::Welcome && (result == Disconnected || result == 0)) {
                    retryDial(now);
                } else {
                    fail(WlanStatus::PeerLeft, result);
                }
            }
            break;
        }
        received_ += static_cast<std::size_t>(length);
        if (received_ == incoming_.size()) {
            received_ = 0;
            receive(now);
            if (!active() && sent_ < outgoing_.size()) {
                break;
            }
        }
    }
}

void WlanSession::activate(std::uint64_t now) {
    status_ = WlanStatus::Active;
    phase_ = Phase::Play;
    deadline_ = now + PeerWait;
}

void WlanSession::receive(std::uint64_t now) {
    if (readWord(incoming_.data()) != Magic || readWord(incoming_.data() + 4) != Protocol) {
        fail(WlanStatus::Incompatible);
        return;
    }
    const auto message = static_cast<Message>(incoming_[8]);
    if ((phase_ == Phase::Hello && message == Message::Hello) ||
        (phase_ == Phase::Welcome && message == Message::Welcome)) {
        const auto team = incoming_[Payload + 8];
        const auto rules=incoming_[Payload+9];GameSettings check;
        if (readWord(incoming_.data() + Payload + 4) != catalog_ || team > 1 || !check.applyWireRules(rules)) {
            fail(WlanStatus::Incompatible);
            return;
        }
        if (host_) {
            queue(Message::Welcome);
            putWord(outgoing_.data() + Payload, seed_);
            putWord(outgoing_.data() + Payload + 4, catalog_);
            outgoing_[Payload + 8] = team_;
            outgoing_[Payload + 9] = rules_;
            phase_ = Phase::Ready;
        } else {
            team_ = team != 0;
            rules_=rules;
            seed_ = readWord(incoming_.data() + Payload);
            queue(Message::Ready);
            putWord(outgoing_.data() + Payload, seed_);
            activate(now);
        }
        return;
    }
    if (phase_ == Phase::Ready && message == Message::Ready && readWord(incoming_.data() + Payload) == seed_) {
        activate(now);
        return;
    }
    if (active() && message == Message::Input) {
        const auto sequence = readWord(incoming_.data() + Payload);
        const auto mask = readWord(incoming_.data() + Payload + 4);
        const auto menu=readWord(incoming_.data()+Payload+8);
        if (sequence == nextRemote_ && sequence - frame_ < InputSlots &&
            (mask & ~PacketButtons) == 0 && (menu&~PacketButtons)==0) {
            const auto index=nextRemote_++%InputSlots;
            remote_[index] = mask;remoteMenu_[index]=menu;
            deadline_ = now + PeerWait;
            return;
        }
    }
    fail(WlanStatus::Incompatible);
}

bool WlanSession::submit(WlanPad input) {
    if (!active() || nextLocal_ - frame_ > InputDelay || sent_ != outgoing_.size()) {
        return false;
    }
    const auto mask = (input.held & Buttons) | ((input.pressed & Buttons) << 16);
    queue(Message::Input);
    putWord(outgoing_.data() + Payload, nextLocal_);
    putWord(outgoing_.data() + Payload + 4, mask);
    const auto menu=(input.menuHeld&Buttons)|((input.menuPressed&Buttons)<<16);
    putWord(outgoing_.data()+Payload+8,menu);
    const auto index=nextLocal_++%InputSlots;local_[index]=mask;localMenu_[index]=menu;
    return true;
}

bool WlanSession::takeFrame(WlanPad& first, WlanPad& second) {
    // Already queued input can play even while the next send is still blocked.
    if (!active() || nextLocal_ == frame_ || nextRemote_ == frame_) {
        return false;
    }
    auto local = unpack(local_[frame_ % InputSlots]);
    auto remote = unpack(remote_[frame_ % InputSlots]);
    const auto localMenu=unpack(localMenu_[frame_%InputSlots]),remoteMenu=unpack(remoteMenu_[frame_%InputSlots]);
    local.menuHeld=localMenu.held;local.menuPressed=localMenu.pressed;
    remote.menuHeld=remoteMenu.held;remote.menuPressed=remoteMenu.pressed;
    first = host_ ? local : remote;
    second = host_ ? remote : local;
    ++frame_;
    return true;
}

}  // namespace bvn
