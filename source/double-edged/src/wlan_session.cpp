// Ad-hoc group/PTP lifecycle adapted from the user's existing Elf Story port.
// Double Edged uses its own product/group/protocol identity and source checks.
#include "wlan_session.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>
#include <zlib.h>
extern "C" void double_edged_link_trace(const char*);
namespace doubleedged {
namespace {
constexpr char Group[]="DEGDWLAN";
constexpr unsigned short Port=3142;
constexpr unsigned BufferBytes=4096,RetryMicros=200000;
constexpr int Retries=10,Nonblocking=1,ScanEvent=3,Connected=1;
constexpr int WouldBlock=int(0x80410709U),Disconnected=int(0x8041070cU),TimedOut=int(0x80410715U),Refused=int(0x80410718U);
constexpr std::uint64_t LobbyWait=45000000,ScanInterval=1000000,DialInterval=250000,PeerWait=12000000,LoadWait=90000000;
constexpr std::uint32_t Magic=0x4b4c4544,Protocol=0x00010001;
void put16(unsigned char* p,std::uint16_t n){p[0]=n;p[1]=n>>8;}
void put32(unsigned char* p,std::uint32_t n){for(unsigned i=0;i<4;++i)p[i]=n>>(i*8);}
std::uint16_t get16(const unsigned char* p){return p[0]|std::uint16_t(p[1])<<8;}
std::uint32_t get32(const unsigned char* p){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(p[i])<<(i*8);return n;}
}
WlanSession::~WlanSession(){close();}
void WlanSession::start(bool host,const WlanSetup& setup,std::uint64_t now){
    close();error_=0;
    if(sceWlanGetSwitchState()==0){fail(WlanStatus::WlanOff);return;}
    if(!initialize())return;
    host_=host;setup_=setup;deadline_=now+LobbyWait;
    if(host){int result=sceNetAdhocctlCreate(Group);if(result<0){fail(WlanStatus::Failed,result);return;}phase_=Phase::Group;status_=WlanStatus::Waiting;}
    else{next_attempt_=now;phase_=Phase::Scan;status_=WlanStatus::Searching;}
}
bool WlanSession::initialize() {
    productStruct product{0, {'D', 'E', 'D', 'G', '0', '0', '0', '0', '1'}, {}};
    int result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    if ((common_loaded_ = result == 0)) {
        result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
        if ((adhoc_loaded_ = result == 0)) {
            result = sceNetInit(128 * 1024, 0x2a, 0x1000, 0x2a, 0x1000);
        }
    }
    if (result < 0) {
        fail(WlanStatus::Failed, result);
        return false;
    }
    net_initialized_ = true;
    result = sceNetAdhocInit();
    if ((adhoc_initialized_ = result == 0)) {
        result = sceNetAdhocctlInit(0x2000, 0x30, &product);
        if ((ctl_initialized_ = result == 0)) {
            callback_ = result = sceNetAdhocctlAddHandler(&WlanSession::ctl_event, this);
            if (result >= 0) {
                result = sceWlanGetEtherAddr(local_mac_.data());
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
    if (ctl_initialized_) {
        sceNetAdhocctlDisconnect();
        sceNetAdhocctlTerm();
    }
    if (adhoc_initialized_) sceNetAdhocTerm();
    if (net_initialized_) sceNetTerm();
    if (adhoc_loaded_) sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
    if (common_loaded_) sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);

    socket_ = listener_ = callback_ = -1;
    ctl_initialized_ = adhoc_initialized_ = net_initialized_ = adhoc_loaded_ = common_loaded_ = false;
    scan_finished_.store(false, std::memory_order_relaxed);
    phase_ = Phase::Closed;
    status_ = WlanStatus::Idle;
    received_ = 0;
    sent_ = outgoing_.size();
    frame_ = 0;
    next_local_ = next_remote_ = InputDelay;
    local_.fill({});
    remote_.fill({});
    own_hashes_.fill({});
    peer_hashes_.fill({});
}

void WlanSession::fail(WlanStatus status, int error) {
    char message[128];std::snprintf(message,sizeof(message),"link failure phase=%d status=%d error=%08x",int(phase_),int(status),unsigned(error));double_edged_link_trace(message);
    close();
    status_ = status;error_=error;
}

void WlanSession::ctl_event(int event, int error, void* context) {
    if (event == ScanEvent && error == 0) {
        auto& link = *static_cast<WlanSession*>(context);
        link.scan_finished_.store(true, std::memory_order_relaxed);
    }
}

void WlanSession::scan(std::uint64_t now) {
    if (scan_finished_.load(std::memory_order_relaxed)) {
        scan_finished_.store(false, std::memory_order_relaxed);
        std::array<SceNetAdhocctlScanInfo, 16> rooms{};
        int length = sizeof(rooms);
        if (sceNetAdhocctlGetScanInfo(&length, rooms.data()) == 0) {
            const int count = std::min(length / static_cast<int>(sizeof(rooms[0])),
                                       static_cast<int>(rooms.size()));
            for (int i = 0; i < count; ++i) {
                auto& room = rooms[static_cast<std::size_t>(i)];
                if (std::memcmp(room.name, Group, sizeof(room.name)) == 0) {
                    std::memcpy(room_mac_.data(), room.bssid, sizeof(room.bssid));
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
    if (now >= next_attempt_ && sceNetAdhocctlGetState(&state) == 0 && state == 0) {
        next_attempt_ = now + ScanInterval;
        sceNetAdhocctlScan();
    }
}

void WlanSession::retry_dial(std::uint64_t now) {
    sceNetAdhocPtpClose(socket_, 0);
    socket_ = -1;
    phase_ = Phase::FindPeer;
    next_attempt_ = now + DialInterval;
    sent_ = outgoing_.size();
    received_ = 0;
}

void WlanSession::connect(std::uint64_t now) {
    if (phase_ == Phase::FindPeer) {
        if (now < next_attempt_) {
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
            if (std::memcmp(peer->mac, room_mac_.data(), sizeof(peer->mac)) == 0) {
                host = peer;
                break;
            }
        }
        std::array<unsigned char, 8> mac{};
        std::memcpy(mac.data(), host->mac, sizeof(host->mac));
        socket_ = sceNetAdhocPtpOpen(local_mac_.data(), 0, mac.data(), Port, BufferBytes,
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
    } else if (result == Refused || result == TimedOut || result == Disconnected) {
        retry_dial(now);
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
            next_attempt_ = now;
            break;
        }
        listener_ = sceNetAdhocPtpListen(local_mac_.data(), Port, BufferBytes, RetryMicros, Retries, 1, 0);
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

void WlanSession::queue(Message type){
    outgoing_.fill(0);put32(outgoing_.data(),Magic);put32(outgoing_.data()+4,Protocol);
    outgoing_[8]=static_cast<std::uint8_t>(type);outgoing_[9]=host_?1:0;sent_=0;
    if(type==Message::Hello || type==Message::Welcome || type==Message::Ready){
        put32(outgoing_.data()+12,setup_.source_crc);put32(outgoing_.data()+16,setup_.executable_crc);
    }
    seal();
}
void WlanSession::seal(){put32(outgoing_.data()+124,crc32(0,outgoing_.data(),124));}
void WlanSession::exchange(std::uint64_t now){
    if(sent_<outgoing_.size()){
        int length=outgoing_.size()-sent_;
        int result=sceNetAdhocPtpSend(socket_,outgoing_.data()+sent_,&length,0,Nonblocking);
        if(result==0)sent_+=length;
        else if(result!=WouldBlock && result!=TimedOut){
            if(phase_==Phase::Welcome && result==Disconnected)retry_dial(now);
            else fail(WlanStatus::PeerLeft,result);
            return;
        }
    }
    if(!active() && sent_<outgoing_.size())return;
    for(unsigned read=0;read<InputSlots && busy();++read){
        int length=incoming_.size()-received_;
        int result=sceNetAdhocPtpRecv(socket_,incoming_.data()+received_,&length,0,Nonblocking);
        if(result!=0 || length<=0){
            if(result!=WouldBlock && result!=TimedOut){
                if(phase_==Phase::Welcome && (result==Disconnected || result==0))retry_dial(now);
                else fail(WlanStatus::PeerLeft,result);
            }
            break;
        }
        received_+=length;
        if(received_==incoming_.size()){
            received_=0;receive(now);
            if(!active() && sent_<outgoing_.size())break;
        }
    }
}
void WlanSession::activate(std::uint64_t now){
    status_=WlanStatus::Active;phase_=Phase::Play;deadline_=now+LoadWait;
    double_edged_link_trace(host_?"link active P1 host":"link active P2 guest");
}
void WlanSession::receive(std::uint64_t now){
    auto p=incoming_.data();
    if(get32(p)!=Magic || get32(p+4)!=Protocol || p[9]!=(host_?0:1) || p[10] || p[11] ||
       get32(p+124)!=crc32(0,p,124)){fail(WlanStatus::Incompatible);return;}
    auto type=static_cast<Message>(p[8]);
    if(type==Message::Hello || type==Message::Welcome || type==Message::Ready){
        if(get32(p+12)!=setup_.source_crc || get32(p+16)!=setup_.executable_crc){fail(WlanStatus::Incompatible);return;}
    }
    if(phase_==Phase::Hello && type==Message::Hello){
        queue(Message::Welcome);put32(outgoing_.data()+20,setup_.seed);
        std::memcpy(outgoing_.data()+24,setup_.preferences.data(),setup_.preferences.size());seal();
        phase_=Phase::Ready;deadline_=now+LoadWait;return;
    }
    if(phase_==Phase::Welcome && type==Message::Welcome){
        setup_.seed=get32(p+20);
        if(!setup_.seed || setup_.seed>0x7fffffffU){fail(WlanStatus::Incompatible);return;}
        std::memcpy(setup_.preferences.data(),p+24,setup_.preferences.size());
        queue(Message::Ready);put32(outgoing_.data()+20,setup_.seed);seal();activate(now);return;
    }
    if(phase_==Phase::Ready && type==Message::Ready){
        if(get32(p+20)!=setup_.seed){fail(WlanStatus::Incompatible);return;}
        activate(now);return;
    }
    if(active() && type==Message::Input){
        auto sequence=get32(p+12);auto keys=get16(p+16);auto x=get16(p+18);auto y=get16(p+20);
        auto hash_frame=get32(p+24),hash_value=get32(p+28);
        if(sequence!=next_remote_ || sequence-frame_>=InputSlots || keys>255 || x>=256 || y>=256 ||
           p[22]!=0 || p[23]!=1 || hash_frame+InputDelay!=sequence){fail(WlanStatus::Incompatible);return;}
        remote_[next_remote_++%InputSlots]={keys,x,y,p[22]!=0};
        peer_hashes_[hash_frame%HashSlots]={hash_frame,hash_value,true};
        const auto& own=own_hashes_[hash_frame%HashSlots];
        if(own.valid && own.frame==hash_frame && own.value!=hash_value){fail(WlanStatus::Desynchronized);return;}
        deadline_=now+PeerWait;return;
    }
    if(active() && type==Message::Checkpoint){
        auto hash_frame=get32(p+12),hash_value=get32(p+16);
        if(hash_frame>frame_+InputSlots || frame_>hash_frame+HashSlots){fail(WlanStatus::Incompatible);return;}
        peer_hashes_[hash_frame%HashSlots]={hash_frame,hash_value,true};
        const auto& own=own_hashes_[hash_frame%HashSlots];
        if(own.valid && own.frame==hash_frame && own.value!=hash_value){fail(WlanStatus::Desynchronized);return;}
        deadline_=now+PeerWait;return;
    }
    fail(WlanStatus::Incompatible);
}
bool WlanSession::submit(const WlanInput& input){
    if(!wants_input())return false;
    const auto& hash=own_hashes_[frame_%HashSlots];
    if(!hash.valid || hash.frame!=frame_)return false;
    if(input.keys>255 || input.analog_x>=256 || input.analog_y>=256 || input.reserved){fail(WlanStatus::Failed);return false;}
    queue(Message::Input);put32(outgoing_.data()+12,next_local_);put16(outgoing_.data()+16,input.keys);
    put16(outgoing_.data()+18,input.analog_x);put16(outgoing_.data()+20,input.analog_y);outgoing_[22]=input.reserved;outgoing_[23]=1;
    put32(outgoing_.data()+24,hash.frame);put32(outgoing_.data()+28,hash.value);seal();
    local_[next_local_++%InputSlots]=input;return true;
}
bool WlanSession::wants_input() const{
    const auto& hash=own_hashes_[frame_%HashSlots];
    return active() && next_local_==frame_+InputDelay && sent_==outgoing_.size() && hash.valid && hash.frame==frame_;
}
bool WlanSession::take_frame(WlanInput& first,WlanInput& second){
    if(!active() || next_local_==frame_+InputDelay || next_remote_==frame_)return false;
    auto local=local_[frame_%InputSlots],remote=remote_[frame_%InputSlots];
    first=host_?local:remote;second=host_?remote:local;++frame_;return true;
}
void WlanSession::checksum(std::uint32_t frame,std::uint32_t value){
    own_hashes_[frame%HashSlots]={frame,value,true};
    const auto& peer=peer_hashes_[frame%HashSlots];
    if(peer.valid && peer.frame==frame && peer.value!=value)fail(WlanStatus::Desynchronized);
}
bool WlanSession::announce_checksum(){
    if(!active() || sent_!=outgoing_.size())return false;
    const auto& hash=own_hashes_[frame_%HashSlots];if(!hash.valid || hash.frame!=frame_)return false;
    queue(Message::Checkpoint);put32(outgoing_.data()+12,hash.frame);put32(outgoing_.data()+16,hash.value);seal();return true;
}
bool WlanSession::confirmed(std::uint32_t frame) const{
    const auto& own=own_hashes_[frame%HashSlots];const auto& peer=peer_hashes_[frame%HashSlots];
    return own.valid && peer.valid && own.frame==frame && peer.frame==frame && own.value==peer.value;
}
}

