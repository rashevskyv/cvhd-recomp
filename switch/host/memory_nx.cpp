// rex::memory for Horizon (replaces memory_posix.cpp). The guest arena is a 4.5 GB address-space
// reservation (libnx virtmem) with at most 8 code-memory blocks placed at base + guest address,
// each backed by RAM from libnx's heap. Only one R/W view of the 512 MB physical memory exists
// (the 0xA0000000 block): the 0xC0000000, 0xE0000000, raw-physical and 0x90000000 views are folded
// onto their blocks in software (xmemory.h TranslateVirtual, the codegen pch's
// REX_PHYS_HOST_OFFSET). Reasons: CLAUDE.md "Horizon memory probe on hardware".
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <switch.h>

#include <rex/memory/utils.h>

namespace rex::memory {

namespace {

struct Arena {
    uint8_t* base = nullptr;
    size_t size = 0;
    VirtmemReservation* reservation = nullptr;
};
Arena arena;

struct Block {
    uint64_t guest_start;
    size_t size;
    Handle handle;
    bool mapped;
};
// The blocks, keyed by the guest address of the view that asks for them. The sizes are the RAM
// budget (~1 GB of the 3.2 GB title takeover gives); the SDK heaps allocate bottom-up on the
// Switch, so a range stays inside its block until the game really uses more.
Block blocks[] = {
    {0x00000000, 256u << 20, INVALID_HANDLE, false},  // 4 KB virtual heap
    {0x40000000, 128u << 20, INVALID_HANDLE, false},  // 64 KB virtual heap
    {0x7F000000, 12u << 20, INVALID_HANDLE, false},   // kernel scratch
    {0x80000000, 64u << 20, INVALID_HANDLE, false},   // XEX image + kernel modules
    {0xA0000000, 512u << 20, INVALID_HANDLE, false},  // the physical arena (one view)
};

bool map_block(Block& block) {
    uint8_t* at = arena.base + block.guest_start;
    void* backing = aligned_alloc(0x1000, block.size);
    if (!backing) {
        std::printf("[nx_memory] no RAM for the 0x%llX block (%zu MB)\n", (unsigned long long)block.guest_start,
                    block.size >> 20);
        return false;
    }
    Result rc = svcCreateCodeMemory(&block.handle, backing, block.size);
    if (R_FAILED(rc)) {
        std::printf("[nx_memory] svcCreateCodeMemory(0x%llX, %zu MB) failed: 0x%X\n",
                    (unsigned long long)block.guest_start, block.size >> 20, rc);
        return false;
    }
    rc = svcControlCodeMemory(block.handle, CodeMapOperation_MapOwner, at, block.size, Perm_Rw);
    if (R_FAILED(rc)) {
        std::printf("[nx_memory] MapOwner(0x%llX -> %p, %zu MB) failed: 0x%X\n", (unsigned long long)block.guest_start,
                    at, block.size >> 20, rc);
        return false;
    }
    // The owner view does not show the source's contents (the probe): zero it here.
    std::memset(at, 0, block.size);
    block.mapped = true;
    std::printf("[nx_memory] mapped guest 0x%llX, %zu MB at %p\n", (unsigned long long)block.guest_start,
                block.size >> 20, at);
    return true;
}

}  // namespace

namespace nx {
uint8_t* arena_base() { return arena.base; }
}  // namespace nx

size_t page_size() { return 0x1000; }
size_t allocation_granularity() { return 0x1000; }

bool IsWritableExecutableMemorySupported() { return false; }

void* AllocFixed(void* base_address, size_t length, AllocationType allocation_type, PageAccess access) {
    (void)allocation_type;
    (void)access;
    if (base_address == nullptr) {
        // Host-side memory with no fixed address.
        length = (length + 0xFFF) & ~size_t(0xFFF);
        void* memory = aligned_alloc(0x1000, length);
        if (memory) {
            std::memset(memory, 0, length);
        }
        return memory;
    }
    // Inside the arena: reserved and committed up front. Outside: the caller got it from the branch
    // above.
    return base_address;
}

bool DeallocFixed(void* base_address, size_t length, DeallocationType deallocation_type) {
    (void)length;
    uint8_t* p = static_cast<uint8_t*>(base_address);
    if (arena.base && p >= arena.base && p < arena.base + arena.size) {
        return true;
    }
    if (deallocation_type == DeallocationType::kRelease) {
        free(base_address);
    }
    return true;
}

bool Protect(void* base_address, size_t length, PageAccess access, PageAccess* out_old_access) {
    (void)base_address;
    (void)length;
    (void)access;
    // Horizon refuses permission changes on these mappings (the probe): nothing is ever protected.
    if (out_old_access) {
        *out_old_access = PageAccess::kReadWrite;
    }
    return true;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
    (void)base_address;
    (void)length;
    access_out = PageAccess::kReadWrite;
    return true;
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path, size_t length, PageAccess access,
                                          bool commit) {
    (void)path;
    (void)access;
    (void)commit;
    if (arena.base) {
        return 1;
    }
    virtmemLock();
    void* at = virtmemFindAslr(length, 0x10000);
    if (at) {
        arena.reservation = virtmemAddReservation(at, length);
    }
    virtmemUnlock();
    if (!at || !arena.reservation) {
        std::printf("[nx_memory] no address space for the %zu MB arena\n", length >> 20);
        return kFileMappingHandleInvalid;
    }
    arena.base = static_cast<uint8_t*>(at);
    arena.size = length;
    std::printf("[nx_memory] reserved %zu MB at %p\n", length >> 20, at);
    return 1;
}

void CloseFileMappingHandle(FileMappingHandle handle, const std::filesystem::path& path) {
    (void)handle;
    (void)path;
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length, PageAccess access,
                  size_t file_offset) {
    (void)handle;
    (void)length;
    (void)access;
    (void)file_offset;
    uint8_t* at = static_cast<uint8_t*>(base_address);
    if (!arena.base || at < arena.base || at >= arena.base + arena.size) {
        return nullptr;
    }
    const uint64_t guest = at - arena.base;
    for (Block& block : blocks) {
        if (block.guest_start == guest) {
            if (!block.mapped && !map_block(block)) {
                return nullptr;
            }
            return at;
        }
    }
    // A view with no block of its own: an alias folded onto another block (0x90000000, 0xC0000000,
    // 0xE0000000, the raw physical view). The address is valid for arithmetic only.
    return at;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address, size_t length) {
    (void)handle;
    (void)base_address;
    (void)length;
    return true;
}

}  // namespace rex::memory
