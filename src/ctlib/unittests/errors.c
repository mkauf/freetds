/*
 * Try usage of callbacks to get errors and messages from library.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctpublic.h>
#include "common.h"

#define ALL_TESTS \
	TEST(ct_callback) \
	TEST(ct_res_info) \
	TEST(ct_send) \
	TEST(cs_config)

/* forward declare all tests */
#undef TEST
#define TEST(name) static void test_ ## name(void);
ALL_TESTS

static void
report_wrong_error(int line)
{
	fprintf(stderr, "%d:Wrong error type %d number %d (%#x)\n", line,
		ct_last_message.type, ct_last_message.number, ct_last_message.number);
	exit(1);
}

static CS_CONTEXT *ctx;
static CS_CONNECTION *conn;
static CS_COMMAND *cmd;

int
main(void)
{
	int verbose = 1;

	printf("%s: Testing message callbacks\n", __FILE__);
	if (verbose) {
		printf("Trying login\n");
	}
	check_call(try_ctlogin, (&ctx, &conn, &cmd, verbose));

	check_call(cs_config, (ctx, CS_SET, CS_MESSAGE_CB, (CS_VOID*) cslibmsg_cb, CS_UNUSED, NULL));

	/* set different callback for the connection */
	check_call(ct_callback, (NULL, conn, CS_SET, CS_CLIENTMSG_CB, (CS_VOID*) clientmsg_cb2));

	/* call all tests */
#undef TEST
#define TEST(name) test_ ## name();
	ALL_TESTS

	if (verbose) {
		printf("Trying logout\n");
	}
	check_call(try_ctlogout, (ctx, conn, cmd, verbose));

	if (verbose) {
		printf("Test succeeded\n");
	}
	return 0;
}

static void
_check_fail(const char *name, CS_RETCODE ret, int line)
{
	if (ret != CS_FAIL) {
		fprintf(stderr, "%s():%d: succeeded\n", name, line);
		exit(1);
	}
}
#define check_fail(func, args) do { \
	ct_reset_last_message(); \
	_check_fail(#func, func args, __LINE__); \
} while(0)

static void
_check_last_message(ct_message_type type, CS_INT number, const char *msg, int line)
{
	bool type_ok = true, number_ok = true, msg_ok = true;

	if (type == CTMSG_NONE && ct_last_message.type == type)
		return;

	type_ok = (ct_last_message.type == type);
	number_ok = (ct_last_message.number == number);
	if (msg && msg[0])
		msg_ok = (strstr(ct_last_message.text, msg) != NULL);
	if (!type_ok || !number_ok || !msg_ok)
		report_wrong_error(line);
}
#define check_last_message(type, number, msg) \
	_check_last_message(type, number, msg, __LINE__)

static void
test_ct_callback(void)
{
	/* this should fail, context or connection should be not NULL */
	check_fail(ct_callback, (NULL, NULL, CS_SET, CS_SERVERMSG_CB, servermsg_cb));
	check_last_message(CTMSG_NONE, 0, NULL);

	/* this should fail, context and connection cannot be both not NULL */
	check_fail(ct_callback, (ctx, conn, CS_SET, CS_SERVERMSG_CB, servermsg_cb));
	check_last_message(CTMSG_CLIENT2, 0x01010133, NULL);

	/* this should fail, invalid action */
	check_fail(ct_callback, (ctx, NULL, 3, CS_SERVERMSG_CB, servermsg_cb));
	check_last_message(CTMSG_CLIENT, 0x01010105, "action");

	/* this should fail, invalid action */
	check_fail(ct_callback, (NULL, conn, 3, CS_SERVERMSG_CB, servermsg_cb));
	check_last_message(CTMSG_CLIENT2, 0x01010105, "action");

	/* this should fail, invalid type */
	check_fail(ct_callback, (ctx, NULL, CS_SET, 20, servermsg_cb));
	check_last_message(CTMSG_CLIENT, 0x01010105, "type");

	/* this should fail, invalid type */
	check_fail(ct_callback, (NULL, conn, CS_SET, 20, servermsg_cb));
	check_last_message(CTMSG_CLIENT2, 0x01010105, "type");
}

static void
test_ct_res_info(void)
{
	CS_RETCODE ret;
	CS_INT result_type;
	CS_INT num_cols;
	CS_INT count;

	check_call(ct_command, (cmd, CS_LANG_CMD, "SELECT 'hi' AS greeting", CS_NULLTERM, CS_UNUSED));
	check_call(ct_send, (cmd));

	while ((ret = ct_results(cmd, &result_type)) == CS_SUCCEED) {
		switch (result_type) {
		case CS_CMD_SUCCEED:
		case CS_CMD_DONE:
			break;
		case CS_ROW_RESULT:
			/* this should fail, invalid number */
			check_fail(ct_res_info, (cmd, 1234, &num_cols, CS_UNUSED, NULL));
			check_last_message(CTMSG_CLIENT2, 0x01010105, "operation");

			while ((ret = ct_fetch(cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED, &count)) == CS_SUCCEED)
				continue;

			if (ret != CS_END_DATA) {
				fprintf(stderr, "ct_fetch() unexpected return %d.\n", (int) ret);
				exit(1);
			}
			break;
		default:
			fprintf(stderr, "ct_results() unexpected result_type %d.\n", (int) result_type);
			exit(1);
		}
	}
	if (ret != CS_END_RESULTS) {
		fprintf(stderr, "ct_results() unexpected return %d.\n", (int) ret);
		exit(1);
	}
}

static void
test_ct_send(void)
{
	/* reset command to idle state */
	check_call(ct_cmd_drop, (cmd));
	check_call(ct_cmd_alloc, (conn, &cmd));

	/* this should fail, invalid command state */
	check_fail(ct_send, (cmd));
	check_last_message(CTMSG_CLIENT2, 0x0101019b, "idle");
}

static void
test_cs_config(void)
{
	/* a set of invalid, not accepted values */
	static const CS_INT invalid_values[] = {
		-1,
		-5,
		-200,
		CS_WILDCARD,
		CS_NO_LIMIT,
		CS_UNUSED,
		0 /* terminator */
	};
	const CS_INT *p_invalid;
	CS_INT out_len;
	char out_buf[8];

	check_call(cs_config, (ctx, CS_SET, CS_USERDATA, (CS_VOID *)"test",  CS_NULLTERM, NULL));

	/* check that value written does not have the NUL terminator */
	strcpy(out_buf, "123456");
	check_call(cs_config, (ctx, CS_GET, CS_USERDATA, (CS_VOID *)out_buf, 8, NULL));
	if (strcmp(out_buf, "test56") != 0) {
		fprintf(stderr, "Wrong output buffer '%s'\n", out_buf);
		exit(1);
	}

	check_call(cs_config, (ctx, CS_SET, CS_USERDATA, (CS_VOID *)"test123",  4, NULL));

	/* check that value written does not have more characters */
	strcpy(out_buf, "123456");
	check_call(cs_config, (ctx, CS_GET, CS_USERDATA, (CS_VOID *)out_buf, 8, NULL));
	if (strcmp(out_buf, "test56") != 0) {
		fprintf(stderr, "Wrong output buffer '%s'\n", out_buf);
		exit(1);
	}

	for (p_invalid = invalid_values; *p_invalid != 0; ++p_invalid) {
		check_fail(cs_config, (ctx, CS_SET, CS_USERDATA, (CS_VOID *)"test", *p_invalid, NULL));
		check_last_message(CTMSG_CSLIB, 0x02010106, "buflen");
	}

	/* wrong action */
	check_fail(cs_config, (ctx, 1000, CS_USERDATA, (CS_VOID *)"test", 4, NULL));
	check_last_message(CTMSG_CSLIB, 0x02010106, "action");

	/* wrong property */
	check_fail(cs_config, (ctx, CS_SET, 100000, NULL, CS_UNUSED, NULL));
	check_last_message(CTMSG_CSLIB, 0x02010106, "property");

	/* read exactly expected bytes */
	check_call(cs_config, (ctx, CS_GET, CS_USERDATA, (CS_VOID *)out_buf, 4, NULL));

	/* wrong buflen getting value */
	out_len = -123;
	check_fail(cs_config, (ctx, CS_GET, CS_USERDATA, (CS_VOID *)out_buf, CS_NULLTERM, &out_len));
	check_last_message(CTMSG_CSLIB, 0x02010106, "buflen");
	if (out_len != -123) {
		fprintf(stderr, "Wrong buffer length returned\n");
		exit(1);
	}

	/* shorter buffer */
	out_len = -123;
	strcpy(out_buf, "123456");
	check_fail(cs_config, (ctx, CS_GET, CS_USERDATA, (CS_VOID *)out_buf, 2, &out_len));
	check_last_message(CTMSG_CSLIB, 0x02010102, " 2 bytes");
	if (out_len != 4) {
		fprintf(stderr, "Wrong buffer length returned\n");
		exit(1);
	}
	if (strcmp(out_buf, "123456") != 0) {
		fprintf(stderr, "Wrong buffer returned\n");
		exit(1);
	}
}
