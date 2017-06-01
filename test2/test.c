
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

#define DROP_THRESHOLD 100000000

#define SCALE 1000000

#define BURSTSIZE 16
#define PACKETLEN 300

static const struct rte_eth_conf port_conf_default = {
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};

enum ports
{
    tx_rx_port = 0,
    rx_tx_port = 1,
    port_number
};


static struct rte_mempool * _mpool[port_number];

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int port_init(uint8_t port)
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
        retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE, rte_eth_dev_socket_id(port), NULL, _mpool[port]);
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

    if (!checkPort(port))
    {
        printf("Port %d link status not ready...\n", port);
        return -1;
    }

    return 0;
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static void lcore_tx_rx_main(void)
{
    const uint8_t nb_ports = rte_eth_dev_count();
    uint8_t port = 0;
    struct rte_mbuf *txbufs[BURSTSIZE];

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

    /* Borrow one for TX */
    if (rte_pktmbuf_alloc_bulk(_mpool[tx_rx_port], txbufs, BURSTSIZE) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot alloc mbuf for tx\n");
    }

    initTxPackets(txbufs, BURSTSIZE, PACKETLEN);

    uint64_t start = 0;
    uint64_t stop = 0;

    struct rte_mbuf *rxbufs[BURSTSIZE];
    uint16_t nb_rx = 0;
    uint16_t nb_tx = 0;

    int count = 0;
    int drop_count = 0;

    int scale_count = 0;
    double latency = 0;

    printf("Go for test!!! scale=%d\n", SCALE);
    /* Run until the application is quit or killed. */
    while (scale_count < SCALE)
    {
        count = 0;
        nb_rx = 0;
        /*
         * Send & Recv port 0
        */
        start = rte_get_tsc_cycles();
        nb_tx = rte_eth_tx_burst(tx_rx_port, 0, txbufs, BURSTSIZE);
        while (nb_rx < BURSTSIZE)
        {
            nb_rx += rte_eth_rx_burst(tx_rx_port, 0, rxbufs, BURSTSIZE);

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
        }
    
        if (++ scale_count >= SCALE)
        {
            break;
        }
    }

    printf("BURSTSIZE=%d PACKETLEN=%d latency=%f drop=%d\n", BURSTSIZE, PACKETLEN, latency / SCALE, drop_count);

    exit(0);
}

static int lcore_rx_tx_main(void *nonused)
{
    (void) nonused;

    struct rte_mbuf *rxbufs[BURSTSIZE];
    uint16_t nb_rx = 0;
    uint16_t tx = 0;
    uint16_t tx_ret = 0;

    while (true)
    {
        nb_rx = 0;
        tx = 0;
        tx_ret = 0;
        /*
         * Recv & Send port 1
        */
        nb_rx = rte_eth_rx_burst(rx_tx_port, 0, rxbufs, BURSTSIZE);

        while (nb_rx)
        {
            tx_ret = rte_eth_tx_burst(rx_tx_port, 0, rxbufs + tx, nb_rx);
            nb_rx -= tx_ret;
            tx += tx_ret;
        }

    }

    exit(0);
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[])
{
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

    int count = 0;
    for (count = 0; count < port_number; ++ count)
    {
        /* Creates a new mempool in memory to hold the mbufs. */
        char poolName[128] = "\0";
        sprintf(poolName, "MBUF_POOL_%d", count);

        _mpool[count] = rte_pktmbuf_pool_create(poolName, NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

        if (_mpool[count] == NULL)
        {
            printf("Cannot create mbuf pool_%d\n", count);
            return -1;
        }
    }

    /* Initialize all ports. */
    if (port_init(tx_rx_port) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot init port %d\n", tx_rx_port);
    }

    if (port_init(rx_tx_port) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot init port %d\n", rx_tx_port);
    }

    if (rte_lcore_count() > 2)
    {
        printf("\nWARNING: Too many lcores enabled. Only 2 used.\n");
        exit(1);
    }

    /* Fire remote core... */
    if (rte_eal_mp_remote_launch(&lcore_rx_tx_main, NULL, SKIP_MASTER) != 0)
    {
        rte_exit(EXIT_FAILURE, "Fire remote slave core failed...\n");
    }

    /* Call lcore_main on the master core only. */
    lcore_tx_rx_main();

    return 0;
}

