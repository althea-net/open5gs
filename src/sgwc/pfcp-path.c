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

#include "pfcp-path.h"

static void pfcp_node_fsm_init(ogs_pfcp_node_t *node, bool try_to_assoicate)
{
    sgwc_event_t e;

    ogs_assert(node);

    memset(&e, 0, sizeof(e));
    e.pfcp_node = node;

    if (try_to_assoicate == true) {
        node->t_association = ogs_timer_add(ogs_app()->timer_mgr,
                sgwc_timer_pfcp_association, node);
        ogs_assert(node->t_association);
    }

    ogs_fsm_init(&node->sm, sgwc_pfcp_state_initial, sgwc_pfcp_state_final, &e);
}

static void pfcp_node_fsm_fini(ogs_pfcp_node_t *node)
{
    sgwc_event_t e;

    ogs_assert(node);

    memset(&e, 0, sizeof(e));
    e.pfcp_node = node;

    ogs_fsm_fini(&node->sm, &e);

    if (node->t_association)
        ogs_timer_delete(node->t_association);
}

static void pfcp_recv_cb(short when, ogs_socket_t fd, void *data)
{
    int rv;

    ssize_t size;
    sgwc_event_t *e = NULL;
    ogs_pkbuf_t *pkbuf = NULL;
    ogs_sockaddr_t from;
    ogs_pfcp_node_t *node = NULL;
    ogs_pfcp_header_t *h = NULL;

    ogs_assert(fd != INVALID_SOCKET);

    pkbuf = ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    ogs_assert(pkbuf);
    ogs_pkbuf_put(pkbuf, OGS_MAX_SDU_LEN);

    size = ogs_recvfrom(fd, pkbuf->data, pkbuf->len, 0, &from);
    if (size <= 0) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                "ogs_recvfrom() failed");
        ogs_pkbuf_free(pkbuf);
        return;
    }

    ogs_pkbuf_trim(pkbuf, size);

    h = (ogs_pfcp_header_t *)pkbuf->data;
    if (h->version != OGS_PFCP_VERSION) {
        ogs_pfcp_header_t rsp;

        ogs_error("Not supported version[%d]", h->version);

        memset(&rsp, 0, sizeof rsp);
        rsp.flags = (OGS_PFCP_VERSION << 5);
        rsp.type = OGS_PFCP_VERSION_NOT_SUPPORTED_RESPONSE_TYPE;
        rsp.length = htobe16(4);
        rsp.sqn_only = h->sqn_only;
        if (ogs_sendto(fd, &rsp, 8, 0, &from) < 0) {
            ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                    "ogs_sendto() failed");
        }
        ogs_pkbuf_free(pkbuf);

        return;
    }

    e = sgwc_event_new(SGWC_EVT_SXA_MESSAGE);
    ogs_assert(e);

    node = ogs_pfcp_node_find(&ogs_pfcp_self()->pfcp_peer_list, &from);
    if (!node) {
        node = ogs_pfcp_node_add(&ogs_pfcp_self()->pfcp_peer_list, &from);
        ogs_assert(node);

        node->sock = data;
        pfcp_node_fsm_init(node, false);
    }
    e->pfcp_node = node;
    e->pkbuf = pkbuf;

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_error("ogs_queue_push() failed:%d", (int)rv);
        ogs_pkbuf_free(e->pkbuf);
        sgwc_event_free(e);
    }
}

int sgwc_pfcp_open(void)
{
    ogs_socknode_t *node = NULL;
    ogs_sock_t *sock = NULL;

    /* PFCP Server */
    ogs_list_for_each(&ogs_pfcp_self()->pfcp_list, node) {
        sock = ogs_pfcp_server(node);
        if (!sock) return OGS_ERROR;

        node->poll = ogs_pollset_add(ogs_app()->pollset,
                OGS_POLLIN, sock->fd, pfcp_recv_cb, sock);
        ogs_assert(node->poll);
    }
    ogs_list_for_each(&ogs_pfcp_self()->pfcp_list6, node) {
        sock = ogs_pfcp_server(node);
        if (!sock) return OGS_ERROR;

        node->poll = ogs_pollset_add(ogs_app()->pollset,
                OGS_POLLIN, sock->fd, pfcp_recv_cb, sock);
        ogs_assert(node->poll);
    }

    OGS_SETUP_PFCP_SERVER;

    return OGS_OK;
}

void sgwc_pfcp_close(void)
{
    ogs_pfcp_node_t *pfcp_node = NULL;

    ogs_list_for_each(&ogs_pfcp_self()->pfcp_peer_list, pfcp_node)
        pfcp_node_fsm_fini(pfcp_node);

    ogs_socknode_remove_all(&ogs_pfcp_self()->pfcp_list);
    ogs_socknode_remove_all(&ogs_pfcp_self()->pfcp_list6);
}

static void sess_timeout(ogs_pfcp_xact_t *pfcp_xact, void *data)
{
    uint8_t pfcp_type;

    uint8_t gtp_cause;
    uint8_t gtp_type;
    ogs_gtp_xact_t * s11_xact = NULL;
    sgwc_sess_t *sess = data;
    sgwc_ue_t *sgwc_ue = NULL;

    ogs_assert(pfcp_xact);
    ogs_assert(sess);

    s11_xact = pfcp_xact->assoc_xact;

    if (!s11_xact) {
        ogs_error("sess_timeout: No s11 context");
        return;
    }

    switch (s11_xact->gtp_version) {
    case 1:
        gtp_cause = OGS_GTP1_CAUSE_NETWORK_FAILURE;
        break;
    case 2:
        gtp_cause = OGS_GTP2_CAUSE_TIMED_OUT_REQUEST;
        break;
    }

    sgwc_ue = sess->sgwc_ue;

    pfcp_type = pfcp_xact->seq[0].type;
    switch (pfcp_type) {
    case OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE:
        ogs_error("Timeout: No PFCP session establishment response");
        ogs_gtp_send_error_message(
                s11_xact, sgwc_ue ? sgwc_ue->mme_s11_teid : 0,
                OGS_GTP2_CREATE_SESSION_RESPONSE_TYPE, gtp_cause);
        break;
    case OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE:
        ogs_error("Timeout: No PFCP session modification response");

        // multiple gtp message-types are translated to pfcp session mod request
        switch (s11_xact->seq[0].type) {
        case OGS_GTP2_MODIFY_BEARER_REQUEST_TYPE:
            gtp_type = OGS_GTP2_MODIFY_BEARER_RESPONSE_TYPE;
            break;
        case OGS_GTP2_RELEASE_ACCESS_BEARERS_REQUEST_TYPE:
            gtp_type = OGS_GTP2_RELEASE_ACCESS_BEARERS_RESPONSE_TYPE;
            break;
        default:
            ogs_error("GTP request type not implemented: %d", s11_xact->seq[0].type);
            gtp_type = OGS_GTP2_MODIFY_BEARER_RESPONSE_TYPE;
            break;
        }

        ogs_gtp_send_error_message(
                s11_xact, sgwc_ue ? sgwc_ue->mme_s11_teid : 0,
                gtp_type, gtp_cause);
        break;
    case OGS_PFCP_SESSION_DELETION_REQUEST_TYPE:
        ogs_error("Timeout: No PFCP session deletion response");
        ogs_gtp_send_error_message(
                s11_xact, sgwc_ue ? sgwc_ue->mme_s11_teid : 0,
                OGS_GTP2_DELETE_SESSION_RESPONSE_TYPE, gtp_cause);
        sgwc_sess_remove(sess);
        break;
    default:
        ogs_error("PFCP request type not implemented [type:%d]", pfcp_type);
        break;
    }
}

static void bearer_timeout(ogs_pfcp_xact_t *xact, void *data)
{
    uint8_t type;

    ogs_assert(xact);
    type = xact->seq[0].type;

    switch (type) {
    case OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE:
        ogs_error("No PFCP session modification response");
        break;
    default:
        ogs_error("Not implemented [type:%d]", type);
        break;
    }
}

int sgwc_pfcp_send_bearer_to_modify_list(
        sgwc_sess_t *sess, ogs_pfcp_xact_t *xact)
{
    int rv;
    ogs_pkbuf_t *sxabuf = NULL;
    ogs_pfcp_header_t h;

    ogs_assert(sess);
    ogs_assert(xact);

    xact->local_seid = sess->sgwc_sxa_seid;

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE;
    h.seid = sess->sgwu_sxa_seid;

    sxabuf = sgwc_sxa_build_bearer_to_modify_list(h.type, sess, xact);
    if (!sxabuf) {
        ogs_error("sgwc_sxa_build_bearer_to_modify_list() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_update_tx(xact, &h, sxabuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_pfcp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

int sgwc_pfcp_send_session_establishment_request(
        sgwc_sess_t *sess, ogs_gtp_xact_t *gtp_xact, ogs_pkbuf_t *gtpbuf)
{
    int rv;
    ogs_pkbuf_t *sxabuf = NULL;
    ogs_pfcp_header_t h;
    ogs_pfcp_xact_t *xact = NULL;

    ogs_assert(sess);

    xact = ogs_pfcp_xact_local_create(sess->pfcp_node, sess_timeout, sess);
    if (!xact) {
        ogs_error("ogs_pfcp_xact_local_create() failed");
        return OGS_ERROR;
    }

    xact->assoc_xact = gtp_xact;
    if (gtpbuf) {
        xact->gtpbuf = ogs_pkbuf_copy(gtpbuf);
        if (!xact->gtpbuf) {
            ogs_error("ogs_pkbuf_copy() failed");
            return OGS_ERROR;
        }
    }
    xact->local_seid = sess->sgwc_sxa_seid;

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE;
    h.seid = sess->sgwu_sxa_seid;

    sxabuf = sgwc_sxa_build_session_establishment_request(h.type, sess);
    if (!sxabuf) {
        ogs_error("sgwc_sxa_build_session_establishment_request() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_update_tx(xact, &h, sxabuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_pfcp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

int sgwc_pfcp_resend_established_sessions(ogs_pfcp_node_t *node)
{
    sgwc_ue_t *sgwc_ue = NULL;
    sgwc_sess_t *sess = NULL;

    ogs_pfcp_cp_send_session_set_deletion_request(node, NULL);

    ogs_list_for_each(&sgwc_self()->sgw_ue_list, sgwc_ue) {
        ogs_list_for_each(&sgwc_ue->sess_list, sess) {
            if (sess->pfcp_node == node) {
                if (sess->pfcp_established) {
                    sess->sgwu_sxa_seid = 0;
                    sgwc_pfcp_send_session_establishment_request(sess, NULL, NULL);                
                }
            }
        }
    }
    return OGS_OK;
}

int sgwc_pfcp_send_session_modification_request(
        sgwc_sess_t *sess, ogs_gtp_xact_t *gtp_xact,
        ogs_pkbuf_t *gtpbuf, uint64_t flags)
{
    ogs_pfcp_xact_t *xact = NULL;
    sgwc_bearer_t *bearer = NULL;

    ogs_assert(sess);

    xact = ogs_pfcp_xact_local_create(sess->pfcp_node, sess_timeout, sess);
    if (!xact) {
        ogs_error("ogs_pfcp_xact_local_create() failed");
        return OGS_ERROR;
    }

    xact->assoc_xact = gtp_xact;
    xact->modify_flags = flags | OGS_PFCP_MODIFY_SESSION;
    if (gtpbuf) {
        xact->gtpbuf = ogs_pkbuf_copy(gtpbuf);
        if (!xact->gtpbuf) {
            ogs_error("ogs_pkbuf_copy() failed");
            return OGS_ERROR;
        }
    }
    xact->local_seid = sess->sgwc_sxa_seid;

    ogs_list_for_each(&sess->bearer_list, bearer)
        ogs_list_add(&xact->bearer_to_modify_list, &bearer->to_modify_node);

    return sgwc_pfcp_send_bearer_to_modify_list(sess, xact);
}

int sgwc_pfcp_send_bearer_modification_request(
        sgwc_bearer_t *bearer, ogs_gtp_xact_t *gtp_xact,
        ogs_pkbuf_t *gtpbuf, uint64_t flags)
{
    int rv;
    ogs_pkbuf_t *sxabuf = NULL;
    ogs_pfcp_header_t h;
    ogs_pfcp_xact_t *xact = NULL;
    sgwc_sess_t *sess = NULL;

    ogs_assert(bearer);
    sess = bearer->sess;
    ogs_assert(sess);

    xact = ogs_pfcp_xact_local_create(sess->pfcp_node, bearer_timeout, bearer);
    if (!xact) {
        ogs_error("ogs_pfcp_xact_local_create() failed");
        return OGS_ERROR;
    }

    xact->assoc_xact = gtp_xact;
    xact->modify_flags = flags;
    if (gtpbuf) {
        xact->gtpbuf = ogs_pkbuf_copy(gtpbuf);
        if (!xact->gtpbuf) {
            ogs_error("ogs_pkbuf_copy() failed");
            return OGS_ERROR;
        }
    }
    xact->local_seid = sess->sgwc_sxa_seid;

    ogs_list_add(&xact->bearer_to_modify_list, &bearer->to_modify_node);

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE;
    h.seid = sess->sgwu_sxa_seid;

    sxabuf = sgwc_sxa_build_bearer_to_modify_list(h.type, sess, xact);
    if (!sxabuf) {
        ogs_error("sgwc_sxa_build_bearer_to_modify_list() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_update_tx(xact, &h, sxabuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_pfcp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

int sgwc_pfcp_send_session_deletion_request(
        sgwc_sess_t *sess, ogs_gtp_xact_t *gtp_xact, ogs_pkbuf_t *gtpbuf)
{
    int rv;
    ogs_pkbuf_t *sxabuf = NULL;
    ogs_pfcp_header_t h;
    ogs_pfcp_xact_t *xact = NULL;

    if (!sess) {
        ogs_error("sess is null!");
        return OGS_ERROR;
    }

    xact = ogs_pfcp_xact_local_create(sess->pfcp_node, sess_timeout, sess);
    if (!xact) {
        ogs_error("ogs_pfcp_xact_local_create() failed");
        return OGS_ERROR;
    }

    xact->assoc_xact = gtp_xact;
    if (gtpbuf) {
        xact->gtpbuf = ogs_pkbuf_copy(gtpbuf);
        if (!xact->gtpbuf) {
            ogs_error("ogs_pkbuf_copy() failed");
            return OGS_ERROR;
        }
    }
    xact->local_seid = sess->sgwc_sxa_seid;

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_SESSION_DELETION_REQUEST_TYPE;
    h.seid = sess->sgwu_sxa_seid;

    sxabuf = sgwc_sxa_build_session_deletion_request(h.type, sess);
    if (!sxabuf) {
        ogs_error("sgwc_sxa_build_session_deletion_request() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_update_tx(xact, &h, sxabuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_pfcp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

int sgwc_pfcp_send_session_report_response(
        ogs_pfcp_xact_t *xact, sgwc_sess_t *sess, uint8_t cause)
{
    int rv;
    ogs_pkbuf_t *sxabuf = NULL;
    ogs_pfcp_header_t h;

    memset(&h, 0, sizeof(ogs_pfcp_header_t));
    h.type = OGS_PFCP_SESSION_REPORT_RESPONSE_TYPE;
    h.seid = sess->sgwu_sxa_seid;

    sxabuf = ogs_pfcp_build_session_report_response(h.type, cause);
    if (!sxabuf) {
        ogs_error("ogs_pfcp_build_session_report_response() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_update_tx(xact, &h, sxabuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_pfcp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_pfcp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}
