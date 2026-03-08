#pragma once

#ifdef _WIN32
#include <SDL3/SDL_loadso.h>
#else
#include <dlfcn.h>
#endif

#include <string>

namespace SK
{
inline void* DynLib_Open(const char* path)
{
#ifdef _WIN32
    return static_cast<void*>(SDL_LoadObject(path));
#else
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
}

inline void* DynLib_Sym(void* handle, const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

inline void DynLib_Close(void* handle)
{
#ifdef _WIN32
    SDL_UnloadObject(static_cast<SDL_SharedObject*>(handle));
#else
    dlclose(handle);
#endif
}
}
