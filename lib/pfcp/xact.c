/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ogs-pfcp.h"
#include "ogs-app.h"

#define PFCP_MIN_XACT_ID             1
#define PFCP_MAX_XACT_ID             0x800000

typedef enum {
    PFCP_XACT_UNKNOWN_STAGE,
    PFCP_XACT_INITIAL_STAGE,
    PFCP_XACT_INTERMEDIATE_STAGE,
    PFCP_XACT_FINAL_STAGE,
} ogs_pfcp_xact_stage_t;

static int ogs_pfcp_xact_initialized = 0;
static uint32_t g_xact_id = 0;

static OGS_POOL(pool, ogs_pfcp_xact_t);

static ogs_pfcp_xact_t *ogs_pfcp_xact_remote_create(
        ogs_pfcp_node_t *node, uint32_t sqn);
static ogs_pfcp_xact_stage_t ogs_pfcp_xact_get_stage(
        uint8_t type, uint32_t sqn);
static int ogs_pfcp_xact_delete(ogs_pfcp_xact_t *xact);
static int ogs_pfcp_xact_update_rx(ogs_pfcp_xact_t *xact, uint8_t type);
static ogs_pfcp_xact_t *ogs_pfcp_xact_find_by_xid(
        ogs_pfcp_node_t *node, uint8_t type, uint32_t xid);


static void response_timeout(void *data);
static void holding_timeout(void *data);
static void delayed_commit_timeout(void *data);

int ogs_pfcp_xact_init(void)
{
    ogs_assert(ogs_pfcp_xact_initialized == 0);

    ogs_pool_init(&pool, ogs_app()->pool.pfcp_xact);

    g_xact_id = 0;

    ogs_pfcp_xact_initialized = 1;

    return OGS_OK;
}

void ogs_pfcp_xact_final(void)
{
    ogs_assert(ogs_pfcp_xact_initialized == 1);

    ogs_pool_final(&pool);

    ogs_pfcp_xact_initialized = 0;
}

ogs_pfcp_xact_t *ogs_pfcp_xact_local_create(ogs_pfcp_node_t *node,
        void (*cb)(ogs_pfcp_xact_t *xact, void *data), void *data)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_t *xact = NULL;

    ogs_assert(node);

    ogs_pool_alloc(&pool, &xact);
    ogs_assert(xact);
    memset(xact, 0, sizeof *xact);
    xact->index = ogs_pool_index(&pool, xact);

    xact->org = OGS_PFCP_LOCAL_ORIGINATOR;
    xact->xid = OGS_NEXT_ID(g_xact_id, PFCP_MIN_XACT_ID, PFCP_MAX_XACT_ID);
    xact->node = node;
    xact->cb = cb;
    xact->data = data;

    xact->tm_response = ogs_timer_add(
            ogs_app()->timer_mgr, response_timeout, xact);
    ogs_assert(xact->tm_response);
    xact->response_rcount = ogs_app()->time.message.pfcp.n1_response_rcount,

    xact->tm_holding = ogs_timer_add(
            ogs_app()->timer_mgr, holding_timeout, xact);
    ogs_assert(xact->tm_holding);
    xact->holding_rcount = ogs_app()->time.message.pfcp.n1_holding_rcount,

    xact->tm_delayed_commit = ogs_timer_add(
            ogs_app()->timer_mgr, delayed_commit_timeout, xact);
    ogs_assert(xact->tm_delayed_commit);

    ogs_list_add(xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
            &xact->node->local_list : &xact->node->remote_list, xact);

    ogs_list_init(&xact->pdr_to_create_list);

    ogs_debug("[%d] %s Create  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            OGS_ADDR(&node->addr, buf),
            OGS_PORT(&node->addr));

    return xact;
}

static ogs_pfcp_xact_t *ogs_pfcp_xact_remote_create(
        ogs_pfcp_node_t *node, uint32_t sqn)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_t *xact = NULL;

    ogs_assert(node);

    ogs_pool_alloc(&pool, &xact);
    ogs_assert(xact);
    memset(xact, 0, sizeof *xact);
    xact->index = ogs_pool_index(&pool, xact);

    xact->org = OGS_PFCP_REMOTE_ORIGINATOR;
    xact->xid = OGS_PFCP_SQN_TO_XID(sqn);
    xact->node = node;

    xact->tm_response = ogs_timer_add(
            ogs_app()->timer_mgr, response_timeout, xact);
    ogs_assert(xact->tm_response);
    xact->response_rcount = ogs_app()->time.message.pfcp.n1_response_rcount,

    xact->tm_holding = ogs_timer_add(
            ogs_app()->timer_mgr, holding_timeout, xact);
    ogs_assert(xact->tm_holding);
    xact->holding_rcount = ogs_app()->time.message.pfcp.n1_holding_rcount,

    xact->tm_delayed_commit = ogs_timer_add(
            ogs_app()->timer_mgr, delayed_commit_timeout, xact);
    ogs_assert(xact->tm_delayed_commit);

    ogs_list_add(xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
            &xact->node->local_list : &xact->node->remote_list, xact);

    ogs_debug("[%d] %s Create  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            OGS_ADDR(&node->addr, buf),
            OGS_PORT(&node->addr));

    return xact;
}

ogs_pfcp_xact_t *ogs_pfcp_xact_cycle(ogs_pfcp_xact_t *xact)
{
    return ogs_pool_cycle(&pool, xact);
}

void ogs_pfcp_xact_delete_all(ogs_pfcp_node_t *node)
{
    ogs_pfcp_xact_t *xact = NULL, *next_xact = NULL;

    ogs_list_for_each_safe(&node->local_list, next_xact, xact)
        ogs_pfcp_xact_delete(xact);
    ogs_list_for_each_safe(&node->remote_list, next_xact, xact)
        ogs_pfcp_xact_delete(xact);
}

int ogs_pfcp_xact_update_tx(ogs_pfcp_xact_t *xact,
        ogs_pfcp_header_t *hdesc, ogs_pkbuf_t *pkbuf)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_stage_t stage;
    ogs_pfcp_header_t *h = NULL;
    int pfcp_hlen = 0;

    ogs_assert(xact);
    ogs_assert(xact->node);
    ogs_assert(hdesc);
    ogs_assert(pkbuf);

    ogs_debug("[%d] %s UPD TX-%d  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            hdesc->type,
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    stage = ogs_pfcp_xact_get_stage(hdesc->type, xact->xid);
    if (xact->org == OGS_PFCP_LOCAL_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            if (xact->step != 0) {
                ogs_error("invalid step[%d] type[%d]", xact->step, hdesc->type);
                ogs_pkbuf_free(pkbuf);
                return OGS_ERROR;
            }
            break;

        case PFCP_XACT_INTERMEDIATE_STAGE:
            ogs_expect(0);
            ogs_pkbuf_free(pkbuf);
            return OGS_ERROR;

        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 2) {
                ogs_error("invalid step[%d] type[%d]", xact->step, hdesc->type);
                ogs_pkbuf_free(pkbuf);
                return OGS_ERROR;
            }
            break;

        default:
            ogs_assert_if_reached();
            break;
        }
    } else if (xact->org == OGS_PFCP_REMOTE_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            ogs_expect(0);
            ogs_pkbuf_free(pkbuf);
            return OGS_ERROR;

        case PFCP_XACT_INTERMEDIATE_STAGE:
        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 1) {
                ogs_error("invalid step[%d] type[%d]", xact->step, hdesc->type);
                ogs_pkbuf_free(pkbuf);
                return OGS_ERROR;
            }
            break;

        default:
            ogs_error("invalid stage[%d] type[%d]", stage, hdesc->type);
            ogs_pkbuf_free(pkbuf);
            return OGS_ERROR;
        }
    } else {
        ogs_error("invalid org[%d] type[%d]", xact->org, hdesc->type);
        ogs_pkbuf_free(pkbuf);
        return OGS_ERROR;
    }

    if (hdesc->type >= OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE) {
        pfcp_hlen = OGS_PFCP_HEADER_LEN;
    } else {
        pfcp_hlen = OGS_PFCP_HEADER_LEN - OGS_PFCP_SEID_LEN;
    }

    ogs_pkbuf_push(pkbuf, pfcp_hlen);
    h = (ogs_pfcp_header_t *)pkbuf->data;
    memset(h, 0, pfcp_hlen);

    h->version = OGS_PFCP_VERSION;
    h->type = hdesc->type;

    if (h->type >= OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE) {
        h->seid_presence = 1;
        h->seid = htobe64(hdesc->seid);
        h->sqn = OGS_PFCP_XID_TO_SQN(xact->xid);
    } else {
        h->seid_presence = 0;
        h->sqn_only = OGS_PFCP_XID_TO_SQN(xact->xid);
    }
    h->length = htobe16(pkbuf->len - 4);

    /* Save Message type and packet of this step */
    xact->seq[xact->step].type = h->type;
    xact->seq[xact->step].pkbuf = pkbuf;

    /* Step */
    xact->step++;

    return OGS_OK;
}

static int ogs_pfcp_xact_update_rx(ogs_pfcp_xact_t *xact, uint8_t type)
{
    int rv = OGS_OK;
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_stage_t stage;

    ogs_debug("[%d] %s UPD RX-%d  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            type,
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    stage = ogs_pfcp_xact_get_stage(type, xact->xid);
    if (xact->org == OGS_PFCP_LOCAL_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            ogs_expect(0);
            return OGS_ERROR;

        case PFCP_XACT_INTERMEDIATE_STAGE:
            if (xact->seq[1].type == type) {
                ogs_pkbuf_t *pkbuf = NULL;

                if (xact->step != 2 && xact->step != 3) {
                    ogs_error("invalid step[%d] type[%d]", xact->step, type);
                    ogs_pkbuf_free(pkbuf);
                    return OGS_ERROR;
                }

                pkbuf = xact->seq[2].pkbuf;
                if (pkbuf) {
                    if (xact->tm_holding)
                        ogs_timer_start(xact->tm_holding,
                                ogs_app()->time.message.
                                    pfcp.t1_holding_duration);

                    ogs_warn("[%d] %s Request Duplicated. Retransmit!"
                            " for step %d type %d peer [%s]:%d",
                            xact->xid,
                            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
                                "LOCAL " : "REMOTE",
                            xact->step, type,
                            OGS_ADDR(&xact->node->addr,
                                buf),
                            OGS_PORT(&xact->node->addr));
                    rv = ogs_pfcp_sendto(xact->node, pkbuf);
                    ogs_expect(rv == OGS_OK);
                } else {
                    ogs_warn("[%d] %s Request Duplicated. Discard!"
                            " for step %d type %d peer [%s]:%d",
                            xact->xid,
                            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
                                "LOCAL " : "REMOTE",
                            xact->step, type,
                            OGS_ADDR(&xact->node->addr,
                                buf),
                            OGS_PORT(&xact->node->addr));
                }

                return OGS_RETRY;
            }

            if (xact->step != 1) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                return OGS_ERROR;
            }

            if (xact->tm_holding)
                ogs_timer_start(xact->tm_holding,
                        ogs_app()->time.message.pfcp.t1_holding_duration);

            break;

        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 1) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                return OGS_ERROR;
            }
            break;

        default:
            ogs_error("invalid stage[%d]", stage);
            return OGS_ERROR;
        }
    } else if (xact->org == OGS_PFCP_REMOTE_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            if (xact->seq[0].type == type) {
                ogs_pkbuf_t *pkbuf = NULL;

                if (xact->step != 1 && xact->step != 2) {
                    ogs_error("invalid step[%d] type[%d]", xact->step, type);
                    return OGS_ERROR;
                }

                pkbuf = xact->seq[1].pkbuf;
                if (pkbuf) {
                    if (xact->tm_holding)
                        ogs_timer_start(xact->tm_holding,
                                ogs_app()->time.message.
                                    pfcp.t1_holding_duration);

                    ogs_warn("[%d] %s Request Duplicated. Retransmit!"
                            " for step %d type %d peer [%s]:%d",
                            xact->xid,
                            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
                                "LOCAL " : "REMOTE",
                            xact->step, type,
                            OGS_ADDR(&xact->node->addr,
                                buf),
                            OGS_PORT(&xact->node->addr));
                    rv = ogs_pfcp_sendto(xact->node, pkbuf);
                    ogs_expect(rv == OGS_OK);
                } else {
                    ogs_warn("[%d] %s Request Duplicated. Discard!"
                            " for step %d type %d peer [%s]:%d",
                            xact->xid,
                            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
                                "LOCAL " : "REMOTE",
                            xact->step, type,
                            OGS_ADDR(&xact->node->addr,
                                buf),
                            OGS_PORT(&xact->node->addr));
                }

                return OGS_RETRY;
            }

            if (xact->step != 0) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                return OGS_ERROR;
            }
            if (xact->tm_holding)
                ogs_timer_start(xact->tm_holding,
                        ogs_app()->time.message.pfcp.t1_holding_duration);

            break;

        case PFCP_XACT_INTERMEDIATE_STAGE:
            ogs_expect(0);
            return OGS_ERROR;

        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 2) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                return OGS_ERROR;
            }

            /* continue */
            break;

        default:
            ogs_error("invalid stage[%d]", stage);
            return OGS_ERROR;
        }
    } else {
        ogs_error("invalid org[%d]", xact->org);
        return OGS_ERROR;
    }

    if (xact->tm_response)
        ogs_timer_stop(xact->tm_response);

    /* Save Message type of this step */
    xact->seq[xact->step].type = type;

    /* Step */
    xact->step++;

    return OGS_OK;
}

int ogs_pfcp_xact_commit(ogs_pfcp_xact_t *xact)
{
    int rv;
    char buf[OGS_ADDRSTRLEN];

    uint8_t type;
    ogs_pkbuf_t *pkbuf = NULL;
    ogs_pfcp_xact_stage_t stage;

    ogs_assert(xact);
    ogs_assert(xact->node);

    ogs_debug("[%d] %s Commit  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    type = xact->seq[xact->step-1].type;
    stage = ogs_pfcp_xact_get_stage(type, xact->xid);

    if (xact->org == OGS_PFCP_LOCAL_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            if (xact->step != 1) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                ogs_pfcp_xact_delete(xact);
                return OGS_ERROR;
            }

            if (xact->tm_response)
                ogs_timer_start(xact->tm_response,
                        ogs_app()->time.message.pfcp.t1_response_duration);

            break;

        case PFCP_XACT_INTERMEDIATE_STAGE:
            ogs_expect(0);
            ogs_pfcp_xact_delete(xact);
            return OGS_ERROR;

        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 2 && xact->step != 3) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                ogs_pfcp_xact_delete(xact);
                return OGS_ERROR;
            }
            if (xact->step == 2) {
                ogs_pfcp_xact_delete(xact);
                return OGS_OK;
            }

            break;

        default:
            ogs_error("invalid stage[%d] type[%d]", stage, type);
            ogs_pfcp_xact_delete(xact);
            return OGS_ERROR;
        }
    } else if (xact->org == OGS_PFCP_REMOTE_ORIGINATOR) {
        switch (stage) {
        case PFCP_XACT_INITIAL_STAGE:
            ogs_expect(0);
            ogs_pfcp_xact_delete(xact);
            return OGS_ERROR;

        case PFCP_XACT_INTERMEDIATE_STAGE:
            if (xact->step != 2) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                ogs_pfcp_xact_delete(xact);
                return OGS_ERROR;
            }
            if (xact->tm_response)
                ogs_timer_start(xact->tm_response,
                        ogs_app()->time.message.pfcp.t1_response_duration);

            break;

        case PFCP_XACT_FINAL_STAGE:
            if (xact->step != 2 && xact->step != 3) {
                ogs_error("invalid step[%d] type[%d]", xact->step, type);
                ogs_pfcp_xact_delete(xact);
                return OGS_ERROR;
            }
            if (xact->step == 3) {
                ogs_pfcp_xact_delete(xact);
                return OGS_OK;
            }

            break;

        default:
            ogs_error("invalid stage[%d] type[%d]", stage, type);
            ogs_pfcp_xact_delete(xact);
            return OGS_ERROR;
        }
    } else {
        ogs_error("invalid org[%d] type[%d]", xact->org, type);
        ogs_pfcp_xact_delete(xact);
        return OGS_ERROR;
    }

    pkbuf = xact->seq[xact->step-1].pkbuf;
    ogs_assert(pkbuf);

    rv = ogs_pfcp_sendto(xact->node, pkbuf);
    ogs_expect(rv == OGS_OK);

    return OGS_OK;
}

void ogs_pfcp_xact_delayed_commit(ogs_pfcp_xact_t *xact, ogs_time_t duration)
{
    ogs_assert(xact);
    ogs_assert(duration);
    ogs_assert(xact->tm_delayed_commit);

    ogs_timer_start(xact->tm_delayed_commit, duration);
}

static void response_timeout(void *data)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_t *xact = data;

    ogs_assert(xact);
    ogs_assert(xact->node);

    ogs_debug("[%d] %s Response Timeout "
            "for step %d type %d peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            xact->step, xact->seq[xact->step-1].type,
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    if (--xact->response_rcount > 0) {
        ogs_pkbuf_t *pkbuf = NULL;

        if (xact->tm_response)
            ogs_timer_start(xact->tm_response,
                    ogs_app()->time.message.pfcp.t1_response_duration);

        pkbuf = xact->seq[xact->step-1].pkbuf;
        ogs_assert(pkbuf);

        if (ogs_pfcp_sendto(xact->node, pkbuf) != OGS_OK) {
            ogs_error("ogs_pfcp_sendto() failed");
            goto out;
        }
    } else {
        ogs_warn("[%d] %s No Reponse. Give up! "
                "for step %d type %d peer [%s]:%d",
                xact->xid,
                xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
                xact->step, xact->seq[xact->step-1].type,
                OGS_ADDR(&xact->node->addr, buf),
                OGS_PORT(&xact->node->addr));

        if (xact->cb)
            xact->cb(xact, xact->data);

        ogs_pfcp_xact_delete(xact);
    }

    return;

out:
    ogs_pfcp_xact_delete(xact);
}

static void holding_timeout(void *data)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_t *xact = data;

    ogs_assert(xact);
    ogs_assert(xact->node);

    ogs_debug("[%d] %s Holding Timeout "
            "for step %d type %d peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            xact->step, xact->seq[xact->step-1].type,
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    if (--xact->holding_rcount > 0) {
        if (xact->tm_holding)
            ogs_timer_start(xact->tm_holding,
                    ogs_app()->time.message.pfcp.t1_holding_duration);
    } else {
        ogs_debug("[%d] %s Delete Transaction "
                "for step %d type %d peer [%s]:%d",
                xact->xid,
                xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
                xact->step, xact->seq[xact->step-1].type,
                OGS_ADDR(&xact->node->addr, buf),
                OGS_PORT(&xact->node->addr));
        ogs_pfcp_xact_delete(xact);
    }
}

static void delayed_commit_timeout(void *data)
{
    char buf[OGS_ADDRSTRLEN];
    ogs_pfcp_xact_t *xact = data;

    ogs_assert(xact);
    ogs_assert(xact->node);

    ogs_debug("[%d] %s Delayed Send Timeout "
            "for step %d type %d peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            xact->step, xact->seq[xact->step-1].type,
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    ogs_pfcp_xact_commit(xact);
}

int ogs_pfcp_xact_receive(
        ogs_pfcp_node_t *node, ogs_pfcp_header_t *h, ogs_pfcp_xact_t **xact)
{
    char buf[OGS_ADDRSTRLEN];
    int rv;
    ogs_pfcp_xact_t *new = NULL;

    ogs_assert(node);
    ogs_assert(h);

    new = ogs_pfcp_xact_find_by_xid(
            node, h->type, OGS_PFCP_SQN_TO_XID(h->sqn));
    if (!new)
        new = ogs_pfcp_xact_remote_create(node, h->sqn);
    ogs_assert(new);

    ogs_debug("[%d] %s Receive peer [%s]:%d",
            new->xid,
            new->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            OGS_ADDR(&node->addr, buf),
            OGS_PORT(&node->addr));

    rv = ogs_pfcp_xact_update_rx(new, h->type);
    if (rv == OGS_ERROR) {
        ogs_error("ogs_pfcp_xact_update_rx() failed");
        ogs_pfcp_xact_delete(new);
        return rv;
    } else if (rv == OGS_RETRY) {
        return rv;
    }

    *xact = new;
    return rv;
}

static ogs_pfcp_xact_stage_t ogs_pfcp_xact_get_stage(uint8_t type, uint32_t xid)
{
    ogs_pfcp_xact_stage_t stage = PFCP_XACT_UNKNOWN_STAGE;

    switch (type) {
    case OGS_PFCP_HEARTBEAT_REQUEST_TYPE:
    case OGS_PFCP_ASSOCIATION_SETUP_REQUEST_TYPE:
    case OGS_PFCP_ASSOCIATION_UPDATE_REQUEST_TYPE:
    case OGS_PFCP_ASSOCIATION_RELEASE_REQUEST_TYPE:
    case OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE:
    case OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE:
    case OGS_PFCP_SESSION_DELETION_REQUEST_TYPE:
    case OGS_PFCP_SESSION_SET_DELETION_REQUEST_TYPE:
    case OGS_PFCP_SESSION_REPORT_REQUEST_TYPE:
        stage = PFCP_XACT_INITIAL_STAGE;
        break;
    case OGS_PFCP_HEARTBEAT_RESPONSE_TYPE:
    case OGS_PFCP_ASSOCIATION_SETUP_RESPONSE_TYPE:
    case OGS_PFCP_ASSOCIATION_UPDATE_RESPONSE_TYPE:
    case OGS_PFCP_ASSOCIATION_RELEASE_RESPONSE_TYPE:
    case OGS_PFCP_VERSION_NOT_SUPPORTED_RESPONSE_TYPE:
    case OGS_PFCP_SESSION_ESTABLISHMENT_RESPONSE_TYPE:
    case OGS_PFCP_SESSION_MODIFICATION_RESPONSE_TYPE:
    case OGS_PFCP_SESSION_DELETION_RESPONSE_TYPE:
    case OGS_PFCP_SESSION_SET_DELETION_RESPONSE_TYPE:
    case OGS_PFCP_SESSION_REPORT_RESPONSE_TYPE:
        stage = PFCP_XACT_FINAL_STAGE;
        break;

    default:
        ogs_error("Not implemented PFCPv2 Message Type(%d)", type);
        break;
    }

    return stage;
}

static ogs_pfcp_xact_t *ogs_pfcp_xact_find_by_xid(
        ogs_pfcp_node_t *node, uint8_t type, uint32_t xid)
{
    char buf[OGS_ADDRSTRLEN];

    ogs_list_t *list = NULL;
    ogs_pfcp_xact_t *xact = NULL;

    ogs_assert(node);

    switch (ogs_pfcp_xact_get_stage(type, xid)) {
    case PFCP_XACT_INITIAL_STAGE:
        list = &node->remote_list;
        break;
    case PFCP_XACT_INTERMEDIATE_STAGE:
        list = &node->local_list;
        break;
    case PFCP_XACT_FINAL_STAGE:
        list = &node->local_list;
        break;
    default:
        ogs_assert_if_reached();
        break;
    }

    ogs_assert(list);
    ogs_list_for_each(list, xact) {
        if (xact->xid == xid) {
            ogs_debug("[%d] %s Find    peer [%s]:%d",
                xact->xid,
                xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
                OGS_ADDR(&node->addr, buf),
                OGS_PORT(&node->addr));
            break;
        }
    }

    return xact;
}

static int ogs_pfcp_xact_delete(ogs_pfcp_xact_t *xact)
{
    char buf[OGS_ADDRSTRLEN];

    ogs_assert(xact);
    ogs_assert(xact->node);

    ogs_debug("[%d] %s Delete  peer [%s]:%d",
            xact->xid,
            xact->org == OGS_PFCP_LOCAL_ORIGINATOR ? "LOCAL " : "REMOTE",
            OGS_ADDR(&xact->node->addr, buf),
            OGS_PORT(&xact->node->addr));

    if (xact->seq[0].pkbuf)
        ogs_pkbuf_free(xact->seq[0].pkbuf);
    if (xact->seq[1].pkbuf)
        ogs_pkbuf_free(xact->seq[1].pkbuf);
    if (xact->seq[2].pkbuf)
        ogs_pkbuf_free(xact->seq[2].pkbuf);

    if (xact->tm_response)
        ogs_timer_delete(xact->tm_response);
    if (xact->tm_holding)
        ogs_timer_delete(xact->tm_holding);
    if (xact->tm_delayed_commit)
        ogs_timer_delete(xact->tm_delayed_commit);

    ogs_list_remove(xact->org == OGS_PFCP_LOCAL_ORIGINATOR ?
            &xact->node->local_list : &xact->node->remote_list, xact);
    ogs_pool_free(&pool, xact);

    return OGS_OK;
}
