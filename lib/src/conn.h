// Copyright (c) 2016-2017, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <netinet/in.h>
#include <stdint.h>

#include <ev.h>
#include <picotls.h>
#include <warpcore/warpcore.h>

#include "diet.h"
#include "quic.h"

// Embryonic and established (actually, non-embryonic) QUIC connections.
extern splay_head(ipnp_splay, q_conn) conns_by_ipnp;
extern splay_head(cid_splay, q_conn) conns_by_cid;


/// A QUIC connection.
struct q_conn {
    splay_entry(q_conn) node_ipnp;
    splay_entry(q_conn) node_cid;
    sl_entry(q_conn) next;

    uint64_t id; ///< Connection ID

    uint32_t vers;         ///< QUIC version in use for this connection.
    uint32_t vers_initial; ///< QUIC version first negotiated.
    uint32_t next_sid;     ///< Next stream ID to use on q_rsv_stream().
    uint8_t flags;         ///< Connection flags.
    uint8_t state;         ///< State of the connection.

    // LD state
    uint8_t handshake_cnt;
    uint8_t tlp_cnt;
    uint8_t rto_cnt;
    uint8_t _unused2;
    uint8_t _unused[6];
    ev_timer ld_alarm; ///< Loss detection alarm.
    ev_timer idle_alarm;

    struct sockaddr_in peer; ///< Address of our peer.
    char * peer_name;

    splay_head(stream, q_stream) streams;
    struct diet closed_streams;
    struct w_sock * sock; ///< File descriptor (socket) for the connection.
    ev_io rx_w;           ///< RX watcher.
    ev_async tx_w;

    struct diet recv; ///< Received packet numbers still needing to be ACKed.

    double reorder_fract;
    uint64_t lg_sent_before_rto;
    ev_tstamp srtt;
    ev_tstamp rttvar;
    uint64_t reorder_thresh;
    ev_tstamp loss_t;

    /// Sent-but-unACKed packets. The @p buf and @len fields of the w_iov
    /// structs are relative to any stream data.
    ///
    struct w_iov_sq sent_pkts;
    struct diet acked_pkts;

    uint64_t lg_sent;  ///< Largest packet number sent
    uint64_t lg_acked; ///< Largest packet number for which an ACK was received
    ev_tstamp latest_rtt;

    // CC state
    uint64_t cwnd;
    uint64_t in_flight;
    uint64_t rec_end;
    uint64_t ssthresh;

    // TLS state
    ptls_t * tls;
    uint8_t in_sec[PTLS_MAX_DIGEST_SIZE];
    uint8_t out_sec[PTLS_MAX_DIGEST_SIZE];
    ptls_aead_context_t * in_kp0;
    ptls_aead_context_t * out_kp0;

    uint8_t tp_buf[64];
    ptls_raw_extension_t tp_ext[2];
    ptls_handshake_properties_t tls_hshake_prop;
    uint8_t stateless_reset_token[16];
    uint64_t max_data;
    uint64_t max_stream_data;
    uint32_t max_stream_id;
    uint16_t idle_timeout;
    uint16_t max_packet_size;

    // uint8_t _unused3[2];
};


extern uint16_t initial_idle_timeout;
extern uint64_t initial_max_data;
extern uint64_t initial_max_stream_data;
extern uint32_t initial_max_stream_id;


extern int __attribute__((nonnull))
ipnp_splay_cmp(const struct q_conn * const a, const struct q_conn * const b);

extern int __attribute__((nonnull))
cid_splay_cmp(const struct q_conn * const a, const struct q_conn * const b);

SPLAY_PROTOTYPE(ipnp_splay, q_conn, node_ipnp, ipnp_splay_cmp)
SPLAY_PROTOTYPE(cid_splay, q_conn, node_cid, cid_splay_cmp)


#define CONN_STAT_IDLE 0
#define CONN_STAT_VERS_SENT 1
#define CONN_STAT_VERS_REJ 2
#define CONN_STAT_VERS_OK 3
#define CONN_STAT_ESTB 4
#define CONN_STAT_CLSD 5

#define CONN_FLAG_CLNT 0x01 ///< We are client on this connection (or server)
#define CONN_FLAG_EMBR 0x02 ///< Connection is in handshake
#define CONN_FLAG_RX 0x04   ///< We had an RX event on this connection
#define CONN_FLAG_TX 0x08   ///< We have a pending TX on this connection

#define is_clnt(c) (is_set(CONN_FLAG_CLNT, (c)->flags))
#define is_serv(c) (!is_clnt(c))
#define conn_type(c)                                                           \
    (is_set(CONN_FLAG_EMBR, (c)->flags)                                        \
         ? (is_clnt(c) ? "embr clnt" : "embr serv")                            \
         : (is_clnt(c) ? "clnt" : "serv"))

struct ev_loop;

extern void __attribute__((nonnull)) detect_lost_pkts(struct q_conn * const c);

extern void __attribute__((nonnull))
cid_splay(struct q_conn * const c, const struct sockaddr_in * const peer);

extern void __attribute__((nonnull))
ld_alarm(struct ev_loop * const l, ev_timer * const w, int e);

extern void __attribute__((nonnull))
tx(struct ev_loop * const l, ev_async * const w, int e);

extern void __attribute__((nonnull))
rx(struct ev_loop * const l, ev_io * const rx_w, int e);

extern struct q_conn * __attribute__((nonnull))
get_conn_by_ipnp(const struct sockaddr_in * const peer, const uint8_t type);

extern struct q_conn * get_conn_by_cid(const uint64_t id, const uint8_t type);

extern void __attribute__((nonnull)) set_ld_alarm(struct q_conn * const c);

extern void * __attribute__((nonnull)) loop_run(void * const arg);

extern void __attribute__((nonnull))
loop_update(struct ev_loop * const l, ev_async * const w, int e);
