#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(KIPSMA_FP_STATIC)
        #define KIPSMA_FP_API
    #elif defined(KIPSMA_FP_BUILDING_DLL)
        #define KIPSMA_FP_API __declspec(dllexport)
    #else
        #define KIPSMA_FP_API __declspec(dllimport)
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define KIPSMA_FP_API __attribute__((visibility("default")))
    #else
        #define KIPSMA_FP_API
    #endif
#endif

