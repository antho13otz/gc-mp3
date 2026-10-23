// GC Station sur PC : new / delete (gme, en C++) comptent dans le tas de la GameCube
#include <cstddef>
#include <new>
extern "C" void *__wrap_malloc(size_t);
extern "C" void __wrap_free(void *);
void *operator new(size_t n) { void *p = __wrap_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void *operator new[](size_t n) { void *p = __wrap_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return __wrap_malloc(n ? n : 1); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return __wrap_malloc(n ? n : 1); }
void operator delete(void *p) noexcept { __wrap_free(p); }
void operator delete[](void *p) noexcept { __wrap_free(p); }
void operator delete(void *p, size_t) noexcept { __wrap_free(p); }
void operator delete[](void *p, size_t) noexcept { __wrap_free(p); }
