#ifndef CKLOS_NETWORK_H
#define CKLOS_NETWORK_H

#include "types.h"

enum {
    TCP_EVENT_CONNECTED = 1,
    TCP_EVENT_DATA = 2,
    TCP_EVENT_DISCONNECTED = 3
};

typedef void (*tcp_event_handler)(uint8_t event, const uint8_t *data, uint32_t length);

void network_init(void);
void network_poll(void);
bool network_available(void);
bool network_arp_resolve(uint32_t address, uint8_t *mac, uint32_t timeout_ms);
bool network_ping(uint32_t address, uint32_t timeout_ms);
uint32_t network_ip_address(void);
uint32_t network_gateway_address(void);
uint32_t network_packets_received(void);
uint32_t network_packets_sent(void);
uint32_t network_tx_errors(void);
uint32_t network_last_tx_status(void);
uint16_t network_io_port(void);
uint16_t network_rx_cursor(void);
uint16_t network_interrupt_status(void);
uint8_t network_control_register(void);
uint32_t network_receive_config(void);
uint8_t network_clock_state(void);
const uint8_t *network_mac_address(void);
bool network_tcp_listen(uint16_t port, tcp_event_handler handler);
bool network_tcp_write(const uint8_t *data, uint32_t length);
void network_tcp_close(void);
bool network_tcp_established(void);
uint16_t network_tcp_local_port(void);
uint16_t network_tcp_remote_port(void);
uint32_t network_tcp_data_segments(void);
uint32_t network_tcp_data_bytes(void);
uint32_t network_tcp_accepted_segments(void);
uint32_t network_tcp_last_sequence(void);
uint32_t network_tcp_expected_sequence(void);
uint32_t network_tcp_write_attempts(void);
uint32_t network_tcp_write_failures(void);
uint32_t network_tcp_last_write_state(void);
uint32_t network_tcp_segment_failure(void);
uint32_t network_ipv4_send_failure(void);

#endif
