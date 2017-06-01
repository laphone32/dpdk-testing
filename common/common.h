
#include <stdbool.h>

#include <rte_cycles.h>
#include <rte_ethdev.h>


#ifndef __LAPHONE_COMMON_H
#define __LAPHONE_COMMON_H

static double tsc2sec(uint64_t tsc)
{
    return (double)tsc/rte_get_tsc_hz();
}

static void initTxPackets(struct rte_mbuf **txbufs, int buf_size, uint16_t len)
{
    struct ether_hdr *eth_hdr = NULL;
    int count = 0;

    for (count = 0; count < buf_size; ++ count)
    {
        struct rte_mbuf *m = txbufs[count];
        m->pkt_len = len;
        m->data_len = len;

        eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
        rte_eth_macaddr_get(1, &eth_hdr->d_addr);
        rte_eth_macaddr_get(0, &eth_hdr->s_addr);
        eth_hdr->ether_type = htons(0x0800);
    }
}

static bool checkPort(uint8_t port)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
    uint8_t count = 0;
    struct rte_eth_link link;

    printf("Checking link status...\n");
    for (count = 0; count <= MAX_CHECK_TIME; ++ count)
    {
        memset(&link, 0, sizeof(link));
        rte_eth_link_get_nowait(port, &link);
        /* print link status if flag set */
        if (link.link_status)
        {
            printf("Port %d Link Up - speed %u Mbps - %s\n", port, (unsigned)link.link_speed, (link.link_duplex == ETH_LINK_FULL_DUPLEX) ? ("full-duplex") : ("half-duplex"));
        
            return true;
        }
        else
        {
            rte_delay_ms(CHECK_INTERVAL);
            continue;
        }
    }

    printf("Port %d Link down...\n", port);
    return false;
}

#endif

