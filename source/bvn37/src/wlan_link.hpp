// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bvn {

enum class WlanStatus : std::uint8_t {
    Idle, Searching, Waiting, Connecting, Active, WlanOff, Timeout, PeerLeft, Incompatible, Failed,
};

struct WlanPad {
    unsigned held = 0, pressed = 0;
    unsigned menuHeld=0,menuPressed=0;
};

// Both sides advance selection and combat from the same pair of inputs.
class WlanSession {
public:
    ~WlanSession();
    void start(bool host, bool team, std::uint32_t seed, std::uint32_t catalog, std::uint64_t now,unsigned rules=9);
    void close();
    void pump(std::uint64_t now);

    WlanStatus status() const { return status_; }
    bool busy() const { return phase_ != Phase::Closed; }
    bool active() const { return status_ == WlanStatus::Active; }
    bool host() const { return host_; }
    std::uint32_t seed() const { return seed_; }
    bool team() const { return team_; }
    unsigned rules()const{return rules_;}
    int error() const { return error_; }

    static constexpr std::uint32_t InputDelay = 1;
    std::uint32_t frame() const { return frame_; }
    bool submit(WlanPad input);
    bool takeFrame(WlanPad& first, WlanPad& second);

private:
    enum class Phase { Closed, Scan, Group, Listen, FindPeer, Dial, Hello, Welcome, Ready, Play };
    enum class Message : std::uint8_t { Hello = 1, Welcome, Ready, Input };
    using Packet = std::array<std::uint8_t, 24>;
    static constexpr std::size_t InputSlots = 8;

    static void ctlEvent(int event, int error, void* context);
    bool initialize();
    void fail(WlanStatus status, int error = 0);
    void scan(std::uint64_t now);
    void connect(std::uint64_t now);
    void retryDial(std::uint64_t now);
    void exchange(std::uint64_t now);
    void receive(std::uint64_t now);
    void activate(std::uint64_t now);
    void queue(Message type);

    Phase phase_ = Phase::Closed;
    WlanStatus status_ = WlanStatus::Idle;
    bool host_ = false;
    bool team_ = false;
    bool commonLoaded_ = false;
    bool adhocLoaded_ = false;
    bool netInitialized_ = false;
    bool adhocInitialized_ = false;
    bool ctlInitialized_ = false;
    int callback_ = -1;
    int listener_ = -1;
    int socket_ = -1;
    std::atomic<bool> scanFinished_{false};
    std::array<unsigned char, 8> localMac_{};
    std::array<unsigned char, 8> roomMac_{};
    std::uint64_t deadline_ = 0;
    std::uint64_t nextAttempt_ = 0;
    std::uint32_t seed_ = 0, catalog_ = 0;
    unsigned rules_=9;
    int error_ = 0;

    Packet outgoing_{};
    Packet incoming_{};
    std::size_t sent_ = outgoing_.size();
    std::size_t received_ = 0;
    std::array<std::uint32_t, InputSlots> local_{};
    std::array<std::uint32_t, InputSlots> remote_{};
    std::array<std::uint32_t, InputSlots> localMenu_{},remoteMenu_{};
    std::uint32_t frame_ = 0;
    std::uint32_t nextLocal_ = InputDelay;
    std::uint32_t nextRemote_ = InputDelay;
};

}  // namespace bvn
