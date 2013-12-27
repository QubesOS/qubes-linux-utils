/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

/* See also http://wiki.qubes-os.org/trac/wiki/Qrexec */

#include <stdint.h>

#define QREXEC_PROTOCOL_VERSION 2
#define MAX_FDS 256
#define MAX_DATA_CHUNK 4096

#define RPC_REQUEST_COMMAND "QUBESRPC"
#define VCHAN_BASE_PORT 512
#define MAX_DATA_CHUNK 4096

/* Messages sent over control vchan between daemon(dom0) and agent(vm).
 * The same are used between client(dom0) and daemon(dom0).
 */
enum {
    /* daemon->agent messages */

    /* start process in VM and pass its stdin/out/err to dom0
     * struct exec_params passed as data */
    MSG_EXEC_CMDLINE = 0x200,

    /* start process in VM discarding its stdin/out/err (connect to /dev/null)
    * struct exec_params passed as data */
    MSG_JUST_EXEC,

    /* connect to existing process in VM to receive its stdin/out/err
     * struct service_params passed as cmdline field in exec_params */
    MSG_SERVICE_CONNECT,

    /* refuse to start a service (denied by policy, invalid parameters etc)
     * struct service_params passed as data to identify which service call was
     * refused */
    MSG_SERVICE_REFUSED,

    /* agent->daemon messages */
    /* call Qubes RPC service
     * struct trigger_service_params passed as data */
    MSG_TRIGGER_SERVICE = 0x210,


    /* connection was terminated, struct exec_params passed as data (with empty
     * cmdline field) informs about released vchan port */
    MSG_CONNECTION_TERMINATED,

    /* common messages */
    /* initialize connection, struct peer_info passed as data
     * should be sent as the first message (server first, then client) */
    MSG_HELLO = 0x300,
};

/* uniform for all peers, data type depends on message type */
struct msg_header {
    uint32_t type;           /* message type */
    uint32_t len;            /* data length */
};

/* variable size */
struct exec_params {
    uint32_t connect_domain; /* target domain name */
    uint32_t connect_port;   /* target vchan port for i/o exchange */
    char cmdline[0];         /* command line to execute, size = msg_header.len - sizeof(struct exec_params) */
};

struct service_params {
    char ident[32];
};

struct trigger_service_params {
    char service_name[64];
    char target_domain[32];
    struct service_params request_id; /* service request id */
};

struct peer_info {
    uint32_t version; /* qrexec protocol version */
};

/* data vchan client<->agent, separate for each VM process */
enum {
    /* stdin dom0->VM */
    MSG_DATA_STDIN = 0x190,
    /* stdout VM->dom0 */
    MSG_DATA_STDOUT,
    /* stderr VM->dom0 */
    MSG_DATA_STDERR,
    /* VM process exit code VM->dom0 (int) */
    MSG_DATA_EXIT_CODE,
};

// linux-specific stuff below

#define QREXEC_AGENT_TRIGGER_PATH "/var/run/qubes/qrexec-agent"
#define QREXEC_AGENT_FDPASS_PATH "/var/run/qubes/qrexec-agent-fdpass"
#define MEMINFO_WRITER_PIDFILE "/var/run/meminfo-writer.pid"
#define QUBES_RPC_MULTIPLEXER_PATH "/usr/lib/qubes/qubes-rpc-multiplexer"
#define QREXEC_DAEMON_SOCKET_DIR "/var/run/qubes"

