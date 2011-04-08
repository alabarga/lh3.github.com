#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <zlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "kseq.h"
KSEQ_INIT(gzFile, gzread)

typedef struct {
	int n, m;
	uint64_t *a;
} reglist_t;

#include "khash.h"
KHASH_MAP_INIT_STR(reg, reglist_t)

typedef kh_reg_t reghash_t;

reghash_t *stk2_reg_read(const char *fn)
{
	reghash_t *h = kh_init(reg);
	gzFile fp;
	kstream_t *ks;
	int dret;
	kstring_t *str;
	// read the list
	str = calloc(1, sizeof(kstring_t));
	fp = strcmp(fn, "-")? gzopen(fn, "r") : gzdopen(fileno(stdin), "r");
	ks = ks_init(fp);
	while (ks_getuntil(ks, 0, str, &dret) >= 0) {
		char *s = strdup(str->s);
		int beg = 0, end = INT_MAX;
		reglist_t *p;
		khint_t k = kh_get(reg, h, s);
		if (k == kh_end(h)) {
			int ret;
			k = kh_put(reg, h, s, &ret);
			memset(&kh_val(h, k), 0, sizeof(reglist_t));
		}
		p = &kh_val(h, k);
		if (dret != '\n') {
			if (ks_getuntil(ks, 0, str, &dret) > 0)
				beg = atoi(str->s);
			if (dret != '\n') {
				if (ks_getuntil(ks, 0, str, &dret) > 0)
					end = atoi(str->s);
			}
		}
		// skip the rest of the line
		if (dret != '\n') while ((dret = ks_getc(ks)) > 0 && dret != '\n');
		if (p->n == p->m) {
			p->m = p->m? p->m<<1 : 4;
			p->a = realloc(p->a, p->m * 8);
		}
		p->a[p->n++] = (uint64_t)beg<<32 | end;
	}
	ks_destroy(ks);
	gzclose(fp);
	free(str->s); free(str);
	return h;
}

void stk2_reg_destroy(reghash_t *h)
{
	khint_t k;
	for (k = 0; k < kh_end(h); ++k) {
		if (kh_exist(h, k)) {
			free(kh_val(h, k).a);
			free((char*)kh_key(h, k));
		}
	}
	kh_destroy(reg, h);
}

/* constant table */

unsigned char seq_nt16_table[256] = {
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15 /*'-'*/,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15, 1,14, 2, 13,15,15, 4, 11,15,15,12, 15, 3,15,15,
	15,15, 5, 6,  8,15, 7, 9,  0,10,15,15, 15,15,15,15,
	15, 1,14, 2, 13,15,15, 4, 11,15,15,12, 15, 3,15,15,
	15,15, 5, 6,  8,15, 7, 9,  0,10,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
	15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15
};

char *seq_nt16_rev_table = "XACMGRSVTWYHKDBN";
unsigned char seq_nt16to4_table[] = { 4, 0, 1, 4, 2, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4 };
int bitcnt_table[] = { 4, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

/* composition */
int stk2_comp(int argc, char *argv[])
{
	gzFile fp;
	kseq_t *seq;
	int l;
	if (argc == 1) {
		fprintf(stderr, "Usage:  seqtk2 comp <in.fa>\n\n");
		fprintf(stderr, "Output format: chr, length, #A, #C, #G, #T, #2, #3, #4, #upper, #lower, #lower+N\n");
		return 1;
	}
	fp = (strcmp(argv[1], "-") == 0)? gzdopen(fileno(stdin), "r") : gzopen(argv[1], "r");
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		int i, cnt[10]; // #A, #C, #G, #T, #2, #3, #4, #upper, #lower, #lower+N
		memset(cnt, 0, 10 * sizeof(int));
		for (i = 0; i < l; ++i) {
			int a, c, b = seq->seq.s[i];
			if (isupper(b)) ++cnt[7];
			if (islower(b)) ++cnt[8];
			c = seq_nt16_table[b];
			if (islower(b) || c == 15) ++cnt[9];
			a = bitcnt_table[c];
			if (a > 1) ++cnt[a+2];
			c = seq_nt16to4_table[c];
			if (c < 4) ++cnt[c];
		}
		printf("%s\t%d", seq->name.s, l);
		for (i = 0; i < 10; ++i) printf("\t%d", cnt[i]);
		putchar('\n');
		fflush(stdout);
	}
	kseq_destroy(seq);
	gzclose(fp);
	return 0;
}

int stk2_hety(int argc, char *argv[])
{
	gzFile fp;
	kseq_t *seq;
	int l, c, win_size = 50000, n_start = 5, win_step, is_lower_mask = 0;
	char *buf;
	uint32_t cnt[3];
	if (argc == 1) {
		fprintf(stderr, "\n");
		fprintf(stderr, "Usage:   seqtk2 hety [options] <in.fa>\n\n");
		fprintf(stderr, "Options: -w INT   window size [%d]\n", win_size);
		fprintf(stderr, "         -t INT   # start positions in a window [%d]\n", n_start);
		fprintf(stderr, "         -m       treat lowercases as masked\n");
		fprintf(stderr, "\n");
		return 1;
	}
	while ((c = getopt(argc, argv, "w:t:m")) >= 0) {
		switch (c) {
		case 'w': win_size = atoi(optarg); break;
		case 't': n_start = atoi(optarg); break;
		case 'm': is_lower_mask = 1; break;
		}
	}
	fp = (strcmp(argv[optind], "-") == 0)? gzdopen(fileno(stdin), "r") : gzopen(argv[optind], "r");
	seq = kseq_init(fp);
	win_step = win_size / n_start;
	buf = calloc(win_size, 1);
	while ((l = kseq_read(seq)) >= 0) {
		int x, i, y, z, next = 0;
		cnt[0] = cnt[1] = cnt[2] = 0;
		for (i = 0; i <= l; ++i) {
			if ((i >= win_size && i % win_step == 0) || i == l) {
				if (i == l && l >= win_size) {
					for (y = l - win_size; y < next; ++y) --cnt[(int)buf[y % win_size]];
				}
				if (cnt[1] + cnt[2] > 0)
					printf("%s\t%d\t%d\t%.2lf\t%d\t%d\n", seq->name.s, next, i,
						   (double)cnt[2] / (cnt[1] + cnt[2]) * win_size, cnt[1] + cnt[2], cnt[2]);
				next = i;
			}
			if (i < l) {
				y = i % win_size;
				c = seq->seq.s[i];
				if (is_lower_mask && islower(c)) c = 'N';
				c = seq_nt16_table[c];
				x = bitcnt_table[c];
				if (i >= win_size) --cnt[(int)buf[y]];
				buf[y] = z = x > 2? 0 : x == 2? 2 : 1;
				++cnt[z];
			}
		}
	}
	free(buf);
	kseq_destroy(seq);
	gzclose(fp);
	return 0;
}

/* fq2fa */
int stk2_fq2fa(int argc, char *argv[])
{
	gzFile fp;
	kseq_t *seq;
	int l, i, j, qual_thres = 0;
	if (argc == 1) {
		fprintf(stderr, "Usage: seqtk2 fq2fa <in.fq> [qual_thres]\n");
		return 1;
	}
	fp = strcmp(argv[1], "-")? gzopen(argv[1], "r") : gzdopen(fileno(stdin), "r");
	if (argc >= 3) qual_thres = atoi(argv[2]);
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		if (seq->qual.l) {
			for (i = 0; i < l; ++i)
				if (seq->qual.s[i] - 33 < qual_thres)
					seq->seq.s[i] = tolower(seq->seq.s[i]);
		}
		printf(">%s", seq->name.s);
		for (i = 0; i < l; i += 60) {
			putchar('\n');
			for (j = i; j < i + 60 && j < l; ++j) putchar(seq->seq.s[j]);
		}
		putchar('\n');
	}
	kseq_destroy(seq);
	gzclose(fp);
	return 0;
}

int stk2_maskseq(int argc, char *argv[])
{
	khash_t(reg) *h = kh_init(reg);
	gzFile fp;
	kseq_t *seq;
	int l, i, j, c, is_complement = 0, is_lower = 0;
	khint_t k;
	while ((c = getopt(argc, argv, "cl")) >= 0) {
		switch (c) {
		case 'c': is_complement = 1; break;
		case 'l': is_lower = 1; break;
		}
	}
	if (argc - optind < 2) {
		fprintf(stderr, "Usage: seqtk2 maskseq [-cl] <in.fa> <in.bed>\n");
		return 1;
	}
	h = stk2_reg_read(argv[optind+1]);
	// maskseq
	fp = strcmp(argv[optind], "-")? gzopen(argv[optind], "r") : gzdopen(fileno(stdin), "r");
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		reglist_t *p;
		k = kh_get(reg, h, seq->name.s);
		if (k == kh_end(h)) continue;
		p = &kh_val(h, k);
		for (i = 0; i < p->n; ++i) {
			int beg = p->a[i]>>32, end = p->a[i];
			if (beg >= seq->seq.l) {
				fprintf(stderr, "[maskseq] start position >= the sequence length.\n");
				continue;
			}
			if (is_lower) for (j = beg; j < end; ++j) seq->seq.s[j] = tolower(seq->seq.s[j]);
			else for (j = beg; j < end; ++j) seq->seq.s[j] = 'N';
		}
		printf(">%s", seq->name.s);
		for (j = 0; j < seq->seq.l; ++j) {
			if (j%60 == 0) putchar('\n');
			putchar(seq->seq.s[j]);
		}
		putchar('\n');
	}
	// free
	kseq_destroy(seq);
	gzclose(fp);
	stk2_reg_destroy(h);
	return 0;
}

int stk2_combine(int argc, char *argv[])
{
	kseq_t *seq[2];
	gzFile fp[2];
	int l[2];
	if (argc < 3) {
		fprintf(stderr, "Usage: seqtk2 combine <in1.fa> <in2.fa>\n");
		return 1;
	}
	fp[0] = strcmp(argv[1], "-")? gzopen(argv[1], "r") : gzdopen(fileno(stdin), "r");
	fp[1] = strcmp(argv[2], "-")? gzopen(argv[2], "r") : gzdopen(fileno(stdin), "r");
	seq[0] = kseq_init(fp[0]); seq[1] = kseq_init(fp[1]);
	while ((l[0] = kseq_read(seq[0])) >= 0) {
		int min, i;
		l[1] = kseq_read(seq[1]); 
		if (l[0] != l[1]) fprintf(stderr, "Warning: %d != %d\n", l[0], l[1]);
		min = l[0] < l[1]? l[0] : l[1];
		printf(">%s", seq[0]->name.s);
		for (i = 0; i < min; ++i) {
			int c = seq_nt16_table[(int)seq[0]->seq.s[i]] | seq_nt16_table[(int)seq[1]->seq.s[i]];
			if (i % 60 == 0) putchar('\n');
			putchar(seq_nt16_rev_table[c]);
		}
		putchar('\n');
	}
	kseq_destroy(seq[0]); gzclose(fp[0]);
	kseq_destroy(seq[1]); gzclose(fp[1]);
	return 0;
}

/* subseq */

int stk2_subseq(int argc, char *argv[])
{
	khash_t(reg) *h = kh_init(reg);
	gzFile fp;
	kseq_t *seq;
	int l, i, j;
	khint_t k;
	if (argc < 3) {
		fprintf(stderr, "Usage: seqtk2 subseq <in.fa> <in.bed>\n\n");
		fprintf(stderr, "Note: Use 'samtools faidx' if only a few regions are intended.\n");
		return 1;
	}
	h = stk2_reg_read(argv[2]);
	// subseq
	fp = strcmp(argv[1], "-")? gzopen(argv[1], "r") : gzdopen(fileno(stdin), "r");
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		reglist_t *p;
		k = kh_get(reg, h, seq->name.s);
		if (k == kh_end(h)) continue;
		p = &kh_val(h, k);
		for (i = 0; i < p->n; ++i) {
			int beg = p->a[i]>>32, end = p->a[i];
			if (beg >= seq->seq.l) {
				fprintf(stderr, "[subseq] start position >= the sequence length.\n");
				continue;
			}
			printf("%c%s", seq->qual.l == seq->seq.l? '@' : '>', seq->name.s);
			if (end == INT_MAX) {
				if (beg) printf(":%d", beg+1);
			} else printf(":%d-%d", beg+1, end);
			if (end > seq->seq.l) end = seq->seq.l;
			for (j = 0; j < end - beg; ++j) {
				if (j % 60 == 0) putchar('\n');
				putchar(seq->seq.s[j + beg]);
			}
			putchar('\n');
			if (seq->qual.l != seq->seq.l) continue;
			printf("+\n");
			for (j = 0; j < end - beg; ++j) {
				if (j % 60 == 0) putchar('\n');
				putchar(seq->seq.s[j + beg]);
			}
			putchar('\n');
		}
	}
	// free
	kseq_destroy(seq);
	gzclose(fp);
	stk2_reg_destroy(h);
	return 0;
}

int stk2_mutfa(int argc, char *argv[])
{
	khash_t(reg) *h = kh_init(reg);
	gzFile fp;
	kseq_t *seq;
	kstream_t *ks;
	int l, i, dret;
	kstring_t *str;
	khint_t k;
	if (argc < 3) {
		fprintf(stderr, "Usage: seqtk2 mutfa <in.fa> <in.snp>\n\n");
		fprintf(stderr, "Note: <in.snp> contains at least four columns per line which are:\n");
		fprintf(stderr, "      'chr  1-based-pos  any  base-changed-to'.\n");
		return 1;
	}
	// read the list
	str = calloc(1, sizeof(kstring_t));
	fp = strcmp(argv[2], "-")? gzopen(argv[2], "r") : gzdopen(fileno(stdin), "r");
	ks = ks_init(fp);
	while (ks_getuntil(ks, 0, str, &dret) >= 0) {
		char *s = strdup(str->s);
		int beg = 0, ret;
		reglist_t *p;
		k = kh_get(reg, h, s);
		if (k == kh_end(h)) {
			k = kh_put(reg, h, s, &ret);
			memset(&kh_val(h, k), 0, sizeof(reglist_t));
		}
		p = &kh_val(h, k);
		if (ks_getuntil(ks, 0, str, &dret) > 0) beg = atol(str->s) - 1; // 2nd col
		ks_getuntil(ks, 0, str, &dret); // 3rd col
		ks_getuntil(ks, 0, str, &dret); // 4th col
		// skip the rest of the line
		if (dret != '\n') while ((dret = ks_getc(ks)) > 0 && dret != '\n');
		if (isalpha(str->s[0]) && str->l == 1) {
			if (p->n == p->m) {
				p->m = p->m? p->m<<1 : 4;
				p->a = realloc(p->a, p->m * 8);
			}
			p->a[p->n++] = (uint64_t)beg<<32 | str->s[0];
		}
	}
	ks_destroy(ks);
	gzclose(fp);
	free(str->s); free(str);
	// mutfa
	fp = strcmp(argv[1], "-")? gzopen(argv[1], "r") : gzdopen(fileno(stdin), "r");
	seq = kseq_init(fp);
	while ((l = kseq_read(seq)) >= 0) {
		reglist_t *p;
		k = kh_get(reg, h, seq->name.s);
		if (k != kh_end(h)) {
			p = &kh_val(h, k);
			for (i = 0; i < p->n; ++i) {
				int beg = p->a[i]>>32;
				if (beg < seq->seq.l)
					seq->seq.s[beg] = (int)p->a[i];
			}
		}
		printf(">%s", seq->name.s);
		for (i = 0; i < l; ++i) {
			if (i%60 == 0) putchar('\n');
			putchar(seq->seq.s[i]);
		}
		putchar('\n');
	}
	// free
	kseq_destroy(seq);
	gzclose(fp);
	for (k = 0; k < kh_end(h); ++k) {
		if (kh_exist(h, k)) {
			free(kh_val(h, k).a);
			free((char*)kh_key(h, k));
		}
	}
	kh_destroy(reg, h);
	return 0;
}

/* main function */
static int usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage:   seqtk2 <command> <arguments>\n\n");
	fprintf(stderr, "Command: comp      get the nucleotide composite of FASTA/Q\n");
	fprintf(stderr, "         hety      regional heterozygosity\n");
	fprintf(stderr, "         fq2fa     convert FASTQ to FASTA\n");
	fprintf(stderr, "         subseq    extract subsequences from FASTA/Q\n");
	fprintf(stderr, "         mutfa     point mutate FASTA at specified positions\n");
	fprintf(stderr, "         combine   combine two FASTA files\n");
	fprintf(stderr, "\n");
	return 1;
}

int main(int argc, char *argv[])
{
	if (argc == 1) return usage();
	if (strcmp(argv[1], "comp") == 0) stk2_comp(argc-1, argv+1);
	else if (strcmp(argv[1], "hety") == 0) stk2_hety(argc-1, argv+1);
	else if (strcmp(argv[1], "fq2fa") == 0) stk2_fq2fa(argc-1, argv+1);
	else if (strcmp(argv[1], "subseq") == 0) stk2_subseq(argc-1, argv+1);
	else if (strcmp(argv[1], "maskseq") == 0) stk2_maskseq(argc-1, argv+1);
	else if (strcmp(argv[1], "mutfa") == 0) stk2_mutfa(argc-1, argv+1);
	else if (strcmp(argv[1], "combine") == 0) stk2_combine(argc-1, argv+1);
	else {
		fprintf(stderr, "[main] unrecognized commad '%s'. Abort!\n", argv[1]);
		return 1;
	}
	return 0;
}
