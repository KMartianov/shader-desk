// Src/tracy-memory.cpp
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <new>
#include <cstdlib>

// Standard allocations
void* operator new(std::size_t count) {
    auto ptr = std::malloc(count);
    if (!ptr) throw std::bad_alloc{};
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

// Arrays
void* operator new[](std::size_t count) {
    auto ptr = std::malloc(count);
    if (!ptr) throw std::bad_alloc{};
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

// C++14/C++17 Sized deallocation (Optimized deletion)
void operator delete(void* ptr, std::size_t) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}

// C++17 Aligned allocations 
// Very important for OpenGL/GLM vectors!
void* operator new(std::size_t count, std::align_val_t al) {
    void* ptr = std::aligned_alloc(static_cast<std::size_t>(al), count);
    if (!ptr) throw std::bad_alloc{};
    TracyAlloc(ptr, count);
    return ptr;
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    TracyFree(ptr);
    std::free(ptr);
}
#endif // TRACY_ENABLE