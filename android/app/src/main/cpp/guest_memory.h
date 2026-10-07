#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <algorithm>

// A full 32-bit virtual guest address space, independent of host pointer values.
// Reserve only: committed regions are explicitly made accessible on demand.
class GuestMemory {
    static constexpr uint64_t size = uint64_t{1} << 32;
    void* base_ = MAP_FAILED;
    std::map<uint64_t,uint64_t> committed_;
public:
    GuestMemory() {
        static_assert(sizeof(void*) == 8, "GuestMemory requires a 64-bit host");
        base_ = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (base_ == MAP_FAILED) throw std::runtime_error("Guest address-space reservation failed");
    }
    GuestMemory(const GuestMemory&) = delete;
    GuestMemory& operator=(const GuestMemory&) = delete;
    ~GuestMemory() { if (base_ != MAP_FAILED) munmap(base_, size); }
    void* base() const { return base_; }
    bool readable(uint32_t va,uint64_t bytes)const{
        if(!bytes)return true;if(bytes>size-va)return false;
        auto region=committed_.upper_bound(va);if(region==committed_.begin())return false;
        --region;return uint64_t(va)+bytes<=region->second;
    }
    void* address(uint32_t va, uint64_t bytes) const {
        if (bytes > size - va) throw std::out_of_range("Guest range wraps 32-bit address space");
        return static_cast<unsigned char*>(base_) + va;
    }
    void commit(uint32_t va, uint64_t bytes) {
        if (!bytes) throw std::invalid_argument("Empty guest commit");
        address(va, bytes);
        const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
        const uint64_t start = uint64_t(va) / page * page;
        const uint64_t end = (uint64_t(va) + bytes + page - 1) / page * page;
        // Keep page zero inaccessible to diagnose guest null pointers.
        if (!start) throw std::invalid_argument("Guest null page cannot be committed");
        if (mprotect(static_cast<unsigned char*>(base_) + start, end - start, PROT_READ | PROT_WRITE))
            throw std::runtime_error("Guest commit failed");
        uint64_t mergedStart=start,mergedEnd=end;
        auto region=committed_.lower_bound(start);
        if(region!=committed_.begin()){auto previous=std::prev(region);if(previous->second>=start)region=previous;}
        while(region!=committed_.end()&&region->first<=mergedEnd){mergedStart=std::min(mergedStart,region->first);mergedEnd=std::max(mergedEnd,region->second);region=committed_.erase(region);}
        committed_.emplace(mergedStart,mergedEnd);
    }
};

inline void testGuestMemory() {
    GuestMemory mem;
    // Exercise the game image range and addresses with the high guest bit set.
    for (uint32_t va : {0x00400000u, 0x80000000u, 0xffff0000u}) {
        mem.commit(va, 4);
        const uint32_t expected = va ^ 0x12345678u;
        std::memcpy(mem.address(va, 4), &expected, 4);
        uint32_t actual = 0; std::memcpy(&actual, mem.address(va, 4), 4);
        if (actual != expected) throw std::runtime_error("Guest memory round trip failed");
    }
    bool rejected = false;
    try { mem.address(0xffffffffu, 2); } catch (const std::out_of_range&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Guest wraparound was accepted");
    if(mem.readable(0,1)||mem.readable(0x30000000,1)||!mem.readable(0x400000,4)||mem.readable(0xffffffff,2))throw std::runtime_error("Guest committed range validation failed");
}
