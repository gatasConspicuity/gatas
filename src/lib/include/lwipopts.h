#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

#define LWIP_SOCKET                 0

// Use the SDK's actual CYW43 async-context ownership check: its FreeRTOS
// integration overrides the generic lwIP core mutex implementation.
#if !defined(NDEBUG) && !defined(UNIT_TESTING)
#ifdef __cplusplus
extern "C" {
#endif
void gatas_lwip_mark_tcpip_thread(void);
void gatas_lwip_assert_core_locked(void);
#ifdef __cplusplus
}
#endif
#define LWIP_MARK_TCPIP_THREAD() gatas_lwip_mark_tcpip_thread()
#define LWIP_ASSERT_CORE_LOCKED() gatas_lwip_assert_core_locked()
#endif

#define MEM_ALIGNMENT               4
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_ARP_QUEUE          10

// This is calculated see opt.h
// #define PBUF_RAM_BUFSIZE           LWIP_MEM_ALIGN_SIZE(TCP_MSS+40+PBUF_LINK_ENCAPSULATION_HLEN+PBUF_LINK_HLEN)
#if defined(PICO_RP2350)
#define MEMP_NUM_UDP_PCB            16
#define MEMP_NUM_TCP_PCB            12
// PBUF_POOL_SIZE Increased from 24 15 Oct 2026
#define PBUF_POOL_SIZE              24
#else
#define MEMP_NUM_UDP_PCB            12
#define MEMP_NUM_TCP_PCB            8
// PBUF_POOL_SIZE Increased from 16 15 Oct 2026
#define PBUF_POOL_SIZE              20
#endif

// Keep MEMP_MEM_MALLOC low , since LWiP should use the POOL only
#define MEM_SIZE                    512
#define MEMP_MEM_MALLOC             0
#define MEM_LIBC_MALLOC             0
#define MEM_USE_POOLS               1
#define MEMP_USE_CUSTOM_POOLS       1
#define MEM_USE_POOLS_TRY_BIGGER_POOL  0
// CHanging to 1460 take an additional 14784 bytes
#define TCP_MSS                     536
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_CHECKSUM_ON_COPY       1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0

#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#if !defined(NDEBUG) && GATAS_LWIP_DEBUG
#define MEM_STATS                   1
#define SYS_STATS                   1
#define MEMP_STATS                  1
#define LINK_STATS                  1
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#define MEMP_SANITY_CHECK           1
#define MEMP_OVERFLOW_CHECK         1
#endif

#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define MEMP_DEBUG                  LWIP_DBG_OFF
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
