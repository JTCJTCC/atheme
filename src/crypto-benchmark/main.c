/*
 * SPDX-License-Identifier: ISC
 * SPDX-URL: https://spdx.org/licenses/ISC.html
 *
 * Copyright (C) 2019 Aaron M. D. Jones <aaronmdjones@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <atheme/argon2.h>          // ATHEME_ARGON2_*
#include <atheme/digest.h>          // DIGALG_*
#include <atheme/memory.h>          // sreallocarray()
#include <atheme/pbkdf2.h>          // PBKDF2_*
#include <atheme/scrypt.h>          // ATHEME_SCRYPT_*
#include <atheme/stdheaders.h>      // (everything else)
#include <atheme/sysconf.h>         // HAVE_*
#include <atheme/tools.h>           // string_to_uint()

#include <ext/getopt_long.h>        // mowgli_getopt_option_t, mowgli_getopt_long()

#include "benchmark.h"              // (everything else)
#include "optimal.h"                // do_optimal_benchmarks()

#define BENCH_ARRAY_SIZE(x)         ((sizeof((x))) / (sizeof((x)[0])))

#define BENCH_CLOCKTIME_MIN         0.10L
#define BENCH_CLOCKTIME_DEF         0.25L
#define BENCH_CLOCKTIME_MAX         1.00L

#define BENCH_MEMLIMIT_MIN          BENCH_MAX(ATHEME_ARGON2_MEMCOST_MIN, ATHEME_SCRYPT_MEMLIMIT_MIN)
#define BENCH_MEMLIMIT_DEF          BENCH_MAX(ATHEME_ARGON2_MEMCOST_DEF, ATHEME_SCRYPT_MEMLIMIT_DEF)
#define BENCH_MEMLIMIT_MAX          BENCH_MIN(ATHEME_ARGON2_MEMCOST_MAX, ATHEME_SCRYPT_MEMLIMIT_MAX)

#ifdef HAVE_LIBARGON2

static argon2_type b_argon2_types_default[] = { Argon2_id };
static argon2_type *b_argon2_types = NULL;
static size_t b_argon2_types_count = 0;

static size_t b_argon2_memcosts_default[] = { ATHEME_ARGON2_MEMCOST_MIN, ATHEME_ARGON2_MEMCOST_DEF };
static size_t *b_argon2_memcosts = NULL;
static size_t b_argon2_memcosts_count = 0;

static size_t b_argon2_timecosts_default[] = { ATHEME_ARGON2_TIMECOST_MIN, ATHEME_ARGON2_TIMECOST_DEF };
static size_t *b_argon2_timecosts = NULL;
static size_t b_argon2_timecosts_count = 0;

static size_t b_argon2_threads_default[] = { ATHEME_ARGON2_THREADS_DEF };
static size_t *b_argon2_threads = NULL;
static size_t b_argon2_threads_count = 0;

#endif /* HAVE_LIBARGON2 */

#ifdef HAVE_LIBSODIUM_SCRYPT

static size_t b_scrypt_memlimits_default[] = { ATHEME_SCRYPT_MEMLIMIT_DEF };
static size_t *b_scrypt_memlimits = NULL;
static size_t b_scrypt_memlimits_count = 0;

static size_t b_scrypt_opslimits_default[] = { ATHEME_SCRYPT_OPSLIMIT_DEF };
static size_t *b_scrypt_opslimits = NULL;
static size_t b_scrypt_opslimits_count = 0;

#endif /* HAVE_LIBSODIUM_SCRYPT */

static size_t b_pbkdf2_itercounts_default[] = { PBKDF2_ITERCNT_MIN, PBKDF2_ITERCNT_DEF, PBKDF2_ITERCNT_MAX };
static size_t *b_pbkdf2_itercounts = NULL;
static size_t b_pbkdf2_itercounts_count = 0;

static enum digest_algorithm b_pbkdf2_digests_default[] = { DIGALG_SHA1, DIGALG_SHA2_256, DIGALG_SHA2_512 };
static enum digest_algorithm *b_pbkdf2_digests = NULL;
static size_t b_pbkdf2_digests_count = 0;

static long double optimal_clocklimit = BENCH_CLOCKTIME_DEF;
static unsigned int optimal_memlimit = BENCH_MEMLIMIT_DEF;
static bool optimal_memlimit_given = false;

static unsigned int run_options = BENCH_RUN_OPTIONS_NONE;

static const mowgli_getopt_option_t bench_long_opts[] = {

	{                     "help",       no_argument, NULL, 'h', 0 },
	{                  "version",       no_argument, NULL, 'v', 0 },

	{   "run-optimal-benchmarks",       no_argument, NULL, 'o', 0 },
	{      "optimal-clock-limit", required_argument, NULL, 'g', 0 },
#ifdef HAVE_ANY_MEMORY_HARD_ALGORITHM
	{     "optimal-memory-limit", required_argument, NULL, 'l', 0 },
#endif
#ifdef HAVE_LIBARGON2
	{    "run-argon2-benchmarks",       no_argument, NULL, 'a', 0 },
	{             "argon2-types", required_argument, NULL, 'n', 0 },
	{      "argon2-memory-costs", required_argument, NULL, 'm', 0 },
	{        "argon2-time-costs", required_argument, NULL, 't', 0 },
	{           "argon2-threads", required_argument, NULL, 'p', 0 },
#endif
#ifdef HAVE_LIBSODIUM_SCRYPT
	{    "run-scrypt-benchmarks",       no_argument, NULL, 's', 0 },
	{         "scrypt-memlimits", required_argument, NULL, 'e', 0 },
	{         "scrypt-opslimits", required_argument, NULL, 'f', 0 },
#endif
	{    "run-pbkdf2-benchmarks",       no_argument, NULL, 'k', 0 },
	{        "pbkdf2-iterations", required_argument, NULL, 'c', 0 },
	{ "pbkdf2-digest-algorithms", required_argument, NULL, 'd', 0 },

	{ NULL, 0, NULL, 0, 0 },
};

static inline void
print_version(void)
{
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "%s (Cryptographic Benchmarking Utility)\n", PACKAGE_STRING);
	(void) fprintf(stderr, "Using digest frontend: %s\n", digest_get_frontend_info());
}

static inline void
print_usage(void)
{
	(void) fprintf(stderr, "\n"
	"  Usage: " PACKAGE_TARNAME "-crypto-benchmark -h\n"
	"         " PACKAGE_TARNAME "-crypto-benchmark -v\n"
	"         " PACKAGE_TARNAME "-crypto-benchmark -o [-g ...] [-l ...]\n"
#ifdef HAVE_LIBARGON2
	"         " PACKAGE_TARNAME "-crypto-benchmark -a [-n ...] [-m ...] [-t ...] [-p ...]\n"
#endif
#ifdef HAVE_LIBSODIUM_SCRYPT
	"         " PACKAGE_TARNAME "-crypto-benchmark -s [-e ...] [-f ...]\n"
#endif
	"         " PACKAGE_TARNAME "-crypto-benchmark -k [-c ...] [-d ...]\n"
	"\n"
	"  -h/--help                    Display this help information and exit\n"
	"  -v/--version                 Display program version and exit\n"
	"\n"
	"  -o/--run-optimal-benchmarks  Perform an automatic parameter tuning benchmark:\n"
	"  -g/--optimal-clock-limit       Wall clock time limit for optimal benchmarks\n"
	"                                   (in seconds, fractional values accepted)\n"
#ifdef HAVE_ANY_MEMORY_HARD_ALGORITHM
	"  -l/--optimal-memory-limit      Memory limit for optimal benchmarking\n"
	"                                   (as a power of 2, in KiB)\n"
	"                                   For example, '-l 16' means 2^16 KiB; 64 MiB\n"
#else
	"  -l/--optimal-memory-limit      Unsupported\n"
#endif
	"\n"
	"    If one of the above limits are not given, defaults are used.\n"
#ifdef HAVE_LIBARGON2
	"\n"
	"  -a/--run-argon2-benchmarks   Benchmark the Argon2 code with configurations:\n"
	"  -n/--argon2-types              Comma-separated types\n"
	"  -m/--argon2-memory-costs       Comma-separated memory costs\n"
	"  -t/--argon2-time-costs         Comma-separated time costs\n"
	"  -p/--argon2-threads            Comma-separated thread counts\n"
	"\n"
	"    Valid types are: Argon2d, Argon2i, Argon2id (case-insensitive)\n"
#endif
#ifdef HAVE_LIBSODIUM_SCRYPT
	"\n"
	"  -s/--run-scrypt-benchmarks   Benchmark the scrypt code with configurations:\n"
	"  -e/--scrypt-memlimits          Comma-separated memlimits\n"
	"  -f/--scrypt-opslimits          Comma-separated opslimits\n"
#endif
	"\n"
	"  -k/--run-pbkdf2-benchmarks   Benchmark the PBKDF2 code with configurations:\n"
	"  -c/--pbkdf2-iterations         Comma-separated iteration counts\n"
	"  -d/--pbkdf2-digests            Comma-separated digest algorithms\n"
	"\n"
	"    Valid digests are: MD5, SHA1, SHA2-256, SHA2-512 (case-insensitive)\n"
	"\n"
	"    If one of the comma-separated options are not given, defaults are used.\n"
	"\n");
}

static inline bool
process_uint_option(const int sw, const char *const restrict val, size_t **const restrict arr,
                    size_t *const restrict arr_len, const unsigned int val_min, const unsigned int val_max)
{
	char *opt;
	char *tok;

	if (! (opt = strdup(val)))
	{
		(void) perror("strdup(3)");
		return false;
	}
	while ((tok = strsep(&opt, ",")) != NULL)
	{
		unsigned int b_value = 0;

		if (! string_to_uint(tok, &b_value) || b_value < val_min || b_value > val_max)
		{
			(void) fprintf(stderr, "'%s' is not a valid value for integer option '%c'\n", tok, sw);
			(void) fprintf(stderr, "range of valid values: %u to %u (inclusive)\n", val_min, val_max);
			return false;
		}
		if (! (*arr = sreallocarray(*arr, (*arr_len) + 1, sizeof **arr)))
		{
			(void) perror("sreallocarray()");
			return false;
		}

		(*arr)[(*arr_len)++] = b_value;
	}

	(void) free(opt);
	return true;
}

static bool
process_options(int argc, char *argv[])
{
	char bench_short_opts[BUFSIZE];

	char *ptr = bench_short_opts;
	char *opt;
	char *tok;
	int c;

	(void) memset(bench_short_opts, 0x00, sizeof bench_short_opts);

	for (size_t x = 0; bench_long_opts[x].name != NULL; x++)
	{
		*ptr++ = bench_long_opts[x].val;

		if (bench_long_opts[x].has_arg == no_argument)
			continue;

		*ptr++ = ':';

		if (bench_long_opts[x].has_arg == optional_argument)
			*ptr++ = ':';
	}

	while ((c = mowgli_getopt_long(argc, argv, bench_short_opts, bench_long_opts, NULL)) != -1)
	{
		switch (c)
		{
			case 'h':
				(void) print_usage();
				exit(EXIT_SUCCESS);

			case 'v':
				// Version string was already printed at program startup
				exit(EXIT_SUCCESS);

			case 'o':
				run_options |= BENCH_RUN_OPTIONS_OPTIMAL;
				break;

			case 'g':
			{
				errno = 0;

				char *end = NULL;
				const long double ret = strtold(mowgli_optarg, &end);

				if (! ret || (end && *end) || errno != 0)
				{
					(void) fprintf(stderr, "'%s' is not a valid value for decimal option '%c'\n",
					                       mowgli_optarg, c);
					return false;
				}
				if (ret < BENCH_CLOCKTIME_MIN || ret > BENCH_CLOCKTIME_MAX)
				{
					(void) fprintf(stderr, "'%s' is not a valid value for decimal option '%c'\n",
					                       mowgli_optarg, c);
					(void) fprintf(stderr, "range of valid values: %LF to %LF (inclusive)\n",
					                       BENCH_CLOCKTIME_MIN, BENCH_CLOCKTIME_MAX);
					return false;
				}

				optimal_clocklimit = ret;
				break;
			}

#ifdef HAVE_ANY_MEMORY_HARD_ALGORITHM
			case 'l':
				if (! string_to_uint(mowgli_optarg, &optimal_memlimit))
				{
					(void) fprintf(stderr, "'%s' is not a valid value for integer option '%c'\n",
					                       mowgli_optarg, c);
					(void) fprintf(stderr, "range of valid values: %u to %u (inclusive)\n",
					                       BENCH_MEMLIMIT_MIN, BENCH_MEMLIMIT_MAX);
					return false;
				}
				if (optimal_memlimit < BENCH_MEMLIMIT_MIN || optimal_memlimit > BENCH_MEMLIMIT_MAX)
				{
					(void) fprintf(stderr, "'%u' is not a valid value for integer option '%c'\n",
					                       optimal_memlimit, c);
					(void) fprintf(stderr, "range of valid values: %u to %u (inclusive)\n",
					                       BENCH_MEMLIMIT_MIN, BENCH_MEMLIMIT_MAX);
					return false;
				}
				optimal_memlimit_given = true;
				break;
#endif /* HAVE_ANY_MEMORY_HARD_ALGORITHM */

#ifdef HAVE_LIBARGON2
			case 'a':
				run_options |= BENCH_RUN_OPTIONS_ARGON2;
				break;

			case 'n':
			{
				if (! (opt = strdup(mowgli_optarg)))
				{
					(void) perror("strdup(3)");
					return false;
				}
				while ((tok = strsep(&opt, ",")) != NULL)
				{
					const argon2_type b_argon2_type = argon2_name_to_type(tok);

					if (! (b_argon2_types = sreallocarray(b_argon2_types,
					                                      b_argon2_types_count + 1,
					                                      sizeof b_argon2_type)))
					{
						(void) perror("sreallocarray()");
						return false;
					}

					b_argon2_types[b_argon2_types_count++] = b_argon2_type;
				}

				(void) free(opt);
				break;
			}

			case 'm':
				if (! process_uint_option(c, mowgli_optarg, &b_argon2_memcosts,
				                          &b_argon2_memcosts_count, ATHEME_ARGON2_MEMCOST_MIN,
				                          ATHEME_ARGON2_MEMCOST_MAX))
					// This function logs error messages on failure
					return false;

				break;

			case 't':
				if (! process_uint_option(c, mowgli_optarg, &b_argon2_timecosts,
				                          &b_argon2_timecosts_count, ATHEME_ARGON2_TIMECOST_MIN,
				                          ATHEME_ARGON2_TIMECOST_MAX))
					// This function logs error messages on failure
					return false;

				break;

			case 'p':
				if (! process_uint_option(c, mowgli_optarg, &b_argon2_threads,
				                          &b_argon2_threads_count, ATHEME_ARGON2_THREADS_MIN,
				                          ATHEME_ARGON2_THREADS_MAX))
					// This function logs error messages on failure
					return false;

				break;
#endif /* HAVE_LIBARGON2 */

#ifdef HAVE_LIBSODIUM_SCRYPT
			case 's':
				run_options |= BENCH_RUN_OPTIONS_SCRYPT;
				break;

			case 'e':
				if (! process_uint_option(c, mowgli_optarg, &b_scrypt_memlimits,
				                          &b_scrypt_memlimits_count, ATHEME_SCRYPT_MEMLIMIT_MIN,
				                          ATHEME_SCRYPT_MEMLIMIT_MAX))
					// This function logs error messages on failure
					return false;

				break;

			case 'f':
				if (! process_uint_option(c, mowgli_optarg, &b_scrypt_opslimits,
				                          &b_scrypt_opslimits_count, ATHEME_SCRYPT_OPSLIMIT_MIN,
				                          ATHEME_SCRYPT_OPSLIMIT_MAX))
					// This function logs error messages on failure
					return false;

				break;
#endif /* HAVE_LIBSODIUM_SCRYPT */

			case 'k':
				run_options |= BENCH_RUN_OPTIONS_PBKDF2;
				break;

			case 'c':
				if (! process_uint_option(c, mowgli_optarg, &b_pbkdf2_itercounts,
				                          &b_pbkdf2_itercounts_count, PBKDF2_ITERCNT_MIN,
				                          PBKDF2_ITERCNT_MAX))
					// This function logs error messages on failure
					return false;

				break;

			case 'd':
			{
				if (! (opt = strdup(mowgli_optarg)))
				{
					(void) perror("strdup(3)");
					return false;
				}
				while ((tok = strsep(&opt, ",")) != NULL)
				{
					const enum digest_algorithm b_pbkdf2_digest = md_name_to_digest(tok);

					if (! (b_pbkdf2_digests = sreallocarray(b_pbkdf2_digests,
					                                        b_pbkdf2_digests_count + 1,
					                                        sizeof b_pbkdf2_digest)))
					{
						(void) perror("sreallocarray()");
						return false;
					}

					b_pbkdf2_digests[b_pbkdf2_digests_count++] = b_pbkdf2_digest;
				}

				(void) free(opt);
				break;
			}

			default:
				return false;
		}
	}

	if (! run_options || (run_options & (run_options - 1U)))
	{
		(void) print_usage();
		(void) fprintf(stderr, "Error: Conflicting options (or no options) given.\n");
		(void) fprintf(stderr, "\n");
		return false;
	}

#ifdef HAVE_LIBARGON2
	if (! b_argon2_types)
	{
		b_argon2_types = b_argon2_types_default;
		b_argon2_types_count = BENCH_ARRAY_SIZE(b_argon2_types_default);
	}
	if (! b_argon2_memcosts)
	{
		b_argon2_memcosts = b_argon2_memcosts_default;
		b_argon2_memcosts_count = BENCH_ARRAY_SIZE(b_argon2_memcosts_default);
	}
	if (! b_argon2_timecosts)
	{
		b_argon2_timecosts = b_argon2_timecosts_default;
		b_argon2_timecosts_count = BENCH_ARRAY_SIZE(b_argon2_timecosts_default);
	}
	if (! b_argon2_threads)
	{
		b_argon2_threads = b_argon2_threads_default;
		b_argon2_threads_count = BENCH_ARRAY_SIZE(b_argon2_threads_default);
	}
#endif /* HAVE_LIBARGON2 */

#ifdef HAVE_LIBSODIUM_SCRYPT
	if (! b_scrypt_memlimits)
	{
		b_scrypt_memlimits = b_scrypt_memlimits_default;
		b_scrypt_memlimits_count = BENCH_ARRAY_SIZE(b_scrypt_memlimits_default);
	}
	if (! b_scrypt_opslimits)
	{
		b_scrypt_opslimits = b_scrypt_opslimits_default;
		b_scrypt_opslimits_count = BENCH_ARRAY_SIZE(b_scrypt_opslimits_default);
	}
#endif /* HAVE_LIBSODIUM_SCRYPT */

	if (! b_pbkdf2_itercounts)
	{
		b_pbkdf2_itercounts = b_pbkdf2_itercounts_default;
		b_pbkdf2_itercounts_count = BENCH_ARRAY_SIZE(b_pbkdf2_itercounts_default);
	}
	if (! b_pbkdf2_digests)
	{
		b_pbkdf2_digests = b_pbkdf2_digests_default;
		b_pbkdf2_digests_count = BENCH_ARRAY_SIZE(b_pbkdf2_digests_default);
	}

	return true;
}

#ifdef HAVE_LIBARGON2

static bool ATHEME_FATTR_WUR
do_argon2_benchmarks(void)
{
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "Beginning Argon2 benchmark ...\n");
	(void) fprintf(stderr, "\n");

	(void) argon2_print_colheaders();

	for (size_t b_argon2_type = 0; b_argon2_type < b_argon2_types_count; b_argon2_type++)
	  for (size_t b_argon2_memcost = 0; b_argon2_memcost < b_argon2_memcosts_count; b_argon2_memcost++)
	    for (size_t b_argon2_timecost = 0; b_argon2_timecost < b_argon2_timecosts_count; b_argon2_timecost++)
	      for (size_t b_argon2_thread = 0; b_argon2_thread < b_argon2_threads_count; b_argon2_thread++)
	        if (! benchmark_argon2(b_argon2_types[b_argon2_type], b_argon2_memcosts[b_argon2_memcost],
	                               b_argon2_timecosts[b_argon2_timecost], b_argon2_threads[b_argon2_thread], NULL))
	          // This function logs error messages on failure
	          return false;

	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");
	return true;
}

#endif /* HAVE_LIBARGON2 */

#ifdef HAVE_LIBSODIUM_SCRYPT

static bool ATHEME_FATTR_WUR
do_scrypt_benchmarks(void)
{
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "Beginning scrypt benchmark ...\n");
	(void) fprintf(stderr, "\n");

	(void) scrypt_print_colheaders();

	for (size_t b_scrypt_memlimit = 0; b_scrypt_memlimit < b_scrypt_memlimits_count; b_scrypt_memlimit++)
	  for (size_t b_scrypt_opslimit = 0; b_scrypt_opslimit < b_scrypt_opslimits_count; b_scrypt_opslimit++)
	    if (! benchmark_scrypt(b_scrypt_memlimits[b_scrypt_memlimit], b_scrypt_opslimits[b_scrypt_opslimit], NULL))
	      // This function logs error messages on failure
	      return false;

	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");
	return true;
}

#endif /* HAVE_LIBSODIUM_SCRYPT */

static bool ATHEME_FATTR_WUR
do_pbkdf2_benchmarks(void)
{
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "Beginning PBKDF2 benchmark ...\n");
	(void) fprintf(stderr, "\n");

	(void) pbkdf2_print_colheaders();

	for (size_t b_pbkdf2_digest = 0; b_pbkdf2_digest < b_pbkdf2_digests_count; b_pbkdf2_digest++)
	  for (size_t b_pbkdf2_itercount = 0; b_pbkdf2_itercount < b_pbkdf2_itercounts_count; b_pbkdf2_itercount++)
	    if (! benchmark_pbkdf2(b_pbkdf2_digests[b_pbkdf2_digest], b_pbkdf2_itercounts[b_pbkdf2_itercount], NULL))
	      // This function logs error messages on failure
	      return false;

	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");
	return true;
}

int
main(int argc, char *argv[])
{
	(void) print_version();

	if (! benchmark_init())
		// This function logs error messages on failure
		return EXIT_FAILURE;

	if (! process_options(argc, argv))
		// This function logs error messages on failure
		return EXIT_FAILURE;

#if (ATHEME_API_DIGEST_FRONTEND == ATHEME_API_DIGEST_FRONTEND_INTERNAL) && !defined(IN_CI_BUILD_ENVIRONMENT)
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "NOTE: This program may perform significantly better if you build it\n");
	(void) fprintf(stderr, "      against a supported third-party cryptographic digest library!\n");
#endif

	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");
	(void) fprintf(stderr, "\n");

	if ((run_options & BENCH_RUN_OPTIONS_OPTIMAL) &&
	    ! do_optimal_benchmarks(optimal_clocklimit, optimal_memlimit, optimal_memlimit_given))
		// This function logs error messages on failure
		return EXIT_FAILURE;

#ifdef HAVE_LIBARGON2
	if ((run_options & BENCH_RUN_OPTIONS_ARGON2) && ! do_argon2_benchmarks())
		// This function logs error messages on failure
		return EXIT_FAILURE;
#endif /* HAVE_LIBARGON2 */

#ifdef HAVE_LIBSODIUM_SCRYPT
	if ((run_options & BENCH_RUN_OPTIONS_SCRYPT) && ! do_scrypt_benchmarks())
		// This function logs error messages on failure
		return EXIT_FAILURE;
#endif /* HAVE_LIBSODIUM_SCRYPT */

	if ((run_options & BENCH_RUN_OPTIONS_PBKDF2) && ! do_pbkdf2_benchmarks())
		// This function logs error messages on failure
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}