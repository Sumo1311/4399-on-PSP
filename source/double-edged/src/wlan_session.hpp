#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace doubleedged {
enum class WlanStatus : std::uint8_t {
    Idle,Searching,Waiting,Connecting,Active,WlanOff,Timeout,PeerLeft,Incompatible,Desynchronized,Failed
};
struct WlanSetup {
    std::uint32_t seed=42,source_crc=0,executable_crc=0;
    std::array<std::uint8_t,96> preferences{};
};
struct WlanInput {
    // Up/down/left/right/attack/jump/back/pause; menu selection belongs to P1.
    std::uint16_t keys=0,analog_x=128,analog_y=128;
    bool reserved=false;
};

// Two-PSP transport. Each source frame waits for both ordinary controller
// inputs; the source's30Hz clock and all timer intervals remain unchanged.
class WlanSession {
public:
    static constexpr std::uint32_t InputDelay=2;
    ~WlanSession();
    void start(bool host,const WlanSetup& setup,std::uint64_t now);
    void close();
    void pump(std::uint64_t now);
    WlanStatus status() const{return status_;}
    bool busy() const{return phase_!=Phase::Closed;}
    bool active() const{return status_==WlanStatus::Active;}
    bool host() const{return host_;}
    const WlanSetup& setup() const{return setup_;}
    std::uint32_t frame() const{return frame_;}
    bool submit(const WlanInput& input);
    bool wants_input() const;
    std::uint32_t next_input_frame() const{return next_local_;}
    bool take_frame(WlanInput& first,WlanInput& second);
    void checksum(std::uint32_t frame,std::uint32_t value);
    bool announce_checksum();
    bool confirmed(std::uint32_t frame) const;
    int error() const{return error_;}
private:
    enum class Phase {Closed,Scan,Group,Listen,FindPeer,Dial,Hello,Welcome,Ready,Play};
    enum class Message:std::uint8_t {Hello=1,Welcome,Ready,Input,Checkpoint};
    using Packet=std::array<std::uint8_t,128>;
    static constexpr std::size_t InputSlots=8,HashSlots=32;
    static void ctl_event(int event,int error,void* context);
    bool initialize();
    void fail(WlanStatus status,int error=0);
    void scan(std::uint64_t now);
    void connect(std::uint64_t now);
    void retry_dial(std::uint64_t now);
    void exchange(std::uint64_t now);
    void receive(std::uint64_t now);
    void activate(std::uint64_t now);
    void queue(Message type);
    void seal();
    Phase phase_=Phase::Closed;
    WlanStatus status_=WlanStatus::Idle;
    bool host_=false,common_loaded_=false,adhoc_loaded_=false;
    bool net_initialized_=false,adhoc_initialized_=false,ctl_initialized_=false;
    int callback_=-1,listener_=-1,socket_=-1,error_=0;
    std::atomic<bool> scan_finished_{false};
    std::array<unsigned char,8> local_mac_{},room_mac_{};
    std::uint64_t deadline_=0,next_attempt_=0;
    WlanSetup setup_{};
    Packet outgoing_{},incoming_{};
    std::size_t sent_=outgoing_.size(),received_=0;
    std::array<WlanInput,InputSlots> local_{},remote_{};
    struct Hash {std::uint32_t frame=0,value=0;bool valid=false;};
    std::array<Hash,HashSlots> own_hashes_{},peer_hashes_{};
    std::uint32_t frame_=0,next_local_=InputDelay,next_remote_=InputDelay;
};
}
