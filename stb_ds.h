/* modified stb_ds.h - v0.67 - public domain data structures - Sean Barrett 2019

   This was a single-header-file library that provides easy-to-use
   dynamic arrays and hash tables for C (also works in C++).

   For a gentle introduction:
      http://nothings.org/stb_ds

   To use this library, do this in *one* C or C++ file:
      #define STB_DS_IMPLEMENTATION
      #include "stb_ds.h"
*/

#pragma once

#include <stddef.h>
#include <string.h>


#if defined(STBDS_REALLOC) && !defined(STBDS_FREE) || !defined(STBDS_REALLOC) && defined(STBDS_FREE)
#error "You must define both STBDS_REALLOC and STBDS_FREE, or neither."
#endif
#if !defined(STBDS_REALLOC) && !defined(STBDS_FREE)
#include <stdlib.h>
#define STBDS_REALLOC(c,p,s) realloc(p,s)
#define STBDS_FREE(c,p)      free(p)
#endif

#ifdef _MSC_VER
#define STBDS_NOTUSED(v)  (void)(v)
#else
#define STBDS_NOTUSED(v)  (void)sizeof(v)
#endif


#if defined(__GNUC__) || defined(__clang__)
#define STBDS_HAS_TYPEOF
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define STBDS_HAS_LITERAL_ARRAY
#endif

// this macro takes the address of the argument, but on gcc/clang can accept rvalues
#if defined(STBDS_HAS_LITERAL_ARRAY) && defined(STBDS_HAS_TYPEOF)
  #if __clang__
  #define STBDS_ADDRESSOF(typevar, value)     ((__typeof__(typevar)[1]){value}) // literal array decays to pointer to value
  #else
  #define STBDS_ADDRESSOF(typevar, value)     ((typeof(typevar)[1]){value}) // literal array decays to pointer to value
  #endif
#else
#define STBDS_ADDRESSOF(typevar, value)     &(value)
#endif

#define STBDS_OFFSETOF(var,field)           ((char *) &(var)->field - (char *) (var))

#define stbds_header(t)  ((stbds_array_header *) (t) - 1)
#define stbds_temp(t)    stbds_header(t)->temp
#define stbds_temp_key(t) (*(char **) stbds_header(t)->hash_table)

#define stbds_arr(T) T*
#define stbds_arrsetcap(a,n)   (stbds_arrgrow(a,0,n))
#define stbds_arrsetlen(a,n)   ((stbds_arrcap(a) < (size_t) (n) ? stbds_arrsetcap((a),(size_t)(n)),0 : 0), (a) ? stbds_header(a)->length = (size_t) (n) : 0)
#define stbds_arrcap(a)        ((a) ? stbds_header(a)->capacity : 0)
#define stbds_arrlen(a)        ((a) ? (ptrdiff_t) stbds_header(a)->length : 0)
#define stbds_arrlenu(a)       ((a) ?             stbds_header(a)->length : 0)
#define stbds_arrput(a,v)      (stbds_arrmaybegrow(a,1), (a)[stbds_header(a)->length++] = (v))
#define stbds_arrpush          stbds_arrput  // synonym
#define stbds_arrpop(a)        (stbds_header(a)->length--, (a)[stbds_header(a)->length])
#define stbds_arraddn(a,n)     ((void)(stbds_arraddnindex(a, n)))    // deprecated, use one of the following instead:
#define stbds_arraddnptr(a,n)  (stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), &(a)[stbds_header(a)->length-(n)]) : (a))
#define stbds_arraddnindex(a,n)(stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), stbds_header(a)->length-(n)) : stbds_arrlen(a))
#define stbds_arraddnoff       stbds_arraddnindex
#define stbds_arrlast(a)       ((a)[stbds_header(a)->length-1])
#define stbds_arrfree(a)       ((void) ((a) ? STBDS_FREE(NULL,stbds_header(a)) : (void)0), (a)=NULL)
#define stbds_arrdel(a,i)      stbds_arrdeln(a,i,1)
#define stbds_arrdeln(a,i,n)   (memmove(&(a)[i], &(a)[(i)+(n)], sizeof *(a) * (stbds_header(a)->length-(n)-(i))), stbds_header(a)->length -= (n))
#define stbds_arrdelswap(a,i)  ((a)[i] = stbds_arrlast(a), stbds_header(a)->length -= 1)
#define stbds_arrinsn(a,i,n)   (stbds_arraddn((a),(n)), memmove(&(a)[(i)+(n)], &(a)[i], sizeof *(a) * (stbds_header(a)->length-(n)-(i))))
#define stbds_arrins(a,i,v)    (stbds_arrinsn((a),(i),1), (a)[i]=(v))

#define stbds_arrmaybegrow(a,n)  ((!(a) || stbds_header(a)->length + (n) > stbds_header(a)->capacity) \
                                  ? (stbds_arrgrow(a,n,0),0) : 0)

#define stbds_arrgrow(a,b,c)   ((a) = stbds_arrgrowf_wrapper((a), sizeof *(a), (b), (c)))


#define stbds_hm(K, V) struct { K key; V value; }*
#define stbds_hs(K) struct { K key; }*
#define stbds_sh(V) stbds_hm(const char*, V)

#define stbds_hmput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, 0),   \
     (t)[stbds_temp((t)-1)].key = (k),    \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_hmputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), &(s).key, sizeof (s).key, STBDS_HM_BINARY), \
     (t)[stbds_temp((t)-1)] = (s))

#define stbds_hmgeti(t,k) \
    ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_HM_BINARY), \
      stbds_temp((t)-1))

#define stbds_hmgeti_ts(t,k,temp) \
    ((t) = stbds_hmget_key_ts_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, &(temp), STBDS_HM_BINARY), \
      (temp))

#define stbds_hmgetp(t, k) \
    ((void) stbds_hmgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_hmgetp_ts(t, k, temp) \
    ((void) stbds_hmgeti_ts(t,k,temp), &(t)[temp])

#define stbds_hmdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_BINARY)),(t)?stbds_temp((t)-1):0)

#define stbds_hmdefault(t, v) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1].value = (v))

#define stbds_hmdefaults(t, s) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1] = (s))

#define stbds_hmfree(p)        \
    ((void) ((p) != NULL ? stbds_hmfree_func((p)-1,sizeof*(p)),0 : 0),(p)=NULL)

#define stbds_hmgets(t, k)    (*stbds_hmgetp(t,k))
#define stbds_hmget(t, k)     (stbds_hmgetp(t,k)->value)
#define stbds_hmget_ts(t, k, temp)  (stbds_hmgetp_ts(t,k,temp)->value)
#define stbds_hmlen(t)        ((t) ? (ptrdiff_t) stbds_header((t)-1)->length-1 : 0)
#define stbds_hmlenu(t)       ((t) ?             stbds_header((t)-1)->length-1 : 0)
#define stbds_hmgetp_null(t,k)  (stbds_hmgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])

#define stbds_shput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_shputi(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v), stbds_temp((t)-1))

#define stbds_shputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (s).key, sizeof (s).key, STBDS_HM_STRING), \
     (t)[stbds_temp((t)-1)] = (s), \
     (t)[stbds_temp((t)-1)].key = stbds_temp_key((t)-1)) // above line overwrites whole structure, so must rewrite key here if it was allocated internally

#define stbds_pshput(t, p) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (p)->key, sizeof (p)->key, STBDS_HM_PTR_TO_STRING), \
     (t)[stbds_temp((t)-1)] = (p))

#define stbds_shgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING), \
      stbds_temp((t)-1))

#define stbds_pshgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_HM_PTR_TO_STRING), \
      stbds_temp((t)-1))

#define stbds_shgetp(t, k) \
    ((void) stbds_shgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_pshget(t, k) \
    ((void) stbds_pshgeti(t,k), (t)[stbds_temp((t)-1)])

#define stbds_shdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_STRING)),(t)?stbds_temp((t)-1):0)
#define stbds_pshdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_OFFSETOF(*(t),key), STBDS_HM_PTR_TO_STRING)),(t)?stbds_temp((t)-1):0)

#define stbds_sh_new_arena(t)  \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_ARENA))
#define stbds_sh_new_strdup(t) \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_STRDUP))

#define stbds_shdefault(t, v)  stbds_hmdefault(t,v)
#define stbds_shdefaults(t, s) stbds_hmdefaults(t,s)

#define stbds_shfree       stbds_hmfree
#define stbds_shlenu       stbds_hmlenu

#define stbds_shgets(t, k) (*stbds_shgetp(t,k))
#define stbds_shget(t, k)  (stbds_shgetp(t,k)->value)
#define stbds_shgetp_null(t,k)  (stbds_shgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])
#define stbds_shlen        stbds_hmlen

typedef struct
{
  size_t      length;
  size_t      capacity;
  void      * hash_table;
  ptrdiff_t   temp;
} stbds_array_header;

typedef struct stbds_string_block
{
  struct stbds_string_block *next;
  char storage[8];
} stbds_string_block;

typedef struct stbds_string_arena
{
  stbds_string_block *storage;
  size_t remaining;
  unsigned char block;
  unsigned char mode;  // this isn't used by the string arena itself
} stbds_string_arena;

#define STBDS_HM_BINARY         0
#define STBDS_HM_STRING         1

enum
{
   STBDS_SH_NONE,
   STBDS_SH_DEFAULT,
   STBDS_SH_STRDUP,
   STBDS_SH_ARENA
};

#define stbds_arrgrowf_wrapper            stbds_arrgrowf
#define stbds_hmget_key_wrapper           stbds_hmget_key
#define stbds_hmget_key_ts_wrapper        stbds_hmget_key_ts
#define stbds_hmput_default_wrapper       stbds_hmput_default
#define stbds_hmput_key_wrapper           stbds_hmput_key
#define stbds_hmdel_key_wrapper           stbds_hmdel_key
#define stbds_shmode_func_wrapper(t,e,m)  stbds_shmode_func(e,m)

char *stbds_stralloc(stbds_string_arena *a, char *str);
void stbds_strreset(stbds_string_arena *a);