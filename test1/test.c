
/*
 * Modified from basicfwd in DPDK example
 */

#include <unistd.h>

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <arpa/inet.h>
#include "../common/common.h"


#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

#define DROP_THRESHOLD 1000000

#define SCALE 1000000

#define BURSTSIZE 16
#define PACKETLEN 20

static const struct rte_eth_conf port_conf_default = {
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int port_init(uint8_t port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = 1;
    int retval = 0;
    uint16_t q = 0;

    if (port >= rte_eth_dev_count())
    {
        return -1;
    }

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
    {
        return retval;
    }

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++)
    {
        retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (retval < 0)
        {
            return retval;
        }
    }

    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++)
    {
        retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE, rte_eth_dev_socket_id(port), NULL);
        if (retval < 0)
        {
            return retval;
        }
    }

    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
    {
        return retval;
    }

    /* Display the port MAC address. */
    struct ether_addr addr;
    rte_eth_macaddr_get(port, &addr);
    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
               " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
            (unsigned)port,
            addr.addr_bytes[0], addr.addr_bytes[1],
            addr.addr_bytes[2], addr.addr_bytes[3],
            addr.addr_bytes[4], addr.addr_bytes[5]);

    /* Enable RX in promiscuous mode for the Ethernet device. */
    rte_eth_promiscuous_enable(port);

    return 0;
}

/* For TX */
static struct rte_mbuf *txbufs[BURSTSIZE];

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static __attribute__((noreturn)) void lcore_main(void)
{
    const uint8_t nb_ports = rte_eth_dev_count();
    uint8_t port = 0;

    /*
     * Check that the port is on the same NUMA node as the polling thread
     * for best performance.
     */
    for (port = 0; port < nb_ports; port++)
    {
        if (rte_eth_dev_socket_id(port) > 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id())
        {
            printf("WARNING, port %u is on remote NUMA node to "
                    "polling thread.\n\tPerformance will "
                    "not be optimal.\n", port);
        }
    }

    printf("SLEEP 5\n");
    sleep(5);

    uint64_t start = 0;
    uint64_t stop = 0;

    struct rte_mbuf *rxbufs[BURSTSIZE];
    uint16_t nb_rx = 0;
    uint16_t nb_tx = 0;

    int count = 0;
    int drop_count = 0;

    int scale_count = 0;
    double latency = 0;

    /* Run until the application is quit or killed. */
    while (scale_count < SCALE)
    {
        printf("GO! %d\n", scale_count);
        count = 0;
        nb_rx = 0;
        /*
         * Send port 0 -> Recv port 1
        */
        start = rte_get_tsc_cycles();
        nb_tx = rte_eth_tx_burst(0, 0, txbufs, BURSTSIZE);
        while (nb_rx < BURSTSIZE)
        {
            nb_rx += rte_eth_rx_burst(1, 0, rxbufs, BURSTSIZE);

            if (++ count > DROP_THRESHOLD)
            {
                ++ drop_count;
                break;
            }
        };
        stop = rte_get_tsc_cycles();

        if (unlikely(nb_tx != nb_rx))
        {
            printf("LOSS! drop=%d\n", drop_count);
            continue;
        }
        else
        {
            latency += (tsc2sec(stop - start) * 1000000) / BURSTSIZE;
//            printf("OK! stop - start = %ld, usec=%f per=%f count=%d drop=%d\n", (stop - start), latency, latency / BURSTSIZE, count, drop_count);
        }
    
        if (++ scale_count >= SCALE)
        {
            break;
        }
    }

    printf("BURSTSIZE=%d PACKETLEN=%d latency=%f drop=%d\n", BURSTSIZE, PACKETLEN, latency / SCALE, drop_count);

    exit(0);
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool = NULL;
    unsigned nb_ports = 0;

    /* Initialize the Environment Abstraction Layer (EAL). */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    argc -= ret;
    argv += ret;

    /* Check that there is an even number of ports to send/receive on. */
    nb_ports = rte_eth_dev_count();
    if (nb_ports != 2)
    {
        rte_exit(EXIT_FAILURE, "Error: nb_ports != 2\n");
    }

    /* Creates a new mempool in memory to hold the mbufs. */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
    {
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    }

    /* Borrow one for TX */
    if (rte_pktmbuf_alloc_bulk(mbuf_pool, txbufs, BURSTSIZE) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot alloc mbuf for tx\n");
    }


    initTxPackets(txbufs, BURSTSIZE, PACKETLEN);

    /* Initialize all ports. */
    if (port_init(0, mbuf_pool) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot init port 0\n");
    }

    if (port_init(1, mbuf_pool) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot init port 1\n");
    }

    if (rte_lcore_count() > 1)
    {
        printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
    }

    /* Call lcore_main on the master core only. */
    lcore_main();

    return 0;
}

