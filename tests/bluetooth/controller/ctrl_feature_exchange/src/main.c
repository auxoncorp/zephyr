/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <sys/byteorder.h>
#include <ztest.h>
#include "kconfig.h"


#define ULL_LLCP_UNITTEST


#include <bluetooth/hci.h>
#include <sys/byteorder.h>
#include <sys/slist.h>
#include <sys/util.h>
#include "hal/ccm.h"

#include "util/mem.h"
#include "util/memq.h"

#include "pdu.h"
#include "ll.h"
#include "ll_settings.h"

#include "lll.h"
#include "lll_conn.h"

#include "ull_conn_types.h"
#include "ull_tx_queue.h"
#include "ull_llcp.h"
#include "ull_llcp_internal.h"


#include "helper_pdu.h"
#include "helper_util.h"
#include "helper_features.h"

struct ull_cp_conn conn;

static void setup(void)
{
	test_setup(&conn);
}


/*
 * +-----+                     +-------+            +-----+
 * | UT  |                     | LL_A  |            | LT  |
 * +-----+                     +-------+            +-----+
 *    |                            |                   |
 *    | Start                      |                   |
 *    | Feature Exchange Proc.     |                   |
 *    |--------------------------->|                   |
 *    |                            |                   |
 *    |                            | LL_FEATURE_REQ    |
 *    |                            |------------------>|
 *    |                            |                   |
 *    |                            |    LL_FEATURE_RSP |
 *    |                            |<------------------|
 *    |                            |                   |
 *    |     Feature Exchange Proc. |                   |
 *    |                   Complete |                   |
 *    |<---------------------------|                   |
 *    |                            |                   |
 */
void test_feature_exchange_mas_loc(void)
{
	u64_t err;
	u64_t featureset;
	struct node_tx *tx;
	struct node_rx_pdu *ntf;

	struct pdu_data_llctrl_feature_req local_feature_req;
	struct pdu_data_llctrl_feature_rsp remote_feature_rsp;

	featureset = DEFAULT_FEATURE;

	sys_put_le64(featureset, local_feature_req.features);
	sys_put_le64(featureset, remote_feature_rsp.features);

	test_set_role(&conn, BT_HCI_ROLE_MASTER);
	/* Connect */
	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	/* Initiate a Feature Exchange Procedure */
	err = ull_cp_feature_exchange(&conn);
	zassert_equal(err, BT_HCI_ERR_SUCCESS, NULL);

	event_prepare(&conn);
	/* Tx Queue should have one LL Control PDU */
	lt_rx(LL_FEATURE_REQ, &conn, &tx, &local_feature_req);
	lt_rx_q_is_empty(&conn);

	/* Rx */
	lt_tx(LL_FEATURE_RSP, &conn, &remote_feature_rsp);

	event_done(&conn);
	/* There should be one host notification */

	ut_rx_pdu(LL_FEATURE_RSP, &ntf,  &remote_feature_rsp);

	ut_rx_q_is_empty();

	zassert_equal(conn.lll.event_counter, 1,
		      "Wrong event-count %d\n", conn.lll.event_counter);
}

void test_feature_exchange_mas_loc_2(void)
{
	u8_t err;

	test_set_role(&conn, BT_HCI_ROLE_MASTER);
	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	err = ull_cp_feature_exchange(&conn);
	for (int i = 0U; i < PROC_CTX_BUF_NUM; i++) {
		zassert_equal(err, BT_HCI_ERR_SUCCESS, NULL);
		err = ull_cp_feature_exchange(&conn);
	}

	zassert_not_equal(err, BT_HCI_ERR_SUCCESS, NULL);
	zassert_equal(conn.lll.event_counter, 0,
		      "Wrong event-count %d\n", conn.lll.event_counter);
}

/*
 * +-----+ +-------+                  +-----+
 * | UT  | | LL_A  |                 | LT  |
 * +-----+ +-------+                 +-----+
 *   |        |                         |
 *   |        |    LL_SLAVE_FEATURE_REQ |
 *   |        |<------------------------|
 *   |        |                         |
 *   |        | LL_FEATURE_RSP          |
 *   |        |------------------------>|
 *   |        |                         |
 */
#define MAS_REM_NR_OF_EVENTS 2
void test_feature_exchange_mas_rem(void)
{
	u64_t set_featureset[] = {
		DEFAULT_FEATURE,
		LL_FEAT_BIT_MASK_VALID,
		EXPECTED_FEAT_EXCH_VALID,
		0xFFFFFFFFFFFFFFFF,
		0x0 };
	u64_t exp_featureset[] = {
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		0x0 };
	int feat_to_test = ARRAY_SIZE(set_featureset);
	struct node_tx *tx;

	struct pdu_data_llctrl_feature_req remote_feature_req;
	struct pdu_data_llctrl_feature_rsp local_feature_rsp;

	test_set_role(&conn, BT_HCI_ROLE_MASTER);
	/* Connect */
	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	for (int feat_count = 0; feat_count < feat_to_test; feat_count++) {
		sys_put_le64(set_featureset[feat_count],
			     remote_feature_req.features);
		sys_put_le64(exp_featureset[feat_count],
			     local_feature_rsp.features);

		event_prepare(&conn);

		lt_tx(LL_SLAVE_FEATURE_REQ, &conn, &remote_feature_req);

		event_done(&conn);

		event_prepare(&conn);

		lt_rx(LL_FEATURE_RSP, &conn, &tx, &local_feature_rsp);
		lt_rx_q_is_empty(&conn);

		event_done(&conn);

		ut_rx_q_is_empty();

		ull_cp_release_tx(tx);
	}
	zassert_equal(conn.lll.event_counter,
		      MAS_REM_NR_OF_EVENTS*(feat_to_test),
		      "Wrong event-count %d\n", conn.lll.event_counter);
}

#define MAS_REM_2_NR_OF_EVENTS 3
void test_feature_exchange_mas_rem_2(void)
{
	u64_t set_featureset[] = {
		DEFAULT_FEATURE,
		LL_FEAT_BIT_MASK_VALID,
		EXPECTED_FEAT_EXCH_VALID,
		0xFFFFFFFFFFFFFFFF,
		0x0 };
	u64_t exp_featureset[] = {
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		0x0 };
	u64_t ut_featureset[] = {
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE,
		DEFAULT_FEATURE };

	int feat_to_test = ARRAY_SIZE(set_featureset);
	u64_t err;
	struct node_tx *tx;
	struct node_rx_pdu *ntf;

	struct pdu_data_llctrl_feature_req remote_feature_req;
	struct pdu_data_llctrl_feature_rsp local_feature_rsp;
	struct pdu_data_llctrl_feature_req ut_feature_req;

	test_set_role(&conn, BT_HCI_ROLE_MASTER);
	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	for (int feat_count = 0; feat_count < feat_to_test; feat_count++) {
		sys_put_le64(set_featureset[feat_count],
			     remote_feature_req.features);
		sys_put_le64(exp_featureset[feat_count],
			     local_feature_rsp.features);
		sys_put_le64(ut_featureset[feat_count],
			     ut_feature_req.features);

		/*
		 * at the start of a loop all queues should be empty
		 */
		ut_rx_q_is_empty();
		lt_rx_q_is_empty(&conn);

		err = ull_cp_feature_exchange(&conn);
		zassert_equal(err, BT_HCI_ERR_SUCCESS, NULL);

		event_prepare(&conn);
		lt_tx(LL_SLAVE_FEATURE_REQ, &conn, &remote_feature_req);
		event_done(&conn);

		event_prepare(&conn);
		lt_rx(LL_FEATURE_REQ, &conn, &tx, &ut_feature_req);
		lt_tx(LL_FEATURE_RSP, &conn,  &local_feature_rsp);
		event_done(&conn);

		ull_cp_release_tx(tx);

		event_prepare(&conn);
		lt_rx(LL_FEATURE_RSP, &conn, &tx, &local_feature_rsp);
		event_done(&conn);

		ut_rx_pdu(LL_FEATURE_RSP, &ntf, &local_feature_rsp);

		/*
		 * at the end of a loop all queues should be empty
		 */
		ut_rx_q_is_empty();
		lt_rx_q_is_empty(&conn);

		ull_cp_release_tx(tx);
		ull_cp_release_ntf(ntf);

	}


	zassert_equal(conn.lll.event_counter,
		      MAS_REM_2_NR_OF_EVENTS*(feat_to_test),
		      "Wrong event-count %d\n", conn.lll.event_counter);
}


void test_slave_feature_exchange_sla_loc(void)
{
	u64_t err;
	u64_t featureset;
	struct node_tx *tx;
	struct node_rx_pdu *ntf;

	struct pdu_data_llctrl_feature_req local_feature_req;
	struct pdu_data_llctrl_feature_rsp remote_feature_rsp;

	featureset = DEFAULT_FEATURE;

	sys_put_le64(featureset, local_feature_req.features);
	sys_put_le64(featureset, remote_feature_rsp.features);

	test_set_role(&conn, BT_HCI_ROLE_SLAVE);
	/* Connect */
	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	/* Initiate a Feature Exchange Procedure */
	err = ull_cp_feature_exchange(&conn);
	zassert_equal(err, BT_HCI_ERR_SUCCESS, NULL);

	event_prepare(&conn);
	/* Tx Queue should have one LL Control PDU */
	lt_rx(LL_SLAVE_FEATURE_REQ, &conn, &tx, &local_feature_req);
	lt_rx_q_is_empty(&conn);

	/* Rx */
	lt_tx(LL_FEATURE_RSP, &conn, &remote_feature_rsp);

	event_done(&conn);
	/* There should be one host notification */

	ut_rx_pdu(LL_FEATURE_RSP, &ntf,  &remote_feature_rsp);
	ut_rx_q_is_empty();
	zassert_equal(conn.lll.event_counter, 1,
		      "Wrong event-count %d\n", conn.lll.event_counter);
}


void test_feature_exchange_sla_loc_unknown_rsp(void)
{
	u64_t err;
	u64_t featureset;
	struct node_tx *tx;

	struct pdu_data_llctrl_feature_req local_feature_req;

	struct node_rx_pdu *ntf;
	struct pdu_data_llctrl_unknown_rsp unknown_rsp = {
		.type = PDU_DATA_LLCTRL_TYPE_SLAVE_FEATURE_REQ
	};

	featureset = DEFAULT_FEATURE;
	sys_put_le64(featureset, local_feature_req.features);

	test_set_role(&conn, BT_HCI_ROLE_SLAVE);

	ull_cp_state_set(&conn, ULL_CP_CONNECTED);

	/* Initiate a Feature Exchange Procedure */

	event_prepare(&conn);
	err = ull_cp_feature_exchange(&conn);
	zassert_equal(err, BT_HCI_ERR_SUCCESS, NULL);
	event_done(&conn);

	event_prepare(&conn);

	/* Tx Queue should have one LL Control PDU */
	lt_rx(LL_SLAVE_FEATURE_REQ, &conn, &tx, &local_feature_req);
	lt_rx_q_is_empty(&conn);

	/* Rx Commented out for know, handling of UNKNOWN response will come in an update */

	lt_tx(LL_UNKNOWN_RSP, &conn, &unknown_rsp);

	event_done(&conn);

	ut_rx_pdu(LL_UNKNOWN_RSP, &ntf,  &unknown_rsp);
	ut_rx_q_is_empty();
	zassert_equal(conn.lll.event_counter, 2,
	  	      "Wrong event-count %d\n", conn.lll.event_counter);

}


void test_main(void)
{
	ztest_test_suite(feature_exchange_master,
			 ztest_unit_test_setup_teardown(test_feature_exchange_mas_loc, setup, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_feature_exchange_mas_loc_2, setup, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_feature_exchange_mas_rem, setup, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_feature_exchange_mas_rem_2, setup, unit_test_noop)
		);

	ztest_test_suite(feature_exchange_slave,
			 ztest_unit_test_setup_teardown(test_slave_feature_exchange_sla_loc, setup, unit_test_noop)
		);

	ztest_test_suite(feature_exchange_unknown,
			 ztest_unit_test_setup_teardown(test_feature_exchange_sla_loc_unknown_rsp, setup, unit_test_noop)
		);

	ztest_run_test_suite(feature_exchange_master);
	ztest_run_test_suite(feature_exchange_slave);
	ztest_run_test_suite(feature_exchange_unknown);
}
