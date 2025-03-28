#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include "ffsb_stats.h"
#include "util.h"

char *syscall_names[] = {
	"open",
	"read",
	"write",
	"create",
	"lseek",
	"unlink",
	"close",
	"stat",
	"regex",
};

/* yuck, just for the parser anyway.. */
int ffsb_stats_str2syscall(char *str, syscall_t *sys)
{
	int i;
	int ret;
	for (i = 0; i < FFSB_NUM_SYSCALLS; i++) {
		ret = strncasecmp(syscall_names[i], str,
				  strlen(syscall_names[i]));
		/* printf("%s = syscall_names[%d] vs %str ret = %d\n",
		 * syscall_names[i],i,str,ret);
		 */
		if (0 == ret) {
			*sys = (syscall_t)i; /* ewww */
			/* printf("matched syscall %s\n",syscall_names[i]); */
			return 1;
		}
	}
	printf("warning: failed to get match for syscall %s\n", str);
	return 0;
}

void  ffsb_statsc_init(ffsb_statsc_t *fsc)
{
	fsc->num_buckets = 0;
	fsc->buckets = NULL;
	fsc->ignore_stats = 0;
}

void ffsb_statsc_addbucket(ffsb_statsc_t *fsc, uint32_t min, uint32_t max)
{
	struct stat_bucket *temp;
	fsc->num_buckets++;

	/* printf("ffsb_realloc(): fsc_buckets = %d\n",fsc->num_buckets); */
	temp = ffsb_realloc(fsc->buckets, sizeof(struct stat_bucket) *
			    fsc->num_buckets);

	fsc->buckets = temp;

	/* Convert to micro-secs from milli-secs */
	fsc->buckets[fsc->num_buckets-1].min = min ;
	fsc->buckets[fsc->num_buckets-1].max = max ;
}

void ffsb_statsc_destroy(ffsb_statsc_t *fsc)
{
	free(fsc->buckets);
}

void ffsb_statsc_ignore_sys(ffsb_statsc_t *fsc, syscall_t s)
{
	/* printf("fsis: oring 0x%x with 0x%x\n",
	 *      fsc->ignore_stats,
	 *      (1 << s ) );
	 */
	fsc->ignore_stats |= (1 << s);
}

int fsc_ignore_sys(ffsb_statsc_t *fsc, syscall_t s)
{
	return fsc->ignore_stats & (1 << s);
}

void ffsb_statsd_init(ffsb_statsd_t *fsd, ffsb_statsc_t *fsc)
{
	int i;
	memset(fsd, 0, sizeof(*fsd));

	for (i = 0; i < FFSB_NUM_SYSCALLS; i++) {
		fsd->totals[i] = 0;
		fsd->mins[i] = UINT_MAX;
		fsd->maxs[i] = 0;
		fsd->buckets[i] = ffsb_malloc(sizeof(uint32_t) *
					      fsc->num_buckets);
		assert(fsd->buckets[i] != NULL);

		memset(fsd->buckets[i], 0, sizeof(uint32_t) *
		       fsc->num_buckets);

		// init mem for saving values
		fsd->values[i] = (uint32_t *)calloc(FFSB_VALUES_INIT_ARRAY_SIZE, sizeof(uint32_t));
		fsd->num_values[i] = 0;
		fsd->max_values[i] = FFSB_VALUES_INIT_ARRAY_SIZE;
	}

	fsd->config = fsc;
}

void ffsb_statsd_destroy(ffsb_statsd_t *fsd)
{
	int i ;
	for (i = 0 ; i < FFSB_NUM_SYSCALLS; i++)
	{
		free(fsd->buckets[i]);
		free(fsd->values[i]);
	}
}

void ffsb_add_data(ffsb_statsd_t *fsd, syscall_t s, uint32_t value)
{
	unsigned num_buckets, i;
	struct stat_bucket *bucket_defs;

	if (!fsd || fsc_ignore_sys(fsd->config, s))
		return;

	if (value < fsd->mins[s])
		fsd->mins[s] = value;
	if (value > fsd->maxs[s])
		fsd->maxs[s] = value;

	fsd->counts[s]++;
	fsd->totals[s] += value;

	// If allocated mem does not fit reallocate new one with bigger size
	if (fsd->num_values[s] >= fsd->max_values[s])
	{
		fsd->max_values[s] += fsd->max_values[s];
		fsd->values[s] = (uint32_t *)realloc(fsd->values[s], fsd->max_values[s]*sizeof(uint32_t));
	}
	// Store values to fsd
	fsd->values[s][fsd->num_values[s]++] = value;

	if (fsd->config->num_buckets == 0)
		return;

	num_buckets = fsd->config->num_buckets;
	bucket_defs = fsd->config->buckets;

	for (i = 0; i < num_buckets; i++) {
		struct stat_bucket *b = &bucket_defs[i];

		if (value <= b->max && value >= b->min) {
			fsd->buckets[s][i]++;
			break;
		}
	}
}

void ffsb_statsc_copy(ffsb_statsc_t *dest, ffsb_statsc_t *src)
{
	memcpy(dest, src, sizeof(*src));
}

void ffsb_statsd_add(ffsb_statsd_t *dest, ffsb_statsd_t *src)
{
	int i, j;
	unsigned num_buckets;
	if (dest->config != src->config)
		printf("ffsb_statsd_add: warning configs do not"
		       "match for data being collected\n");

	num_buckets = dest->config->num_buckets;

	for (i = 0; i < FFSB_NUM_SYSCALLS; i++) {
		dest->counts[i] += src->counts[i];
		dest->totals[i] += src->totals[i];

		if (src->mins[i] < dest->mins[i])
			dest->mins[i] = src->mins[i];
		if (src->maxs[i] > dest->maxs[i])
			dest->maxs[i] = src->maxs[i];

		for (j = 0; j < num_buckets; j++)
			dest->buckets[i][j] += src->buckets[i][j];

		// If allocated mem does not fit reallocate new one with bigger size
		if (dest->num_values[i] + src->num_values[i] >= dest->max_values[i])
		{
			dest->max_values[i] = dest->num_values[i] + src->num_values[i];
			dest->values[i] = (uint32_t *)realloc(dest->values[i], (dest->max_values[i])*sizeof(uint32_t));
		}

		uint64_t k;
		// merge values from each fsd
		for (k = 0; k < src->num_values[i]; ++k)
		{
			dest->values[i][dest->num_values[i]++] = src->values[i][k];
		}
	}
}

static void print_buckets_helper(ffsb_statsc_t *fsc, uint32_t *buckets)
{
	int i;
	// if (fsc->num_buckets == 0) {
	// 	printf("   -\n");
	// 	return;
	// }
	for (i = 0; i < fsc->num_buckets; i++) {
		struct stat_bucket *sb = &fsc->buckets[i];
		printf("\t\t msec_range[%d]\t%f - %f : %8u\n", 
		       i, (double)sb->min/1000.0f, (double)sb->max/1000.0f,
		       buckets[i]);
	}
	printf("\n");
}


typedef struct {
    float read;
    float regex;
	float total;
	float write;
} Latency;

int cmpfunc (const void * a, const void * b)
{
    if (*(float*)a > *(float*)b) return 1;
    else if (*(float*)a < *(float*)b) return -1;
    else return 0;
}
int compare(const void *p, const void *q) {
    int l = ((Latency *)p)->total;
    int r = ((Latency *)q)->total;
    return (l - r);
}

void ffsb_statsd_print(ffsb_statsd_t *fsd)
{
	int n = (unsigned long)fsd->num_values[1];
	// Latency *lat_arr = (Latency *)malloc(n * sizeof(Latency));
	// for (int i=0;i<n;i++){
	// 	lat_arr[i].read = fsd->values[1][i];
	// 	lat_arr[i].regex = fsd->values[8][i];
	// 	lat_arr[i].write = fsd->values[2][i];
	// 	lat_arr[i].total = lat_arr[i].read + lat_arr[i].regex + lat_arr[i].write ;
	// }
	// qsort(lat_arr, n, sizeof(Latency), compare);

	int i;
	uint64_t overall_calls = 0;
	printf("\nSystem Call Latency statistics in microsecs\n" "=====\n");
	printf("\t\tMin\t\tAvg\t\tMax\t\t99%\t\tTotal Calls\n");
	printf("\t\t========\t========\t========\t============\n");
	for (i = 0; i < FFSB_NUM_SYSCALLS; i++){
		if (fsd->counts[i]) {
			qsort((float *)fsd->values[i],(unsigned long) fsd->counts[i], sizeof(float), cmpfunc);
			printf("[%7s]\t%f\t%05lf\t%f\t%lf\t%12u\n",
			       syscall_names[i], (float)fsd->mins[i],
			       (fsd->totals[i] / (double)fsd->counts[i]),
			       (float)fsd->maxs[i], 
				   (float)fsd->values[i][(unsigned long) (0.99 * (unsigned long)fsd->num_values[i])],
				   (unsigned long)fsd->counts[i]);
			print_buckets_helper(fsd->config, fsd->buckets[i]);
		}
		overall_calls += fsd->num_values[i];
	}
	// printf("[%7s]\t%f\t%05lf\t%f\t%lf\t%12u\n",
	// 		"Total", lat_arr[0].total,
	// 		(fsd->totals[1] / (double)fsd->counts[1]) + (fsd->totals[8] / (double)fsd->counts[8]) + (fsd->totals[2] / (double)fsd->counts[2]),
	// 		lat_arr[n-1].total, 
	// 		lat_arr[(unsigned long) (0.99 * n)].total,
	// 		n);
	printf("\nAverage_latency_breakdown(read): \t%f",
			(fsd->totals[1] / (double)fsd->counts[1]));
	printf("\nAverage_latency_breakdown(regex): \t%f",
			(fsd->totals[8] / (double)fsd->counts[8]));
	printf("\nAverage_latency_breakdown(write): \t%f",
			(fsd->totals[2] / (double)fsd->counts[2]));
	// printf("\n99%% latency breakedown (read):  \t%f",
	// 		lat_arr[(unsigned long) (0.99 * n)].read);
	// printf("\n99%% latency breakedown (regex):  \t%f",
	// 	lat_arr[(unsigned long) (0.99 * n)].regex);
	// printf("\n99%% latency breakedown (write):  \t%f\n",
	// 	lat_arr[(unsigned long) (0.99 * n)].write);

	// printf("\nDiscrete overall System Call Latency statistics in millisecs\n" "=====\n");
	// printf("\nOverall Calls: %lu\n=====\nValues[ms]:", (unsigned long)overall_calls);
	// uint64_t j;
	// for (i = 0; i < FFSB_NUM_SYSCALLS; ++i)
	// {
	// 	printf("\n====\n[%7s]\tTotal calls: %12lu\n", syscall_names[i], (unsigned long)fsd->num_values[i]);
	// 	for (j = 0; j < fsd->num_values[i]; ++j)
	// 	{
	// 		printf("\n%05f", (float)fsd->values[i][j] / 1000.0f);
	// 	}
	// }
}

#if 0 /* Testing */

void *ffsb_malloc(size_t s)
{
	void *p = malloc(s);
	assert(p != NULL);
	return p;
}

int main(int arc, char *argv[])
{
	ffsb_statsc_t fsc;
	ffsb_statsd_t fsd;
	int i ;

	printf("init\n");

	ffsb_statsc_init(&fsc, 10);
	ffsb_statsc_setbucket(&fsc, 0, 0.0f, 50.0f);
	ffsb_statsc_setbucket(&fsc, 1, 50.0f, 10000.0f);
	ffsb_statsc_setbucket(&fsc, 2, 0.1f, 0.2f);
	ffsb_statsc_setbucket(&fsc, 3, 0.0f, 50.0f);
	ffsb_statsc_setbucket(&fsc, 4, 50.0f, 10000.0f);
	ffsb_statsc_setbucket(&fsc, 5, 0.1f, 0.2f);
	ffsb_statsc_setbucket(&fsc, 6, 0.0f, 50.0f);
	ffsb_statsc_setbucket(&fsc, 7, 50.0f, 10000.0f);
	ffsb_statsc_setbucket(&fsc, 8, 0.1f, 0.2f);
	ffsb_statsc_setbucket(&fsc, 9, 50.0f, 10000.0f);
	ffsb_statsd_init(&fsd, &fsc);

	printf("test\n");
	for (i = 0; i < 50000000; i++)
		ffsb_add_data(&fsd, SYS_READ, (float)i);

	printf("cleanup\n");
	ffsb_statsd_destroy(&fsd);
	ffsb_statsc_destroy(&fsc);
	return 0;
}

#endif /* Testing */
