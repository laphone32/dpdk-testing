
/*
 * Modified from basicfwd in DPDK example
 */

#include <unistd.h>

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <arpa/inet.h>
#include <sys/time.h>

#define RX_RING_SIZE 128
#define TX_RING_SIZE 512

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

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
static struct rte_mbuf *txbufs[1];

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

	uint64_t start = 0;
	uint64_t stop = 0;
	
	struct rte_mbuf *rxbufs[100];
	uint16_t nb_rx = 0;
	uint16_t nb_tx = 0;

      //  struct timeval begin;
      //  struct timeval end;

//	gettimeofday(&begin, NULL);
//
	double unit = (double)rte_get_tsc_hz();
	start = rte_get_tsc_cycles();
	sleep(1);
	stop = rte_get_tsc_cycles();
//	gettimeofday(&end, NULL);

printf("TEST! stop - start = %ld unit= %f sec=%f\n", (stop - start) , unit, (stop - start)/unit);

	int count = 0;

	/* Run until the application is quit or killed. */
	for (;;)
	{
printf("GO!\n");
		count = 0;
		/*
		 * Send port 0 -> Recv port 1
		*/
//		start = rte_get_tsc_cycles();
//		gettimeofday(&begin, NULL);
		nb_tx = rte_eth_tx_burst(0, 0, txbufs, 1);
		start = rte_get_tsc_cycles();
		stop = rte_get_tsc_cycles();


//		start = rte_get_tsc_cycles();
		while ((nb_rx = rte_eth_rx_burst(1, 0, rxbufs, 64)) == 0)
		{
			++ count;
		};
//		stop = rte_get_tsc_cycles();
//		gettimeofday(&end, NULL);


		if (unlikely(nb_tx != nb_rx))
		{
			printf("tx(%d) != rx(%d)\n", nb_tx, nb_rx);
		}

printf("OK! stop - start = %ld, usec=%f count=%d\n", (stop - start), ((double)(stop - start) / unit) * 1000000, count);
//printf("OK! stop - start = %ld\n", (end.tv_sec - begin.tv_sec) * 1000000 + (end.tv_usec - begin.tv_usec));
		usleep(10000);
	}
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
	if (rte_pktmbuf_alloc_bulk(mbuf_pool, txbufs, 1) != 0)
	{
		rte_exit(EXIT_FAILURE, "Cannot alloc mbuf for tx\n");
	}


//	char msg[10] = "012345678";
	struct ether_hdr *eth_hdr = NULL;
	struct rte_mbuf *m = txbufs[0];
	m->pkt_len = 10 + sizeof(struct ether_hdr);
	m->data_len = 10 + sizeof(struct ether_hdr);

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
	rte_eth_macaddr_get(1, &eth_hdr->d_addr);
	rte_eth_macaddr_get(0, &eth_hdr->s_addr);
	eth_hdr->ether_type = htons(0x0800);
//	char* data = rte_pktmbuf_append(m, 10);
//        if (data != NULL)
//            rte_memcpy(data, msg, 10);


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

