/*
 * Expat's configuration for the neos Python runtime plugin (its module pyexpat), as CPython 3.13's Modules/expat/expat_config.h
 * (this file is not part of the expat distribution): its byte order the compiler's, not pyconfig.h's.
 */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H 1

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BYTEORDER 4321
#else
#define BYTEORDER 1234
#endif

#define HAVE_MEMMOVE 1

#define XML_NS 1
#define XML_DTD 1
#define XML_GE 1
#define XML_CONTEXT_BYTES 1024

// (pyexpat calls XML_SetHashSalt with a salt of its own (std::random_device's): expat's entropy sources are not needed)
#define XML_POOR_ENTROPY 1
#undef HAVE_ARC4RANDOM
#undef HAVE_ARC4RANDOM_BUF
#undef HAVE_GETENTROPY
#undef HAVE_GETRANDOM
#undef HAVE_SYSCALL_GETRANDOM

#endif /* EXPAT_CONFIG_H */
