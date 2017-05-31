
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
        m->pkt_len = len + sizeof(struct ether_hdr);
        m->data_len = len + sizeof(struct ether_hdr);

        eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
        rte_eth_macaddr_get(1, &eth_hdr->d_addr);
        rte_eth_macaddr_get(0, &eth_hdr->s_addr);
        eth_hdr->ether_type = htons(0x0800);
    }
}

#endif

