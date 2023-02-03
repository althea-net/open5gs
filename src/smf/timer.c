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

#include "timer.h"
#include "event.h"
#include "context.h"

const char *smf_timer_get_name(smf_timer_e id)
{
    switch (id) {
    case SMF_TIMER_PFCP_ASSOCIATION:
        return "SMF_TIMER_PFCP_ASSOCIATION";
    case SMF_TIMER_PFCP_NO_HEARTBEAT:
        return "SMF_TIMER_PFCP_NO_HEARTBEAT";
    case SMF_TIMER_NF_INSTANCE_REGISTRATION_INTERVAL:
        return "SMF_TIMER_NF_INSTANCE_REGISTRATION_INTERVAL";
    case SMF_TIMER_NF_INSTANCE_HEARTBEAT_INTERVAL:
        return "SMF_TIMER_NF_INSTANCE_HEARTBEAT_INTERVAL";
    case SMF_TIMER_NF_INSTANCE_NO_HEARTBEAT:
        return "SMF_TIMER_NF_INSTANCE_NO_HEARTBEAT";
    case SMF_TIMER_NF_INSTANCE_VALIDITY:
        return "SMF_TIMER_NF_INSTANCE_VALIDITY";
    case SMF_TIMER_SUBSCRIPTION_VALIDITY:
        return "SMF_TIMER_SUBSCRIPTION_VALIDITY";
    case SMF_TIMER_SBI_CLIENT_WAIT:
        return "SMF_TIMER_SBI_CLIENT_WAIT";
    default: 
       break;
    }

    return "UNKNOWN_TIMER";
}

static void timer_send_event(int timer_id, void *data)
{
    int rv;
    smf_event_t *e = NULL;
    smf_event_t *old_e = NULL;
    ogs_assert(data);

    switch (timer_id) {
    case SMF_TIMER_PFCP_ASSOCIATION:
    case SMF_TIMER_PFCP_NO_HEARTBEAT:
        e = smf_event_new(SMF_EVT_N4_TIMER);
        ogs_assert(e);
        e->timer_id = timer_id;
        e->pfcp_node = data;
        break;
    case SMF_TIMER_NF_INSTANCE_REGISTRATION_INTERVAL:
    case SMF_TIMER_NF_INSTANCE_HEARTBEAT_INTERVAL:
    case SMF_TIMER_NF_INSTANCE_NO_HEARTBEAT:
    case SMF_TIMER_NF_INSTANCE_VALIDITY:
    case SMF_TIMER_SUBSCRIPTION_VALIDITY:
        e = smf_event_new(SMF_EVT_SBI_TIMER);
        ogs_assert(e);
        e->timer_id = timer_id;
        e->sbi.data = data;
        break;
    case SMF_TIMER_SBI_CLIENT_WAIT:
        e = smf_event_new(SMF_EVT_SBI_TIMER);
        if (!e) {
            ogs_sbi_xact_t *sbi_xact = data;
            ogs_assert(sbi_xact);

            ogs_error("timer_send_event() failed");
            ogs_sbi_xact_remove(sbi_xact);
            return;
        }
        e->timer_id = timer_id;
        e->sbi.data = data;
        break;
    case SMF_TIMER_GX_CCA:
    case SMF_TIMER_GY_CCA:
        old_e = data;

        e = smf_event_new(SMF_EVT_DIAMETER_TIMER);
        ogs_assert(e);
        e->timer_id = timer_id;
        e->sess = old_e->sess;
        e->gx_message = old_e->gx_message;
        e->gtp_xact = old_e->gtp_xact;
        break;
    case SMF_TIMEOUT_PFCP_SER:
    case SMF_TIMEOUT_PFCP_SDR:
    case SMF_TIMEOUT_PFCP_SMR:
        e = smf_event_new(SMF_EVT_PFCP_TIMEOUT);
        ogs_assert(e);
        e->timer_id = timer_id;
        e->sess = data;
        break;
    default:
        ogs_fatal("Unknown timer id[%d]", timer_id);
        ogs_assert_if_reached();
        break;
    }

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_warn("ogs_queue_push() failed [%d] in %s",
                (int)rv, smf_timer_get_name(e->timer_id));
        smf_event_free(e);
    }
}

void smf_timer_pfcp_association(void *data)
{
    timer_send_event(SMF_TIMER_PFCP_ASSOCIATION, data);
}

void smf_timer_pfcp_no_heartbeat(void *data)
{
    timer_send_event(SMF_TIMER_PFCP_NO_HEARTBEAT, data);
}

void smf_timer_nf_instance_registration_interval(void *data)
{
    timer_send_event(SMF_TIMER_NF_INSTANCE_REGISTRATION_INTERVAL, data);
}

void smf_timer_nf_instance_heartbeat_interval(void *data)
{
    timer_send_event(SMF_TIMER_NF_INSTANCE_HEARTBEAT_INTERVAL, data);
}

void smf_timer_nf_instance_no_heartbeat(void *data)
{
    timer_send_event(SMF_TIMER_NF_INSTANCE_NO_HEARTBEAT, data);
}

void smf_timer_nf_instance_validity(void *data)
{
    timer_send_event(SMF_TIMER_NF_INSTANCE_VALIDITY, data);
}

void smf_timer_subscription_validity(void *data)
{
    timer_send_event(SMF_TIMER_SUBSCRIPTION_VALIDITY, data);
}

void smf_timer_sbi_client_wait_expire(void *data)
{
    timer_send_event(SMF_TIMER_SBI_CLIENT_WAIT, data);
}

void smf_timer_gx_no_cca(void *data)
{
    timer_send_event(SMF_TIMER_GX_CCA, data);
}

void smf_timer_gy_no_cca(void *data)
{
    timer_send_event(SMF_TIMER_GY_CCA, data);
}

void smf_timeout_pfcp_no_ser(void *data)
{
    timer_send_event(SMF_TIMEOUT_PFCP_SER, data);
}

void smf_timeout_pfcp_no_sdr(void *data)
{
    timer_send_event(SMF_TIMEOUT_PFCP_SDR, data);
}

void smf_timeout_pfcp_no_smr(void *data)
{
    timer_send_event(SMF_TIMEOUT_PFCP_SMR, data);
}
