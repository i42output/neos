/*
  socket_constants.hpp

  The constants of CPython 3.13's _socket module (Modules/socketmodule.c's socket_exec: their names, values and the
  conditions on which they are defined), generated from CPython 3.13: Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: the module's ADD_INT_MACRO, ADD_INT_CONST and ADD_STR_CONST are calls of the
  functions given (see runtime/python/NOTICE).
*/

#pragma once

namespace neos::python_runtime::detail::socket_constants
{
    // (aAddInt(name, long): an int constant; aAddString(name, char const*): a str; aAddUnsigned(name, unsigned long): an unsigned
    // int (Windows' SIO_*); CPython's configuration: Windows (MS_WINDOWS) and IPv6 (ENABLE_IPV6))
    template <typename AddInt, typename AddString, typename AddUnsigned>
    void define(AddInt add_int, AddString add_str, AddUnsigned add_unsigned)
    {
        (void)add_str;
        (void)add_unsigned;
#if defined(_WIN32)
#define MS_WINDOWS
#endif
#define ENABLE_IPV6
    /* Address families (we only support AF_INET and AF_UNIX) */
#ifdef AF_UNSPEC
    add_int("AF_UNSPEC", AF_UNSPEC);
#endif
    add_int("AF_INET", AF_INET);
#if defined(AF_UNIX)
    add_int("AF_UNIX", AF_UNIX);
#endif /* AF_UNIX */
#ifdef AF_AX25
    /* Amateur Radio AX.25 */
    add_int("AF_AX25", AF_AX25);
#endif
#ifdef AF_IPX
    add_int("AF_IPX", AF_IPX); /* Novell IPX */
#endif
#ifdef AF_APPLETALK
    /* Appletalk DDP */
    add_int("AF_APPLETALK", AF_APPLETALK);
#endif
#ifdef AF_NETROM
    /* Amateur radio NetROM */
    add_int("AF_NETROM", AF_NETROM);
#endif
#ifdef AF_BRIDGE
    /* Multiprotocol bridge */
    add_int("AF_BRIDGE", AF_BRIDGE);
#endif
#ifdef AF_ATMPVC
    /* ATM PVCs */
    add_int("AF_ATMPVC", AF_ATMPVC);
#endif
#ifdef AF_AAL5
    /* Reserved for Werner's ATM */
    add_int("AF_AAL5", AF_AAL5);
#endif
#ifdef HAVE_SOCKADDR_ALG
    add_int("AF_ALG", AF_ALG); /* Linux crypto */
#endif
#ifdef AF_X25
    /* Reserved for X.25 project */
    add_int("AF_X25", AF_X25);
#endif
#ifdef AF_INET6
    add_int("AF_INET6", AF_INET6); /* IP version 6 */
#endif
#ifdef AF_ROSE
    /* Amateur Radio X.25 PLP */
    add_int("AF_ROSE", AF_ROSE);
#endif
#ifdef AF_DECnet
    /* Reserved for DECnet project */
    add_int("AF_DECnet", AF_DECnet);
#endif
#ifdef AF_NETBEUI
    /* Reserved for 802.2LLC project */
    add_int("AF_NETBEUI", AF_NETBEUI);
#endif
#ifdef AF_SECURITY
    /* Security callback pseudo AF */
    add_int("AF_SECURITY", AF_SECURITY);
#endif
#ifdef AF_KEY
    /* PF_KEY key management API */
    add_int("AF_KEY", AF_KEY);
#endif
#ifdef AF_NETLINK
    /*  */
    add_int("AF_NETLINK", AF_NETLINK);
    add_int("NETLINK_ROUTE", NETLINK_ROUTE);
#ifdef NETLINK_SKIP
    add_int("NETLINK_SKIP", NETLINK_SKIP);
#endif
#ifdef NETLINK_W1
    add_int("NETLINK_W1", NETLINK_W1);
#endif
    add_int("NETLINK_USERSOCK", NETLINK_USERSOCK);
    add_int("NETLINK_FIREWALL", NETLINK_FIREWALL);
#ifdef NETLINK_TCPDIAG
    add_int("NETLINK_TCPDIAG", NETLINK_TCPDIAG);
#endif
#ifdef NETLINK_NFLOG
    add_int("NETLINK_NFLOG", NETLINK_NFLOG);
#endif
#ifdef NETLINK_XFRM
    add_int("NETLINK_XFRM", NETLINK_XFRM);
#endif
#ifdef NETLINK_ARPD
    add_int("NETLINK_ARPD", NETLINK_ARPD);
#endif
#ifdef NETLINK_ROUTE6
    add_int("NETLINK_ROUTE6", NETLINK_ROUTE6);
#endif
    add_int("NETLINK_IP6_FW", NETLINK_IP6_FW);
#ifdef NETLINK_DNRTMSG
    add_int("NETLINK_DNRTMSG", NETLINK_DNRTMSG);
#endif
#ifdef NETLINK_TAPBASE
    add_int("NETLINK_TAPBASE", NETLINK_TAPBASE);
#endif
#ifdef NETLINK_CRYPTO
    add_int("NETLINK_CRYPTO", NETLINK_CRYPTO);
#endif
#endif /* AF_NETLINK */

#ifdef AF_QIPCRTR
    /* Qualcomm IPCROUTER */
    add_int("AF_QIPCRTR", AF_QIPCRTR);
#endif

#ifdef AF_VSOCK
    add_int("AF_VSOCK", AF_VSOCK);
    add_int("SO_VM_SOCKETS_BUFFER_SIZE", 0);
    add_int("SO_VM_SOCKETS_BUFFER_MIN_SIZE", 1);
    add_int("SO_VM_SOCKETS_BUFFER_MAX_SIZE", 2);
    add_int("VMADDR_CID_ANY", 0xffffffff);
    add_int("VMADDR_PORT_ANY", 0xffffffff);
    add_int("VMADDR_CID_HOST", 2);
    add_int("VM_SOCKETS_INVALID_VERSION", 0xffffffff);
    add_int("IOCTL_VM_SOCKETS_GET_LOCAL_CID", _IO(7, 0xb9));
#endif

#ifdef AF_ROUTE
    /* Alias to emulate 4.4BSD */
    add_int("AF_ROUTE", AF_ROUTE);
#endif
#ifdef AF_LINK
    add_int("AF_LINK", AF_LINK);
#endif
#ifdef AF_ASH
    /* Ash */
    add_int("AF_ASH", AF_ASH);
#endif
#ifdef AF_ECONET
    /* Acorn Econet */
    add_int("AF_ECONET", AF_ECONET);
#endif
#ifdef AF_ATMSVC
    /* ATM SVCs */
    add_int("AF_ATMSVC", AF_ATMSVC);
#endif
#ifdef AF_SNA
    /* Linux SNA Project (nutters!) */
    add_int("AF_SNA", AF_SNA);
#endif
#ifdef AF_IRDA
    /* IRDA sockets */
    add_int("AF_IRDA", AF_IRDA);
#endif
#ifdef AF_PPPOX
    /* PPPoX sockets */
    add_int("AF_PPPOX", AF_PPPOX);
#endif
#ifdef AF_WANPIPE
    /* Wanpipe API Sockets */
    add_int("AF_WANPIPE", AF_WANPIPE);
#endif
#ifdef AF_LLC
    /* Linux LLC */
    add_int("AF_LLC", AF_LLC);
#endif
#ifdef HAVE_AF_HYPERV
    /* Hyper-V sockets */
    add_int("AF_HYPERV", AF_HYPERV);

    /* for proto */
    add_int("HV_PROTOCOL_RAW", HV_PROTOCOL_RAW);

    /* for setsockopt() */
    add_int("HVSOCKET_CONNECT_TIMEOUT", HVSOCKET_CONNECT_TIMEOUT);
    add_int("HVSOCKET_CONNECT_TIMEOUT_MAX", HVSOCKET_CONNECT_TIMEOUT_MAX);
    add_int("HVSOCKET_CONNECTED_SUSPEND", HVSOCKET_CONNECTED_SUSPEND);
    add_int("HVSOCKET_ADDRESS_FLAG_PASSTHRU", HVSOCKET_ADDRESS_FLAG_PASSTHRU);

    /* for bind() or connect() */
    add_str("HV_GUID_ZERO", "00000000-0000-0000-0000-000000000000");
    add_str("HV_GUID_WILDCARD", "00000000-0000-0000-0000-000000000000");
    add_str("HV_GUID_BROADCAST", "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF");
    add_str("HV_GUID_CHILDREN", "90DB8B89-0D35-4F79-8CE9-49EA0AC8B7CD");
    add_str("HV_GUID_LOOPBACK", "E0E16197-DD56-4A10-9195-5EE7A155A838");
    add_str("HV_GUID_PARENT", "A42E7CDA-D03F-480C-9CC2-A4DE20ABB878");
#endif /* HAVE_AF_HYPERV */

#ifdef USE_BLUETOOTH
    add_int("AF_BLUETOOTH", AF_BLUETOOTH);
#ifdef BTPROTO_L2CAP
    add_int("BTPROTO_L2CAP", BTPROTO_L2CAP);
#endif /* BTPROTO_L2CAP */
#ifdef BTPROTO_HCI
    add_int("BTPROTO_HCI", BTPROTO_HCI);
    add_int("SOL_HCI", SOL_HCI);
#if defined(HCI_FILTER)
    add_int("HCI_FILTER", HCI_FILTER);
#endif
#if defined(HCI_TIME_STAMP)
    add_int("HCI_TIME_STAMP", HCI_TIME_STAMP);
    add_int("HCI_DATA_DIR", HCI_DATA_DIR);
#endif
#endif /* BTPROTO_HCI */
#ifdef BTPROTO_RFCOMM
    add_int("BTPROTO_RFCOMM", BTPROTO_RFCOMM);
#endif /* BTPROTO_RFCOMM */
    add_str("BDADDR_ANY", "00:00:00:00:00:00");
    add_str("BDADDR_LOCAL", "00:00:00:FF:FF:FF");
#ifdef BTPROTO_SCO
    add_int("BTPROTO_SCO", BTPROTO_SCO);
#endif /* BTPROTO_SCO */
#endif /* USE_BLUETOOTH */

#ifdef AF_CAN
    /* Controller Area Network */
    add_int("AF_CAN", AF_CAN);
#endif
#ifdef PF_CAN
    /* Controller Area Network */
    add_int("PF_CAN", PF_CAN);
#endif

/* Reliable Datagram Sockets */
#ifdef AF_RDS
    add_int("AF_RDS", AF_RDS);
#endif
#ifdef PF_RDS
    add_int("PF_RDS", PF_RDS);
#endif

/* Kernel event messages */
#ifdef PF_SYSTEM
    add_int("PF_SYSTEM", PF_SYSTEM);
#endif
#ifdef AF_SYSTEM
    add_int("AF_SYSTEM", AF_SYSTEM);
#endif

/* FreeBSD divert(4) */
#ifdef PF_DIVERT
    add_int("PF_DIVERT", PF_DIVERT);
#endif
#ifdef AF_DIVERT
    add_int("AF_DIVERT", AF_DIVERT);
#endif

#ifdef AF_PACKET
    add_int("AF_PACKET", AF_PACKET);
#endif
#ifdef PF_PACKET
    add_int("PF_PACKET", PF_PACKET);
#endif
#ifdef PACKET_HOST
    add_int("PACKET_HOST", PACKET_HOST);
#endif
#ifdef PACKET_BROADCAST
    add_int("PACKET_BROADCAST", PACKET_BROADCAST);
#endif
#ifdef PACKET_MULTICAST
    add_int("PACKET_MULTICAST", PACKET_MULTICAST);
#endif
#ifdef PACKET_OTHERHOST
    add_int("PACKET_OTHERHOST", PACKET_OTHERHOST);
#endif
#ifdef PACKET_OUTGOING
    add_int("PACKET_OUTGOING", PACKET_OUTGOING);
#endif
#ifdef PACKET_LOOPBACK
    add_int("PACKET_LOOPBACK", PACKET_LOOPBACK);
#endif
#ifdef PACKET_FASTROUTE
    add_int("PACKET_FASTROUTE", PACKET_FASTROUTE);
#endif

#ifdef HAVE_LINUX_TIPC_H
    add_int("AF_TIPC", AF_TIPC);

    /* for addresses */
    add_int("TIPC_ADDR_NAMESEQ", TIPC_ADDR_NAMESEQ);
    add_int("TIPC_ADDR_NAME", TIPC_ADDR_NAME);
    add_int("TIPC_ADDR_ID", TIPC_ADDR_ID);

    add_int("TIPC_ZONE_SCOPE", TIPC_ZONE_SCOPE);
    add_int("TIPC_CLUSTER_SCOPE", TIPC_CLUSTER_SCOPE);
    add_int("TIPC_NODE_SCOPE", TIPC_NODE_SCOPE);

    /* for setsockopt() */
    add_int("SOL_TIPC", SOL_TIPC);
    add_int("TIPC_IMPORTANCE", TIPC_IMPORTANCE);
    add_int("TIPC_SRC_DROPPABLE", TIPC_SRC_DROPPABLE);
    add_int("TIPC_DEST_DROPPABLE", TIPC_DEST_DROPPABLE);
    add_int("TIPC_CONN_TIMEOUT", TIPC_CONN_TIMEOUT);

    add_int("TIPC_LOW_IMPORTANCE", TIPC_LOW_IMPORTANCE);
    add_int("TIPC_MEDIUM_IMPORTANCE", TIPC_MEDIUM_IMPORTANCE);
    add_int("TIPC_HIGH_IMPORTANCE", TIPC_HIGH_IMPORTANCE);
    add_int("TIPC_CRITICAL_IMPORTANCE", TIPC_CRITICAL_IMPORTANCE);

    /* for subscriptions */
    add_int("TIPC_SUB_PORTS", TIPC_SUB_PORTS);
    add_int("TIPC_SUB_SERVICE", TIPC_SUB_SERVICE);
#ifdef TIPC_SUB_CANCEL
    /* doesn't seem to be available everywhere */
    add_int("TIPC_SUB_CANCEL", TIPC_SUB_CANCEL);
#endif
    add_int("TIPC_WAIT_FOREVER", TIPC_WAIT_FOREVER);
    add_int("TIPC_PUBLISHED", TIPC_PUBLISHED);
    add_int("TIPC_WITHDRAWN", TIPC_WITHDRAWN);
    add_int("TIPC_SUBSCR_TIMEOUT", TIPC_SUBSCR_TIMEOUT);
    add_int("TIPC_CFG_SRV", TIPC_CFG_SRV);
    add_int("TIPC_TOP_SRV", TIPC_TOP_SRV);
#endif

#ifdef HAVE_SOCKADDR_ALG
    /* Socket options */
    add_int("ALG_SET_KEY", ALG_SET_KEY);
    add_int("ALG_SET_IV", ALG_SET_IV);
    add_int("ALG_SET_OP", ALG_SET_OP);
    add_int("ALG_SET_AEAD_ASSOCLEN", ALG_SET_AEAD_ASSOCLEN);
    add_int("ALG_SET_AEAD_AUTHSIZE", ALG_SET_AEAD_AUTHSIZE);
    add_int("ALG_SET_PUBKEY", ALG_SET_PUBKEY);

    /* Operations */
    add_int("ALG_OP_DECRYPT", ALG_OP_DECRYPT);
    add_int("ALG_OP_ENCRYPT", ALG_OP_ENCRYPT);
    add_int("ALG_OP_SIGN", ALG_OP_SIGN);
    add_int("ALG_OP_VERIFY", ALG_OP_VERIFY);
#endif

/* IEEE 802.3 protocol numbers required for a standard TCP/IP network stack */
#ifdef ETHERTYPE_ARP
    add_int("ETHERTYPE_ARP", ETHERTYPE_ARP);
#endif
#ifdef ETHERTYPE_IP
    add_int("ETHERTYPE_IP", ETHERTYPE_IP);
#endif
#ifdef ETHERTYPE_IPV6
    add_int("ETHERTYPE_IPV6", ETHERTYPE_IPV6);
#endif
#ifdef ETHERTYPE_VLAN
    add_int("ETHERTYPE_VLAN", ETHERTYPE_VLAN);
#endif

/* Linux pseudo-protocol for sniffing every packet */
#ifdef ETH_P_ALL
    add_int("ETH_P_ALL", ETH_P_ALL);
#endif

    /* Socket types */
    add_int("SOCK_STREAM", SOCK_STREAM);
    add_int("SOCK_DGRAM", SOCK_DGRAM);
/* We have incomplete socket support. */
#ifdef SOCK_RAW
    /* SOCK_RAW is marked as optional in the POSIX specification */
    add_int("SOCK_RAW", SOCK_RAW);
#endif
#ifdef SOCK_SEQPACKET
    add_int("SOCK_SEQPACKET", SOCK_SEQPACKET);
#endif
#if defined(SOCK_RDM)
    add_int("SOCK_RDM", SOCK_RDM);
#endif
#ifdef SOCK_CLOEXEC
    add_int("SOCK_CLOEXEC", SOCK_CLOEXEC);
#endif
#ifdef SOCK_NONBLOCK
    add_int("SOCK_NONBLOCK", SOCK_NONBLOCK);
#endif

#ifdef  SO_DEBUG
    add_int("SO_DEBUG", SO_DEBUG);
#endif
#ifdef  SO_ACCEPTCONN
    add_int("SO_ACCEPTCONN", SO_ACCEPTCONN);
#endif
#ifdef  SO_REUSEADDR
    add_int("SO_REUSEADDR", SO_REUSEADDR);
#endif
#ifdef SO_EXCLUSIVEADDRUSE
    add_int("SO_EXCLUSIVEADDRUSE", SO_EXCLUSIVEADDRUSE);
#endif
#ifdef SO_INCOMING_CPU
    add_int("SO_INCOMING_CPU", SO_INCOMING_CPU);
#endif

#ifdef  SO_KEEPALIVE
    add_int("SO_KEEPALIVE", SO_KEEPALIVE);
#endif
#ifdef  SO_DONTROUTE
    add_int("SO_DONTROUTE", SO_DONTROUTE);
#endif
#ifdef  SO_BROADCAST
    add_int("SO_BROADCAST", SO_BROADCAST);
#endif
#ifdef  SO_USELOOPBACK
    add_int("SO_USELOOPBACK", SO_USELOOPBACK);
#endif
#ifdef  SO_LINGER
    add_int("SO_LINGER", SO_LINGER);
#endif
#ifdef  SO_OOBINLINE
    add_int("SO_OOBINLINE", SO_OOBINLINE);
#endif
#ifndef __GNU__
#ifdef  SO_REUSEPORT
    add_int("SO_REUSEPORT", SO_REUSEPORT);
#endif
#endif
#ifdef  SO_SNDBUF
    add_int("SO_SNDBUF", SO_SNDBUF);
#endif
#ifdef  SO_RCVBUF
    add_int("SO_RCVBUF", SO_RCVBUF);
#endif
#ifdef  SO_SNDLOWAT
    add_int("SO_SNDLOWAT", SO_SNDLOWAT);
#endif
#ifdef  SO_RCVLOWAT
    add_int("SO_RCVLOWAT", SO_RCVLOWAT);
#endif
#ifdef  SO_SNDTIMEO
    add_int("SO_SNDTIMEO", SO_SNDTIMEO);
#endif
#ifdef  SO_RCVTIMEO
    add_int("SO_RCVTIMEO", SO_RCVTIMEO);
#endif
#ifdef  SO_ERROR
    add_int("SO_ERROR", SO_ERROR);
#endif
#ifdef  SO_TYPE
    add_int("SO_TYPE", SO_TYPE);
#endif
#ifdef  SO_SETFIB
    add_int("SO_SETFIB", SO_SETFIB);
#endif
#ifdef  SO_PASSCRED
    add_int("SO_PASSCRED", SO_PASSCRED);
#endif
#ifdef  SO_PEERCRED
    add_int("SO_PEERCRED", SO_PEERCRED);
#endif
#ifdef  LOCAL_PEERCRED
    add_int("LOCAL_PEERCRED", LOCAL_PEERCRED);
#endif
#ifdef  SO_PASSSEC
    add_int("SO_PASSSEC", SO_PASSSEC);
#endif
#ifdef  SO_PEERSEC
    add_int("SO_PEERSEC", SO_PEERSEC);
#endif
#ifdef  SO_BINDTODEVICE
    add_int("SO_BINDTODEVICE", SO_BINDTODEVICE);
#endif
#ifdef  SO_BINDTOIFINDEX
    add_int("SO_BINDTOIFINDEX", SO_BINDTOIFINDEX);
#endif
#ifdef  SO_PRIORITY
    add_int("SO_PRIORITY", SO_PRIORITY);
#endif
#ifdef  SO_MARK
    add_int("SO_MARK", SO_MARK);
#endif
#ifdef  SO_USER_COOKIE
    add_int("SO_USER_COOKIE", SO_USER_COOKIE);
#endif
#ifdef  SO_RTABLE
    add_int("SO_RTABLE", SO_RTABLE);
#endif
#ifdef SO_DOMAIN
    add_int("SO_DOMAIN", SO_DOMAIN);
#endif
#ifdef SO_PROTOCOL
    add_int("SO_PROTOCOL", SO_PROTOCOL);
#endif
#ifdef LOCAL_CREDS
    add_int("LOCAL_CREDS", LOCAL_CREDS);
#endif
#ifdef LOCAL_CREDS_PERSISTENT
    add_int("LOCAL_CREDS_PERSISTENT", LOCAL_CREDS_PERSISTENT);
#endif

    /* Maximum number of connections for "listen" */
#ifdef  SOMAXCONN
    add_int("SOMAXCONN", SOMAXCONN);
#else
    add_int("SOMAXCONN", 5); /* Common value */
#endif

    /* Ancillary message types */
#ifdef  SCM_RIGHTS
    add_int("SCM_RIGHTS", SCM_RIGHTS);
#endif
#ifdef  SCM_CREDENTIALS
    add_int("SCM_CREDENTIALS", SCM_CREDENTIALS);
#endif
#ifdef  SCM_CREDS
    add_int("SCM_CREDS", SCM_CREDS);
#endif
#ifdef  SCM_CREDS2
    add_int("SCM_CREDS2", SCM_CREDS2);
#endif

    /* Flags for send, recv */
#ifdef  MSG_OOB
    add_int("MSG_OOB", MSG_OOB);
#endif
#ifdef  MSG_PEEK
    add_int("MSG_PEEK", MSG_PEEK);
#endif
#ifdef  MSG_DONTROUTE
    add_int("MSG_DONTROUTE", MSG_DONTROUTE);
#endif
#ifdef  MSG_DONTWAIT
    add_int("MSG_DONTWAIT", MSG_DONTWAIT);
#endif
#ifdef  MSG_EOR
    add_int("MSG_EOR", MSG_EOR);
#endif
#ifdef  MSG_TRUNC
    // workaround for https://github.com/WebAssembly/wasi-libc/issues/305
    #if defined(__wasi__) && !defined(__WASI_RIFLAGS_RECV_DATA_TRUNCATED)
    #  define __WASI_RIFLAGS_RECV_DATA_TRUNCATED 2
    #endif
    add_int("MSG_TRUNC", MSG_TRUNC);
#endif
#ifdef  MSG_CTRUNC
    add_int("MSG_CTRUNC", MSG_CTRUNC);
#endif
#ifdef  MSG_WAITALL
    add_int("MSG_WAITALL", MSG_WAITALL);
#endif
#ifdef  MSG_BTAG
    add_int("MSG_BTAG", MSG_BTAG);
#endif
#ifdef  MSG_ETAG
    add_int("MSG_ETAG", MSG_ETAG);
#endif
#ifdef  MSG_NOSIGNAL
    add_int("MSG_NOSIGNAL", MSG_NOSIGNAL);
#endif
#ifdef  MSG_NOTIFICATION
    add_int("MSG_NOTIFICATION", MSG_NOTIFICATION);
#endif
#ifdef  MSG_CMSG_CLOEXEC
    add_int("MSG_CMSG_CLOEXEC", MSG_CMSG_CLOEXEC);
#endif
#ifdef  MSG_ERRQUEUE
    add_int("MSG_ERRQUEUE", MSG_ERRQUEUE);
#endif
#ifdef  MSG_CONFIRM
    add_int("MSG_CONFIRM", MSG_CONFIRM);
#endif
#ifdef  MSG_MORE
    add_int("MSG_MORE", MSG_MORE);
#endif
#ifdef  MSG_EOF
    add_int("MSG_EOF", MSG_EOF);
#endif
#ifdef  MSG_BCAST
    add_int("MSG_BCAST", MSG_BCAST);
#endif
#ifdef  MSG_MCAST
    add_int("MSG_MCAST", MSG_MCAST);
#endif
#ifdef MSG_FASTOPEN
    add_int("MSG_FASTOPEN", MSG_FASTOPEN);
#endif

    /* Protocol level and numbers, usable for [gs]etsockopt */
#ifdef  SOL_SOCKET
    add_int("SOL_SOCKET", SOL_SOCKET);
#endif
#ifdef  SOL_IP
    add_int("SOL_IP", SOL_IP);
#else
    add_int("SOL_IP", 0);
#endif
#ifdef  SOL_IPX
    add_int("SOL_IPX", SOL_IPX);
#endif
#ifdef  SOL_AX25
    add_int("SOL_AX25", SOL_AX25);
#endif
#ifdef  SOL_ATALK
    add_int("SOL_ATALK", SOL_ATALK);
#endif
#ifdef  SOL_NETROM
    add_int("SOL_NETROM", SOL_NETROM);
#endif
#ifdef  SOL_ROSE
    add_int("SOL_ROSE", SOL_ROSE);
#endif
#ifdef  SOL_TCP
    add_int("SOL_TCP", SOL_TCP);
#else
    add_int("SOL_TCP", 6);
#endif
#ifdef  SOL_UDP
    add_int("SOL_UDP", SOL_UDP);
#else
    add_int("SOL_UDP", 17);
#endif
#ifdef SOL_CAN_BASE
    add_int("SOL_CAN_BASE", SOL_CAN_BASE);
#endif
#ifdef SOL_CAN_RAW
    add_int("SOL_CAN_RAW", SOL_CAN_RAW);
    add_int("CAN_RAW", CAN_RAW);
#endif
#if defined(HAVE_LINUX_CAN_H) || defined(HAVE_NETCAN_CAN_H)
    add_int("CAN_EFF_FLAG", CAN_EFF_FLAG);
    add_int("CAN_RTR_FLAG", CAN_RTR_FLAG);
    add_int("CAN_ERR_FLAG", CAN_ERR_FLAG);

    add_int("CAN_SFF_MASK", CAN_SFF_MASK);
    add_int("CAN_EFF_MASK", CAN_EFF_MASK);
    add_int("CAN_ERR_MASK", CAN_ERR_MASK);
#ifdef CAN_ISOTP
    add_int("CAN_ISOTP", CAN_ISOTP);
#endif
#ifdef CAN_J1939
    add_int("CAN_J1939", CAN_J1939);
#endif
#endif
#if defined(HAVE_LINUX_CAN_RAW_H) || defined(HAVE_NETCAN_CAN_H)
    add_int("CAN_RAW_FILTER", CAN_RAW_FILTER);
#ifdef HAVE_LINUX_CAN_RAW_H
    add_int("CAN_RAW_ERR_FILTER", CAN_RAW_ERR_FILTER);
#endif
    add_int("CAN_RAW_LOOPBACK", CAN_RAW_LOOPBACK);
    add_int("CAN_RAW_RECV_OWN_MSGS", CAN_RAW_RECV_OWN_MSGS);
#endif
#ifdef HAVE_LINUX_CAN_RAW_FD_FRAMES
    add_int("CAN_RAW_FD_FRAMES", CAN_RAW_FD_FRAMES);
#endif
#ifdef HAVE_LINUX_CAN_RAW_JOIN_FILTERS
    add_int("CAN_RAW_JOIN_FILTERS", CAN_RAW_JOIN_FILTERS);
#endif
#ifdef HAVE_LINUX_CAN_BCM_H
    add_int("CAN_BCM", CAN_BCM);

    /* BCM opcodes */
    add_int("CAN_BCM_TX_SETUP", TX_SETUP);
    add_int("CAN_BCM_TX_DELETE", TX_DELETE);
    add_int("CAN_BCM_TX_READ", TX_READ);
    add_int("CAN_BCM_TX_SEND", TX_SEND);
    add_int("CAN_BCM_RX_SETUP", RX_SETUP);
    add_int("CAN_BCM_RX_DELETE", RX_DELETE);
    add_int("CAN_BCM_RX_READ", RX_READ);
    add_int("CAN_BCM_TX_STATUS", TX_STATUS);
    add_int("CAN_BCM_TX_EXPIRED", TX_EXPIRED);
    add_int("CAN_BCM_RX_STATUS", RX_STATUS);
    add_int("CAN_BCM_RX_TIMEOUT", RX_TIMEOUT);
    add_int("CAN_BCM_RX_CHANGED", RX_CHANGED);

    /* BCM flags */
    add_int("CAN_BCM_SETTIMER", SETTIMER);
    add_int("CAN_BCM_STARTTIMER", STARTTIMER);
    add_int("CAN_BCM_TX_COUNTEVT", TX_COUNTEVT);
    add_int("CAN_BCM_TX_ANNOUNCE", TX_ANNOUNCE);
    add_int("CAN_BCM_TX_CP_CAN_ID", TX_CP_CAN_ID);
    add_int("CAN_BCM_RX_FILTER_ID", RX_FILTER_ID);
    add_int("CAN_BCM_RX_CHECK_DLC", RX_CHECK_DLC);
    add_int("CAN_BCM_RX_NO_AUTOTIMER", RX_NO_AUTOTIMER);
    add_int("CAN_BCM_RX_ANNOUNCE_RESUME", RX_ANNOUNCE_RESUME);
    add_int("CAN_BCM_TX_RESET_MULTI_IDX", TX_RESET_MULTI_IDX);
    add_int("CAN_BCM_RX_RTR_FRAME", RX_RTR_FRAME);
#ifdef CAN_FD_FRAME
    /* CAN_FD_FRAME was only introduced in the 4.8.x kernel series */
    add_int("CAN_BCM_CAN_FD_FRAME", CAN_FD_FRAME);
#endif
#endif
#ifdef HAVE_LINUX_CAN_J1939_H
    add_int("J1939_MAX_UNICAST_ADDR", J1939_MAX_UNICAST_ADDR);
    add_int("J1939_IDLE_ADDR", J1939_IDLE_ADDR);
    add_int("J1939_NO_ADDR", J1939_NO_ADDR);
    add_int("J1939_NO_NAME", J1939_NO_NAME);
    add_int("J1939_PGN_REQUEST", J1939_PGN_REQUEST);
    add_int("J1939_PGN_ADDRESS_CLAIMED", J1939_PGN_ADDRESS_CLAIMED);
    add_int("J1939_PGN_ADDRESS_COMMANDED", J1939_PGN_ADDRESS_COMMANDED);
    add_int("J1939_PGN_PDU1_MAX", J1939_PGN_PDU1_MAX);
    add_int("J1939_PGN_MAX", J1939_PGN_MAX);
    add_int("J1939_NO_PGN", J1939_NO_PGN);

    /* J1939 socket options */
    add_int("SO_J1939_FILTER", SO_J1939_FILTER);
    add_int("SO_J1939_PROMISC", SO_J1939_PROMISC);
    add_int("SO_J1939_SEND_PRIO", SO_J1939_SEND_PRIO);
    add_int("SO_J1939_ERRQUEUE", SO_J1939_ERRQUEUE);

    add_int("SCM_J1939_DEST_ADDR", SCM_J1939_DEST_ADDR);
    add_int("SCM_J1939_DEST_NAME", SCM_J1939_DEST_NAME);
    add_int("SCM_J1939_PRIO", SCM_J1939_PRIO);
    add_int("SCM_J1939_ERRQUEUE", SCM_J1939_ERRQUEUE);

    add_int("J1939_NLA_PAD", J1939_NLA_PAD);
    add_int("J1939_NLA_BYTES_ACKED", J1939_NLA_BYTES_ACKED);

    add_int("J1939_EE_INFO_NONE", J1939_EE_INFO_NONE);
    add_int("J1939_EE_INFO_TX_ABORT", J1939_EE_INFO_TX_ABORT);

    add_int("J1939_FILTER_MAX", J1939_FILTER_MAX);
#endif
#ifdef SOL_RDS
    add_int("SOL_RDS", SOL_RDS);
#endif
#ifdef HAVE_SOCKADDR_ALG
    add_int("SOL_ALG", SOL_ALG);
#endif
#ifdef RDS_CANCEL_SENT_TO
    add_int("RDS_CANCEL_SENT_TO", RDS_CANCEL_SENT_TO);
#endif
#ifdef RDS_GET_MR
    add_int("RDS_GET_MR", RDS_GET_MR);
#endif
#ifdef RDS_FREE_MR
    add_int("RDS_FREE_MR", RDS_FREE_MR);
#endif
#ifdef RDS_RECVERR
    add_int("RDS_RECVERR", RDS_RECVERR);
#endif
#ifdef RDS_CONG_MONITOR
    add_int("RDS_CONG_MONITOR", RDS_CONG_MONITOR);
#endif
#ifdef RDS_GET_MR_FOR_DEST
    add_int("RDS_GET_MR_FOR_DEST", RDS_GET_MR_FOR_DEST);
#endif
#ifdef  IPPROTO_IP
    add_int("IPPROTO_IP", IPPROTO_IP);
#else
    add_int("IPPROTO_IP", 0);
#endif
#ifdef  IPPROTO_HOPOPTS
    add_int("IPPROTO_HOPOPTS", IPPROTO_HOPOPTS);
#endif
#ifdef  IPPROTO_ICMP
    add_int("IPPROTO_ICMP", IPPROTO_ICMP);
#else
    add_int("IPPROTO_ICMP", 1);
#endif
#ifdef  IPPROTO_IGMP
    add_int("IPPROTO_IGMP", IPPROTO_IGMP);
#endif
#ifdef  IPPROTO_GGP
    add_int("IPPROTO_GGP", IPPROTO_GGP);
#endif
#ifdef  IPPROTO_IPV4
    add_int("IPPROTO_IPV4", IPPROTO_IPV4);
#endif
#ifdef  IPPROTO_IPV6
    add_int("IPPROTO_IPV6", IPPROTO_IPV6);
#endif
#ifdef  IPPROTO_IPIP
    add_int("IPPROTO_IPIP", IPPROTO_IPIP);
#endif
#ifdef  IPPROTO_TCP
    add_int("IPPROTO_TCP", IPPROTO_TCP);
#else
    add_int("IPPROTO_TCP", 6);
#endif
#ifdef  IPPROTO_EGP
    add_int("IPPROTO_EGP", IPPROTO_EGP);
#endif
#ifdef  IPPROTO_PUP
    add_int("IPPROTO_PUP", IPPROTO_PUP);
#endif
#ifdef  IPPROTO_UDP
    add_int("IPPROTO_UDP", IPPROTO_UDP);
#else
    add_int("IPPROTO_UDP", 17);
#endif
#ifdef  IPPROTO_UDPLITE
    add_int("IPPROTO_UDPLITE", IPPROTO_UDPLITE);
    #ifndef UDPLITE_SEND_CSCOV
        #define UDPLITE_SEND_CSCOV 10
    #endif
    add_int("UDPLITE_SEND_CSCOV", UDPLITE_SEND_CSCOV);
    #ifndef UDPLITE_RECV_CSCOV
        #define UDPLITE_RECV_CSCOV 11
    #endif
    add_int("UDPLITE_RECV_CSCOV", UDPLITE_RECV_CSCOV);
#endif
#ifdef  IPPROTO_IDP
    add_int("IPPROTO_IDP", IPPROTO_IDP);
#endif
#ifdef  IPPROTO_HELLO
    add_int("IPPROTO_HELLO", IPPROTO_HELLO);
#endif
#ifdef  IPPROTO_ND
    add_int("IPPROTO_ND", IPPROTO_ND);
#endif
#ifdef  IPPROTO_TP
    add_int("IPPROTO_TP", IPPROTO_TP);
#endif
#ifdef  IPPROTO_ROUTING
    add_int("IPPROTO_ROUTING", IPPROTO_ROUTING);
#endif
#ifdef  IPPROTO_FRAGMENT
    add_int("IPPROTO_FRAGMENT", IPPROTO_FRAGMENT);
#endif
#ifdef  IPPROTO_RSVP
    add_int("IPPROTO_RSVP", IPPROTO_RSVP);
#endif
#ifdef  IPPROTO_GRE
    add_int("IPPROTO_GRE", IPPROTO_GRE);
#endif
#ifdef  IPPROTO_ESP
    add_int("IPPROTO_ESP", IPPROTO_ESP);
#endif
#ifdef  IPPROTO_AH
    add_int("IPPROTO_AH", IPPROTO_AH);
#endif
#ifdef  IPPROTO_MOBILE
    add_int("IPPROTO_MOBILE", IPPROTO_MOBILE);
#endif
#ifdef  IPPROTO_ICMPV6
    add_int("IPPROTO_ICMPV6", IPPROTO_ICMPV6);
#endif
#ifdef  IPPROTO_NONE
    add_int("IPPROTO_NONE", IPPROTO_NONE);
#endif
#ifdef  IPPROTO_DSTOPTS
    add_int("IPPROTO_DSTOPTS", IPPROTO_DSTOPTS);
#endif
#ifdef  IPPROTO_XTP
    add_int("IPPROTO_XTP", IPPROTO_XTP);
#endif
#ifdef  IPPROTO_EON
    add_int("IPPROTO_EON", IPPROTO_EON);
#endif
#ifdef  IPPROTO_PIM
    add_int("IPPROTO_PIM", IPPROTO_PIM);
#endif
#ifdef  IPPROTO_IPCOMP
    add_int("IPPROTO_IPCOMP", IPPROTO_IPCOMP);
#endif
#ifdef  IPPROTO_VRRP
    add_int("IPPROTO_VRRP", IPPROTO_VRRP);
#endif
#ifdef  IPPROTO_SCTP
    add_int("IPPROTO_SCTP", IPPROTO_SCTP);
#endif
#ifdef  IPPROTO_BIP
    add_int("IPPROTO_BIP", IPPROTO_BIP);
#endif
#ifdef  IPPROTO_MPTCP
    add_int("IPPROTO_MPTCP", IPPROTO_MPTCP);
#endif
/**/
#ifdef  IPPROTO_RAW
    add_int("IPPROTO_RAW", IPPROTO_RAW);
#else
    add_int("IPPROTO_RAW", 255);
#endif
#ifdef  IPPROTO_MAX
    add_int("IPPROTO_MAX", IPPROTO_MAX);
#endif

#ifdef  MS_WINDOWS
    add_int("IPPROTO_ICLFXBM", IPPROTO_ICLFXBM);
    add_int("IPPROTO_ST", IPPROTO_ST);
    add_int("IPPROTO_CBT", IPPROTO_CBT);
    add_int("IPPROTO_IGP", IPPROTO_IGP);
    add_int("IPPROTO_RDP", IPPROTO_RDP);
    add_int("IPPROTO_PGM", IPPROTO_PGM);
    add_int("IPPROTO_L2TP", IPPROTO_L2TP);
    add_int("IPPROTO_SCTP", IPPROTO_SCTP);
#endif

#ifdef  SYSPROTO_CONTROL
    add_int("SYSPROTO_CONTROL", SYSPROTO_CONTROL);
#endif

    /* Some port configuration */
#ifdef  IPPORT_RESERVED
    add_int("IPPORT_RESERVED", IPPORT_RESERVED);
#else
    add_int("IPPORT_RESERVED", 1024);
#endif
#ifdef  IPPORT_USERRESERVED
    add_int("IPPORT_USERRESERVED", IPPORT_USERRESERVED);
#else
    add_int("IPPORT_USERRESERVED", 5000);
#endif

    /* Some reserved IP v.4 addresses */
#ifdef  INADDR_ANY
    add_int("INADDR_ANY", INADDR_ANY);
#else
    add_int("INADDR_ANY", 0x00000000);
#endif
#ifdef  INADDR_BROADCAST
    add_int("INADDR_BROADCAST", INADDR_BROADCAST);
#else
    add_int("INADDR_BROADCAST", 0xffffffff);
#endif
#ifdef  INADDR_LOOPBACK
    add_int("INADDR_LOOPBACK", INADDR_LOOPBACK);
#else
    add_int("INADDR_LOOPBACK", 0x7F000001);
#endif
#ifdef  INADDR_UNSPEC_GROUP
    add_int("INADDR_UNSPEC_GROUP", INADDR_UNSPEC_GROUP);
#else
    add_int("INADDR_UNSPEC_GROUP", 0xe0000000);
#endif
#ifdef  INADDR_ALLHOSTS_GROUP
    add_int("INADDR_ALLHOSTS_GROUP", INADDR_ALLHOSTS_GROUP);
#else
    add_int("INADDR_ALLHOSTS_GROUP", 0xe0000001);
#endif
#ifdef  INADDR_MAX_LOCAL_GROUP
    add_int("INADDR_MAX_LOCAL_GROUP", INADDR_MAX_LOCAL_GROUP);
#else
    add_int("INADDR_MAX_LOCAL_GROUP", 0xe00000ff);
#endif
#ifdef  INADDR_NONE
    add_int("INADDR_NONE", INADDR_NONE);
#else
    add_int("INADDR_NONE", 0xffffffff);
#endif

    /* IPv4 [gs]etsockopt options */
#ifdef  IP_OPTIONS
    add_int("IP_OPTIONS", IP_OPTIONS);
#endif
#ifdef  IP_HDRINCL
    add_int("IP_HDRINCL", IP_HDRINCL);
#endif
#ifdef  IP_TOS
    add_int("IP_TOS", IP_TOS);
#endif
#ifdef  IP_TTL
    add_int("IP_TTL", IP_TTL);
#endif
#ifdef  IP_RECVOPTS
    add_int("IP_RECVOPTS", IP_RECVOPTS);
#endif
#ifdef  IP_RECVRETOPTS
    add_int("IP_RECVRETOPTS", IP_RECVRETOPTS);
#endif
#ifdef  IP_RECVTOS
    add_int("IP_RECVTOS", IP_RECVTOS);
#endif
#ifdef  IP_RECVDSTADDR
    add_int("IP_RECVDSTADDR", IP_RECVDSTADDR);
#endif
#ifdef  IP_RETOPTS
    add_int("IP_RETOPTS", IP_RETOPTS);
#endif
#ifdef  IP_MULTICAST_IF
    add_int("IP_MULTICAST_IF", IP_MULTICAST_IF);
#endif
#ifdef  IP_MULTICAST_TTL
    add_int("IP_MULTICAST_TTL", IP_MULTICAST_TTL);
#endif
#ifdef  IP_MULTICAST_LOOP
    add_int("IP_MULTICAST_LOOP", IP_MULTICAST_LOOP);
#endif
#ifdef  IP_ADD_MEMBERSHIP
    add_int("IP_ADD_MEMBERSHIP", IP_ADD_MEMBERSHIP);
#endif
#ifdef  IP_DROP_MEMBERSHIP
    add_int("IP_DROP_MEMBERSHIP", IP_DROP_MEMBERSHIP);
#endif
#ifdef  IP_DEFAULT_MULTICAST_TTL
    add_int("IP_DEFAULT_MULTICAST_TTL", IP_DEFAULT_MULTICAST_TTL);
#endif
#ifdef  IP_DEFAULT_MULTICAST_LOOP
    add_int("IP_DEFAULT_MULTICAST_LOOP", IP_DEFAULT_MULTICAST_LOOP);
#endif
#ifdef  IP_MAX_MEMBERSHIPS
    add_int("IP_MAX_MEMBERSHIPS", IP_MAX_MEMBERSHIPS);
#endif
#ifdef  IP_TRANSPARENT
    add_int("IP_TRANSPARENT", IP_TRANSPARENT);
#endif
#ifdef  IP_PKTINFO
    add_int("IP_PKTINFO", IP_PKTINFO);
#endif
#ifdef IP_BIND_ADDRESS_NO_PORT
    add_int("IP_BIND_ADDRESS_NO_PORT", IP_BIND_ADDRESS_NO_PORT);
#endif
#ifdef IP_UNBLOCK_SOURCE
    add_int("IP_UNBLOCK_SOURCE", IP_UNBLOCK_SOURCE);
#endif
#ifdef IP_BLOCK_SOURCE
    add_int("IP_BLOCK_SOURCE", IP_BLOCK_SOURCE);
#endif
#ifdef IP_ADD_SOURCE_MEMBERSHIP
    add_int("IP_ADD_SOURCE_MEMBERSHIP", IP_ADD_SOURCE_MEMBERSHIP);
#endif
#ifdef IP_DROP_SOURCE_MEMBERSHIP
    add_int("IP_DROP_SOURCE_MEMBERSHIP", IP_DROP_SOURCE_MEMBERSHIP);
#endif

    /* IPv6 [gs]etsockopt options, defined in RFC2553 */
#ifdef  IPV6_JOIN_GROUP
    add_int("IPV6_JOIN_GROUP", IPV6_JOIN_GROUP);
#endif
#ifdef  IPV6_LEAVE_GROUP
    add_int("IPV6_LEAVE_GROUP", IPV6_LEAVE_GROUP);
#endif
#ifdef  IPV6_MULTICAST_HOPS
    add_int("IPV6_MULTICAST_HOPS", IPV6_MULTICAST_HOPS);
#endif
#ifdef  IPV6_MULTICAST_IF
    add_int("IPV6_MULTICAST_IF", IPV6_MULTICAST_IF);
#endif
#ifdef  IPV6_MULTICAST_LOOP
    add_int("IPV6_MULTICAST_LOOP", IPV6_MULTICAST_LOOP);
#endif
#ifdef  IPV6_UNICAST_HOPS
    add_int("IPV6_UNICAST_HOPS", IPV6_UNICAST_HOPS);
#endif
    /* Additional IPV6 socket options, defined in RFC 3493 */
#ifdef IPV6_V6ONLY
    add_int("IPV6_V6ONLY", IPV6_V6ONLY);
#endif
    /* Advanced IPV6 socket options, from RFC 3542 */
#ifdef IPV6_CHECKSUM
    add_int("IPV6_CHECKSUM", IPV6_CHECKSUM);
#endif
#ifdef IPV6_DONTFRAG
    add_int("IPV6_DONTFRAG", IPV6_DONTFRAG);
#endif
#ifdef IPV6_DSTOPTS
    add_int("IPV6_DSTOPTS", IPV6_DSTOPTS);
#endif
#ifdef IPV6_HOPLIMIT
    add_int("IPV6_HOPLIMIT", IPV6_HOPLIMIT);
#endif
#ifdef IPV6_HOPOPTS
    add_int("IPV6_HOPOPTS", IPV6_HOPOPTS);
#endif
#ifdef IPV6_NEXTHOP
    add_int("IPV6_NEXTHOP", IPV6_NEXTHOP);
#endif
#ifdef IPV6_PATHMTU
    add_int("IPV6_PATHMTU", IPV6_PATHMTU);
#endif
#ifdef IPV6_PKTINFO
    add_int("IPV6_PKTINFO", IPV6_PKTINFO);
#endif
#ifdef IPV6_RECVDSTOPTS
    add_int("IPV6_RECVDSTOPTS", IPV6_RECVDSTOPTS);
#endif
#ifdef IPV6_RECVHOPLIMIT
    add_int("IPV6_RECVHOPLIMIT", IPV6_RECVHOPLIMIT);
#endif
#ifdef IPV6_RECVHOPOPTS
    add_int("IPV6_RECVHOPOPTS", IPV6_RECVHOPOPTS);
#endif
#ifdef IPV6_RECVPKTINFO
    add_int("IPV6_RECVPKTINFO", IPV6_RECVPKTINFO);
#endif
#ifdef IPV6_RECVRTHDR
    add_int("IPV6_RECVRTHDR", IPV6_RECVRTHDR);
#endif
#ifdef IPV6_RECVTCLASS
    add_int("IPV6_RECVTCLASS", IPV6_RECVTCLASS);
#endif
#ifdef IPV6_RTHDR
    add_int("IPV6_RTHDR", IPV6_RTHDR);
#endif
#ifdef IPV6_RTHDRDSTOPTS
    add_int("IPV6_RTHDRDSTOPTS", IPV6_RTHDRDSTOPTS);
#endif
#ifdef IPV6_RTHDR_TYPE_0
    add_int("IPV6_RTHDR_TYPE_0", IPV6_RTHDR_TYPE_0);
#endif
#ifdef IPV6_RECVPATHMTU
    add_int("IPV6_RECVPATHMTU", IPV6_RECVPATHMTU);
#endif
#ifdef IPV6_TCLASS
    add_int("IPV6_TCLASS", IPV6_TCLASS);
#endif
#ifdef IPV6_USE_MIN_MTU
    add_int("IPV6_USE_MIN_MTU", IPV6_USE_MIN_MTU);
#endif

    /* TCP options */
#ifdef  TCP_NODELAY
    add_int("TCP_NODELAY", TCP_NODELAY);
#endif
#ifdef  TCP_MAXSEG
    add_int("TCP_MAXSEG", TCP_MAXSEG);
#endif
#ifdef  TCP_CORK
    add_int("TCP_CORK", TCP_CORK);
#endif
#ifdef  TCP_KEEPIDLE
    add_int("TCP_KEEPIDLE", TCP_KEEPIDLE);
#endif
    /* TCP_KEEPALIVE is OSX's TCP_KEEPIDLE equivalent */
#if defined(__APPLE__) && defined(TCP_KEEPALIVE)
    add_int("TCP_KEEPALIVE", TCP_KEEPALIVE);
#endif
#ifdef  TCP_KEEPINTVL
    add_int("TCP_KEEPINTVL", TCP_KEEPINTVL);
#endif
#ifdef  TCP_KEEPCNT
    add_int("TCP_KEEPCNT", TCP_KEEPCNT);
#endif
#ifdef  TCP_SYNCNT
    add_int("TCP_SYNCNT", TCP_SYNCNT);
#endif
#ifdef  TCP_LINGER2
    add_int("TCP_LINGER2", TCP_LINGER2);
#endif
#ifdef  TCP_DEFER_ACCEPT
    add_int("TCP_DEFER_ACCEPT", TCP_DEFER_ACCEPT);
#endif
#ifdef  TCP_WINDOW_CLAMP
    add_int("TCP_WINDOW_CLAMP", TCP_WINDOW_CLAMP);
#endif
#ifdef  TCP_INFO
    add_int("TCP_INFO", TCP_INFO);
#endif
#ifdef  TCP_CONNECTION_INFO
    add_int("TCP_CONNECTION_INFO", TCP_CONNECTION_INFO);
#endif
#ifdef  TCP_QUICKACK
    add_int("TCP_QUICKACK", TCP_QUICKACK);
#endif
#ifdef  TCP_CONGESTION
    add_int("TCP_CONGESTION", TCP_CONGESTION);
#endif
#ifdef  TCP_MD5SIG
    add_int("TCP_MD5SIG", TCP_MD5SIG);
#endif
#ifdef  TCP_THIN_LINEAR_TIMEOUTS
    add_int("TCP_THIN_LINEAR_TIMEOUTS", TCP_THIN_LINEAR_TIMEOUTS);
#endif
#ifdef  TCP_THIN_DUPACK
    add_int("TCP_THIN_DUPACK", TCP_THIN_DUPACK);
#endif
#ifdef  TCP_USER_TIMEOUT
    add_int("TCP_USER_TIMEOUT", TCP_USER_TIMEOUT);
#endif
#ifdef  TCP_REPAIR
    add_int("TCP_REPAIR", TCP_REPAIR);
#endif
#ifdef  TCP_REPAIR_QUEUE
    add_int("TCP_REPAIR_QUEUE", TCP_REPAIR_QUEUE);
#endif
#ifdef  TCP_QUEUE_SEQ
    add_int("TCP_QUEUE_SEQ", TCP_QUEUE_SEQ);
#endif
#ifdef  TCP_REPAIR_OPTIONS
    add_int("TCP_REPAIR_OPTIONS", TCP_REPAIR_OPTIONS);
#endif
#ifdef  TCP_FASTOPEN
    add_int("TCP_FASTOPEN", TCP_FASTOPEN);
#endif
#ifdef  TCP_TIMESTAMP
    add_int("TCP_TIMESTAMP", TCP_TIMESTAMP);
#endif
#ifdef  TCP_NOTSENT_LOWAT
    add_int("TCP_NOTSENT_LOWAT", TCP_NOTSENT_LOWAT);
#endif
#ifdef  TCP_CC_INFO
    add_int("TCP_CC_INFO", TCP_CC_INFO);
#endif
#ifdef  TCP_SAVE_SYN
    add_int("TCP_SAVE_SYN", TCP_SAVE_SYN);
#endif
#ifdef  TCP_SAVED_SYN
    add_int("TCP_SAVED_SYN", TCP_SAVED_SYN);
#endif
#ifdef  TCP_REPAIR_WINDOW
    add_int("TCP_REPAIR_WINDOW", TCP_REPAIR_WINDOW);
#endif
#ifdef  TCP_FASTOPEN_CONNECT
    add_int("TCP_FASTOPEN_CONNECT", TCP_FASTOPEN_CONNECT);
#endif
#ifdef  TCP_ULP
    add_int("TCP_ULP", TCP_ULP);
#endif
#ifdef  TCP_MD5SIG_EXT
    add_int("TCP_MD5SIG_EXT", TCP_MD5SIG_EXT);
#endif
#ifdef  TCP_FASTOPEN_KEY
    add_int("TCP_FASTOPEN_KEY", TCP_FASTOPEN_KEY);
#endif
#ifdef  TCP_FASTOPEN_NO_COOKIE
    add_int("TCP_FASTOPEN_NO_COOKIE", TCP_FASTOPEN_NO_COOKIE);
#endif
#ifdef  TCP_ZEROCOPY_RECEIVE
    add_int("TCP_ZEROCOPY_RECEIVE", TCP_ZEROCOPY_RECEIVE);
#endif
#ifdef  TCP_INQ
    add_int("TCP_INQ", TCP_INQ);
#endif
#ifdef  TCP_TX_DELAY
    add_int("TCP_TX_DELAY", TCP_TX_DELAY);
#endif

    /* IPX options */
#ifdef  IPX_TYPE
    add_int("IPX_TYPE", IPX_TYPE);
#endif

/* Reliable Datagram Sockets */
#ifdef RDS_CMSG_RDMA_ARGS
    add_int("RDS_CMSG_RDMA_ARGS", RDS_CMSG_RDMA_ARGS);
#endif
#ifdef RDS_CMSG_RDMA_DEST
    add_int("RDS_CMSG_RDMA_DEST", RDS_CMSG_RDMA_DEST);
#endif
#ifdef RDS_CMSG_RDMA_MAP
    add_int("RDS_CMSG_RDMA_MAP", RDS_CMSG_RDMA_MAP);
#endif
#ifdef RDS_CMSG_RDMA_STATUS
    add_int("RDS_CMSG_RDMA_STATUS", RDS_CMSG_RDMA_STATUS);
#endif
#ifdef RDS_CMSG_RDMA_UPDATE
    add_int("RDS_CMSG_RDMA_UPDATE", RDS_CMSG_RDMA_UPDATE);
#endif
#ifdef RDS_RDMA_READWRITE
    add_int("RDS_RDMA_READWRITE", RDS_RDMA_READWRITE);
#endif
#ifdef RDS_RDMA_FENCE
    add_int("RDS_RDMA_FENCE", RDS_RDMA_FENCE);
#endif
#ifdef RDS_RDMA_INVALIDATE
    add_int("RDS_RDMA_INVALIDATE", RDS_RDMA_INVALIDATE);
#endif
#ifdef RDS_RDMA_USE_ONCE
    add_int("RDS_RDMA_USE_ONCE", RDS_RDMA_USE_ONCE);
#endif
#ifdef RDS_RDMA_DONTWAIT
    add_int("RDS_RDMA_DONTWAIT", RDS_RDMA_DONTWAIT);
#endif
#ifdef RDS_RDMA_NOTIFY_ME
    add_int("RDS_RDMA_NOTIFY_ME", RDS_RDMA_NOTIFY_ME);
#endif
#ifdef RDS_RDMA_SILENT
    add_int("RDS_RDMA_SILENT", RDS_RDMA_SILENT);
#endif

    /* get{addr,name}info parameters */
#ifdef EAI_ADDRFAMILY
    add_int("EAI_ADDRFAMILY", EAI_ADDRFAMILY);
#endif
#ifdef EAI_AGAIN
    add_int("EAI_AGAIN", EAI_AGAIN);
#endif
#ifdef EAI_BADFLAGS
    add_int("EAI_BADFLAGS", EAI_BADFLAGS);
#endif
#ifdef EAI_FAIL
    add_int("EAI_FAIL", EAI_FAIL);
#endif
#ifdef EAI_FAMILY
    add_int("EAI_FAMILY", EAI_FAMILY);
#endif
#ifdef EAI_MEMORY
    add_int("EAI_MEMORY", EAI_MEMORY);
#endif
#ifdef EAI_NODATA
    add_int("EAI_NODATA", EAI_NODATA);
#endif
#ifdef EAI_NONAME
    add_int("EAI_NONAME", EAI_NONAME);
#endif
#ifdef EAI_OVERFLOW
    add_int("EAI_OVERFLOW", EAI_OVERFLOW);
#endif
#ifdef EAI_SERVICE
    add_int("EAI_SERVICE", EAI_SERVICE);
#endif
#ifdef EAI_SOCKTYPE
    add_int("EAI_SOCKTYPE", EAI_SOCKTYPE);
#endif
#ifdef EAI_SYSTEM
    add_int("EAI_SYSTEM", EAI_SYSTEM);
#endif
#ifdef EAI_BADHINTS
    add_int("EAI_BADHINTS", EAI_BADHINTS);
#endif
#ifdef EAI_PROTOCOL
    add_int("EAI_PROTOCOL", EAI_PROTOCOL);
#endif
#ifdef EAI_MAX
    add_int("EAI_MAX", EAI_MAX);
#endif
#ifdef AI_PASSIVE
    add_int("AI_PASSIVE", AI_PASSIVE);
#endif
#ifdef AI_CANONNAME
    add_int("AI_CANONNAME", AI_CANONNAME);
#endif
#ifdef AI_NUMERICHOST
    add_int("AI_NUMERICHOST", AI_NUMERICHOST);
#endif
#ifdef AI_NUMERICSERV
    add_int("AI_NUMERICSERV", AI_NUMERICSERV);
#endif
#ifdef AI_MASK
    add_int("AI_MASK", AI_MASK);
#endif
#ifdef AI_ALL
    add_int("AI_ALL", AI_ALL);
#endif
#ifdef AI_V4MAPPED_CFG
    add_int("AI_V4MAPPED_CFG", AI_V4MAPPED_CFG);
#endif
#ifdef AI_ADDRCONFIG
    add_int("AI_ADDRCONFIG", AI_ADDRCONFIG);
#endif
#ifdef AI_V4MAPPED
    add_int("AI_V4MAPPED", AI_V4MAPPED);
#endif
#ifdef AI_DEFAULT
    add_int("AI_DEFAULT", AI_DEFAULT);
#endif
#ifdef NI_MAXHOST
    add_int("NI_MAXHOST", NI_MAXHOST);
#endif
#ifdef NI_MAXSERV
    add_int("NI_MAXSERV", NI_MAXSERV);
#endif
#ifdef NI_NOFQDN
    add_int("NI_NOFQDN", NI_NOFQDN);
#endif
#ifdef NI_NUMERICHOST
    add_int("NI_NUMERICHOST", NI_NUMERICHOST);
#endif
#ifdef NI_NAMEREQD
    add_int("NI_NAMEREQD", NI_NAMEREQD);
#endif
#ifdef NI_NUMERICSERV
    add_int("NI_NUMERICSERV", NI_NUMERICSERV);
#endif
#ifdef NI_DGRAM
    add_int("NI_DGRAM", NI_DGRAM);
#endif
#ifdef NI_IDN
    add_int("NI_IDN", NI_IDN);
#endif

    /* shutdown() parameters */
#ifdef SHUT_RD
    add_int("SHUT_RD", SHUT_RD);
#elif defined(SD_RECEIVE)
    add_int("SHUT_RD", SD_RECEIVE);
#else
    add_int("SHUT_RD", 0);
#endif
#ifdef SHUT_WR
    add_int("SHUT_WR", SHUT_WR);
#elif defined(SD_SEND)
    add_int("SHUT_WR", SD_SEND);
#else
    add_int("SHUT_WR", 1);
#endif
#ifdef SHUT_RDWR
    add_int("SHUT_RDWR", SHUT_RDWR);
#elif defined(SD_BOTH)
    add_int("SHUT_RDWR", SD_BOTH);
#else
    add_int("SHUT_RDWR", 2);
#endif

#ifdef SIO_RCVALL
    {
        DWORD codes[] = {SIO_RCVALL, SIO_KEEPALIVE_VALS,
#if defined(SIO_LOOPBACK_FAST_PATH)
            SIO_LOOPBACK_FAST_PATH
#endif
        };
        const char *names[] = {"SIO_RCVALL", "SIO_KEEPALIVE_VALS",
#if defined(SIO_LOOPBACK_FAST_PATH)
            "SIO_LOOPBACK_FAST_PATH"
#endif
        };
        int i;
        for (i = 0; i < static_cast<int>(sizeof(codes) / sizeof(codes[0])); ++i) {
            add_unsigned(names[i], codes[i]);
        }
    }
    add_int("RCVALL_OFF", RCVALL_OFF);
    add_int("RCVALL_ON", RCVALL_ON);
    add_int("RCVALL_SOCKETLEVELONLY", RCVALL_SOCKETLEVELONLY);
#ifdef RCVALL_IPLEVEL
    add_int("RCVALL_IPLEVEL", RCVALL_IPLEVEL);
#endif
#ifdef RCVALL_MAX
    add_int("RCVALL_MAX", RCVALL_MAX);
#endif
#endif /* _MSTCPIP_ */

    /* Initialize gethostbyname lock */

#undef ENABLE_IPV6
#if defined(_WIN32)
#undef MS_WINDOWS
#endif
    }
}
