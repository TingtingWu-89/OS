/**
 * @brief gunzip - decompress gzip-compressed payloads
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <toaru/inflate.h>

static int to_stdout = 0;
static int keep = 0;
static int progress = 0;
static off_t total_length = 0;
static size_t count = 0;

static uint8_t _get(struct inflate_context * ctx) {
	return fgetc(ctx->input_priv);
}

static uint8_t _get_progress(struct inflate_context * ctx) {
	count++;
	if (!(count & 0x3FF)) {
		fprintf(stderr, "\r%zu KiB / %zu KiB (%zu%%)", count / 1024, total_length / 1024, 100 * count / total_length);
		fflush(stderr);
	}
	return fgetc(ctx->input_priv);
}

static void _write(struct inflate_context * ctx, unsigned int sym) {
	fputc(sym, ctx->output_priv);
}

static int usage(int argc, char * argv[]) {
	fprintf(stderr,
			"gunzip - decompress gzip-compressed payloads\n"
			"\n"
			"usage: %s [-c] name...\n"
			"\n"
			" -c     \033[3mwrite to stdout; implies -k\033[0m\n"
			" -k     \033[3mkeep original files unchanged\033[0m\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {

	int opt;
	while ((opt = getopt(argc, argv, "?ck")) != -1) {
		switch (opt) {
			case 'c':
				to_stdout = 1;
				keep = 1;
				break;
			case 'k':
				keep = 1;
				break;
			case '?':
				return usage(argc, argv);
			default:
				fprintf(stderr, "%s: unrecognized option '%c'\n", argv[0], opt);
				break;
		}
	}

	FILE * f;
	if (optind >= argc || !strcmp(argv[optind],"-")) {
		f = stdin;
	} else {
		f =fopen(argv[optind],"r");
	}

	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
		return 1;
	}

	if (!to_stdout && strcmp(&argv[optind][strlen(argv[optind])-3],".gz")) {
		fprintf(stderr, "%s: %s: Don't know how to interpret file name\n", argv[0], argv[optind]);
		return 1;
	}

	char * maybeProgress = getenv("GUNZIP_PROGRESS");

	if (maybeProgress && !strcmp(maybeProgress, "1")) {
		progress = 1;
		fseek(f, 0, SEEK_END);
		total_length = ftell(f);
		fseek(f, 0, SEEK_SET);
	}

	struct inflate_context ctx;
	ctx.input_priv = f;
	if (to_stdout) {
		ctx.output_priv = stdout;
	} else {
		char * tmp = strdup(argv[optind]);
		tmp[strlen(argv[optind])-3] = '\0';
		ctx.output_priv = fopen(tmp,"w");
		free(tmp);
	}
	ctx.get_input = progress ? _get_progress : _get;
	ctx.write_output = _write;
	ctx.ring = NULL; /* Use the global one */

	if (gzip_decompress(&ctx)) {
		return 1;
	}

	fflush(ctx.output_priv);

	if (!to_stdout) {
		fclose(ctx.output_priv);
	}

	if (!keep) {
		unlink(argv[optind]);
	}

	return 0;
}

