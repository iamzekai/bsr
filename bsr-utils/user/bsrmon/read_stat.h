#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLLECTION_TIME_LENGTH 23

struct perf_stat {
	unsigned long long priv;
	unsigned long long min;
	unsigned long long max;
	unsigned long long sum;
	unsigned long cnt;
	bool duplicate;
};

#ifdef _WIN
struct kmem_perf_stat {
	/* bytes */
	struct perf_stat total; /* TotalUsed */ 
	struct perf_stat npused; /* NonPagedUsed */ 
	struct perf_stat pused; /* PagedUsed */ 
	struct perf_stat untnpused; /* UntagNonPagedUsed */
};

struct umem_perf_stat {
	/* bytes */
	struct perf_stat wss;  /* WorkingSetSize */ 
	struct perf_stat qpp;  /* QuotaPagedPoolUsage */ 
	struct perf_stat qnpp; /* QuotaNonPagedPoolUsage */ 
	struct perf_stat pfu;  /* PagefileUsage */
};
#else // _LIN
struct sys_mem_perf_stat {
	struct perf_stat total;
	struct perf_stat used;
	struct perf_stat free;
	struct perf_stat buff_cache;
};
struct kmem_perf_stat {
	/* bytes */
	struct perf_stat req; /* BSR_REQ */ 
	struct perf_stat al;  /* BSR_AL */ 
	struct perf_stat bm;  /* BSR_BM */ 
	struct perf_stat ee;  /* BSR_EE */ 
	struct perf_stat bio_set;
	struct perf_stat kmalloc;
	struct perf_stat vmalloc;
	struct perf_stat page_pool;
};

struct umem_perf_stat {
	/* kbytes */
	struct perf_stat rsz;
	struct perf_stat vsz;
};

// BSR-875
#define TOP_PROCESS_LIST_CNT 5
struct process_info {
	char name[64];
	unsigned int pid;
	unsigned int rsz;
	unsigned int vsz;
	char _time[64];
};

#endif


struct io_perf_stat {
	struct perf_stat iops;
	struct perf_stat kbs;
	unsigned long long ios;
	unsigned long long kb;
};

struct req_perf_stat {
	struct perf_stat before_queue;
	struct perf_stat before_al_begin;
	struct perf_stat in_actlog;
	struct perf_stat submit;
	struct perf_stat bio_endio;
	struct perf_stat destroy;
	struct perf_stat before_bm_write;
	struct perf_stat after_bm_write;
	struct perf_stat after_sync_page;
};


// BSR-765 add AL performance aggregation
struct al_perf_stat {
	unsigned long cnt;
	unsigned long max;
	unsigned long long sum;
};

struct al_stat {
	unsigned int nr_elements;
	struct al_perf_stat used;
	unsigned long long hits;
	unsigned long long misses;
	unsigned long long starving;
	unsigned long long locked;
	unsigned long long changed;
	struct al_perf_stat wait;
	struct al_perf_stat pending;
	unsigned int e_starving;
	unsigned int e_pending;
	unsigned int e_used;
	unsigned int e_busy;
	unsigned int e_wouldblock;
};


extern char g_perf_path[MAX_PATH];

// BSR-948
struct title_field {
	const char *name;
	int nr; // Number of items 
	int no_close_brace;
};
struct perf_field {
	const char *name;
	const char *unit;
};


// for report
void read_io_stat_work(char *path, struct time_filter *tf);
void read_io_complete_work(char *path, struct time_filter *tf);
void read_req_stat_work(char *path, char *resname, struct time_filter *tf);
void read_memory_work(char *path, struct time_filter *tf);
// BSR-764
void read_peer_stat_work(char *path, char *resname, int type, struct time_filter *tf);
// BSR-765
void read_al_stat_work(char *path, struct time_filter *tf);

// for watch
void watch_io_stat(char *path, bool scroll);
void watch_io_complete(char *path, bool scroll);
void watch_req_stat(char *path, bool scroll);
void watch_network_speed(char *path, bool scroll);
void watch_sendbuf(char *path, bool scroll);
void watch_memory(char *path, bool scroll);
// BSR-764
void watch_peer_req_stat(char *path, bool scroll);
// BSR-765
void watch_al_stat(char *path, bool scroll);
// BSR-838
void watch_peer_resync_ratio(char *path, bool scroll);

// for show
void print_current(struct resource *res, int type_flags, bool json);