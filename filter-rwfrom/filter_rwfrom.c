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

#include "includes.h"

#include <sys/types.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <util.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include "smtpd-defines.h"
#include "smtpd-api.h"
#include "log.h"

#include "match.h"

#define RWFROM_CONF "/etc/mail/filter-rwfrom.conf"

struct rwfrom {
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
	char *line;
	size_t no = 0;

	if ((f = fopen(c, "r")) == NULL) {
		log_warn("warn: filter-rwfrom: conf: fopen %s", c);
		return -1;
	}
	while ((line = fparseln(f, NULL, &no, NULL, 0)) != NULL) {
		if (rwfrom_parse(line, no) == -1) {
			free(line);
			fclose(f);
			return -1;
		}
		free(line);
	}
	fclose(f);
	return 0;
}

static int
rwfrom_rewrite(uint64_t id, struct rwfrom *rw, const char *hdr)
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

static void
free_rwfrom(uint64_t id)
{
	struct rwfrom *rw;

	if ((rw = filter_api_get_udata(id)) != NULL) {
		free(rw->mail);
		free(rw->rcpt);
		free(rw);
		filter_api_set_udata(id, NULL);
	}
}

static struct rwfrom *
init_rwfrom(uint64_t id)
{
	struct rwfrom *rw;

	if ((rw = filter_api_get_udata(id)) == NULL) {
		rw = xmalloc(sizeof *rw, "rewrite_from: on_hello");
		filter_api_set_udata(id, rw);
		rw->in_hdr = 1;
		rw->mail = NULL;
		rw->rcpt = NULL;
	}
	return rw;
}

static int
on_mail(uint64_t id, struct mailaddr *mail)
{
	struct rwfrom *rw;
	rw = init_rwfrom(id);
	rw->mail = xstrdup(filter_api_mailaddr_to_text(mail), 
	    "filter-rwfrom: on_rcpt");
	
	return filter_api_accept(id);
}

static int
on_rcpt(uint64_t id, struct mailaddr *rcpt)
{
	struct rwfrom *rw;

	rw = init_rwfrom(id);
	rw->rcpt = xstrdup(filter_api_mailaddr_to_text(rcpt), 
	    "filter-rwfrom: on_rcpt");
	return filter_api_accept(id);
}

static void
on_dataline(uint64_t id, const char *line)
{
	static const char *hdr_from = "From:";
	struct rwfrom *rw;

	rw = init_rwfrom(id);
	if (rw->in_hdr) {
		if (strlen(line) == 0) {
			rw->in_hdr = 0;
		}
		else if (strncasecmp(hdr_from, line, strlen(hdr_from)) == 0) {
			if (rwfrom_rewrite(id, rw, hdr_from) == 0)
				return;
		}
	}
	filter_api_writeln(id, line);
}

static int
on_eom(uint64_t id, size_t size)
{
	free_rwfrom(id);
	return filter_api_accept(id);
}

static void
on_reset(uint64_t id)
{
	free_rwfrom(id);
}

static void
on_rollback(uint64_t id)
{
	free_rwfrom(id);
}

static void
on_disconnect(uint64_t id)
{
	free_rwfrom(id);
}


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

	filter_api_on_mail(on_mail);
	filter_api_on_rcpt(on_rcpt);
	filter_api_on_dataline(on_dataline);
	filter_api_on_eom(on_eom);
	filter_api_on_disconnect(on_disconnect);
	filter_api_on_reset(on_reset);
	filter_api_on_rollback(on_rollback);
	filter_api_loop();

	log_debug("debug: filter-rwfrom: exiting");

	return (0);
}
