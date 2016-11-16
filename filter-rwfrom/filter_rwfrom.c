/*
 * Copyright (c) 2015 Andris Raugulis <moo@arthepsy.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MY_FILTER_API_VERSION 51
#include "includes.h"

#include <sys/types.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#if MY_FILTER_API_VERSION < 51
#include "smtpd-defines.h"
#endif
#include "smtpd-api.h"
#include "log.h"

#include "match.h"

#define RWFROM_CONF "/etc/mail/filter-rwfrom.conf"

struct rwfrom_tx {
	int in_hdr;
	char *mail;
	char *rcpt;
};

struct rwfrom_rule {
	SIMPLEQ_ENTRY(rwfrom_rule) e;
	char *k, *p, *a;
};

static SIMPLEQ_HEAD(rwfrom_q, rwfrom_rule)
	rwfrom_rules = SIMPLEQ_HEAD_INITIALIZER(rwfrom_rules);

static char *
rwfrom_token(char **line)
{
	char *v;

	if (*line != NULL) {
		while (isspace((unsigned char)**line))
			(*line)++;
	}
	if ((v = strsep(line, " \t")) == NULL || strlen(v) == 0)
		return NULL;
	return xstrdup(v, "filter-rwfrom: token");
}

static int
rwfrom_parse(char *line, size_t no)
{
	struct rwfrom_rule *r;
	char *k, *p, *a;

	if ((k = rwfrom_token(&line)) == NULL)
		return 0;
	// TODO: validate key
	if ((p = rwfrom_token(&line)) == NULL) {
		log_warnx("warn: filter-rwfrom: parse: missing pattern at line %lu", no);
		return -1;
	}
	if ((a = rwfrom_token(&line)) == NULL) {
		log_warnx("warn: filter-rwfrom: parse: missing address at line %lu", no);
		return -1;
	}
	r = xcalloc(1, sizeof(struct rwfrom_rule), "filter-rwfrom: parse");
	r->k = k, r->p = p, r->a = a;
	SIMPLEQ_INSERT_TAIL(&rwfrom_rules, r, e);
	return 0;
}

static int
rwfrom_conf(const char *c)
{
	FILE *f;
	char *line = NULL;
	size_t sz = 0, no = 0;
	ssize_t len;

	if ((f = fopen(c, "r")) == NULL) {
		log_warn("warn: filter-rwfrom: conf: fopen %s", c);
		return -1;
	}
	while ((len = getline(&line, &sz, f)) != -1) {
		if (rwfrom_parse(line, ++no) == -1) {
			free(line);
			fclose(f);
			return -1;
		}
		free(line);
	}
	if (ferror(f)) {
		log_warn("warn: filter-rwfrom: conf: getline");
		free(line);
		fclose(f);
		return -1;
	}
	free(line);
	fclose(f);
	return 0;
}

static int
rwfrom_rewrite(uint64_t id, struct rwfrom_tx *rw, const char *hdr)
{
	char buf[SMTPD_MAXLINESIZE];
	struct rwfrom_rule *r;
	int matched;

	SIMPLEQ_FOREACH(r, &rwfrom_rules, e) {
		matched = 0;
		if (strncmp(r->k, "mail", 4) == 0 && rw->mail != NULL &&
		    match_pattern(rw->mail, r->p))
		    matched = 1;
		else if (strncmp(r->k, "rcpt", 4) == 0 && rw->rcpt != NULL &&
		    match_pattern(rw->rcpt, r->p))
		    matched = 1;
		if (matched) {
			buf[0] = '\0';
			(void)strlcat(buf, hdr, sizeof buf);
			(void)strlcat(buf, " ", sizeof buf);
			(void)strlcat(buf, r->a, sizeof buf);
			filter_api_writeln(id, buf);
			return 0;
		}
	}
	return -1;
}

#if MY_FILTER_API_VERSION > 50
static void *
rwfrom_tx_alloc(uint64_t id)
{
	struct rwfrom_tx	*tx;

	tx = xcalloc(1, sizeof (struct rwfrom_tx), "rwfrom_tx_alloc");
	tx->in_hdr=1;

	return tx;
}

static void
rwfrom_tx_free(void *ctx)
{
	struct rwfrom_tx	*tx = ctx;

	free(tx->mail);
	free(tx->rcpt);
	free(tx);
}
#else
static struct rwfrom_tx *
rwfrom_init_udata(uint64_t id)
{
	struct rwfrom_tx *tx;

	if ((tx = filter_api_get_udata(id)) == NULL) {
		tx = xmalloc(sizeof *tx, "rewrite_from: on_hello");
		tx->in_hdr = 1;
		tx->mail = NULL;
		tx->rcpt = NULL;
		filter_api_set_udata(id, tx);
	}

	return tx;
}

static void
rwfrom_free_udata(uint64_t id)
{
	struct rwfrom_tx	*tx;
	
	if ((tx = filter_api_get_udata(id)) != NULL) {
		free(tx->mail);
		free(tx->rcpt);
		free(tx);
	}
	filter_api_set_udata(id, NULL);
}
#endif

static int
rwfrom_on_mail(uint64_t id, struct mailaddr *mail)
{
	struct rwfrom_tx	*tx;

#if MY_FILTER_API_VERSION > 50
	tx = filter_api_transaction(id);
#else
	tx = rwfrom_init_udata(id);
#endif
	tx->mail = xstrdup(filter_api_mailaddr_to_text(mail), 
	    "filter-rwfrom: on_rcpt");
	
	return filter_api_accept(id);
}

static int
rwfrom_on_rcpt(uint64_t id, struct mailaddr *rcpt)
{
	struct rwfrom_tx	*tx;

#if MY_FILTER_API_VERSION > 50
	tx = filter_api_transaction(id);
#else
	tx = rwfrom_init_udata(id);
#endif
	tx->rcpt = xstrdup(filter_api_mailaddr_to_text(rcpt), 
	    "filter-rwfrom: on_rcpt");
	return filter_api_accept(id);
}

static void
#if MY_FILTER_API_VERSION > 50
rwfrom_on_msg_line(uint64_t id, const char *line)
#else
rwfrom_on_dataline(uint64_t id, const char *line)
#endif
{
	static const char *hdr_from = "From:";
	struct rwfrom_tx	*tx;

#if MY_FILTER_API_VERSION > 50
	tx = filter_api_transaction(id);
#else
	tx = rwfrom_init_udata(id);
#endif
	if (tx->in_hdr) {
		if (strlen(line) == 0) {
			tx->in_hdr = 0;
		}
		else if (strncasecmp(hdr_from, line, strlen(hdr_from)) == 0) {
			if (rwfrom_rewrite(id, tx, hdr_from) == 0)
				return;
		}
	}
	filter_api_writeln(id, line);
}

static int
#if MY_FILTER_API_VERSION > 50
rwfrom_on_msg_end(uint64_t id, size_t size)
#else
rwfrom_on_eom(uint64_t id, size_t size)
#endif
{
	return filter_api_accept(id);
}

#if MY_FILTER_API_VERSION < 51
static void
rwfrom_on_commit(uint64_t id)
{
	rwfrom_free_udata(id);
}

static void
rwfrom_on_rollback(uint64_t id)
{
	rwfrom_free_udata(id);
}
#endif


int
main(int argc, char **argv)
{
	int	ch;

	log_init(-1);

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			log_warnx("warn: filter-rwfrom: bad option");
			return (1);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		fatalx("filter-rwfrom: bogus argument(s)");
	
	log_debug("debug: filter-rwfrom: starting...");

	if (rwfrom_conf((argc == 1) ? argv[0] : RWFROM_CONF) == -1)
		fatalx("filter-rwfrom: configuration failed");

	filter_api_on_mail(rwfrom_on_mail);
	filter_api_on_rcpt(rwfrom_on_rcpt);
#if MY_FILTER_API_VERSION > 50
	filter_api_on_msg_line(rwfrom_on_msg_line);
	filter_api_on_msg_end(rwfrom_on_msg_end);
	filter_api_transaction_allocator(rwfrom_tx_alloc);
	filter_api_transaction_destructor(rwfrom_tx_free);
#else
	filter_api_on_dataline(rwfrom_on_dataline);
	filter_api_on_eom(rwfrom_on_eom);
	filter_api_on_commit(rwfrom_on_commit);
	filter_api_on_rollback(rwfrom_on_rollback);
#endif
	filter_api_loop();

	log_debug("debug: filter-rwfrom: exiting");

	return (0);
}
