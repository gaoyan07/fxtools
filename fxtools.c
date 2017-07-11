#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include "utils.h"
#include "fxtools.h"
#include "kseq.h"
#include "htslib/htslib/sam.h"
#include "htslib/htslib/faidx.h"

#define _ll_t long long

KSEQ_INIT(gzFile, gzread)

int usage(void)
{
    fprintf(stderr, "Program: fxtools (fasta and fastq data tools)\n");
    fprintf(stderr, "Usage:   fxtools <command> [options]\n\n");
    fprintf(stderr, "Command: \n");
    fprintf(stderr, "         filter (fl)           filter fa/fq sequences with specified length bound.\n");
    fprintf(stderr, "         filter-name (fn)      filter fa/fq sequences with specified name.\n");
    fprintf(stderr, "         filter-bam (fb)       filter bam/sam records with specified read length bound.\n");
    fprintf(stderr, "         filter-bam-name (fbn) filter bam/sam records with specified read name.\n");
    fprintf(stderr, "         fq2fa (qa)            convert FASTQ format data to FASTA format data.\n");
    fprintf(stderr, "         fa2fq (aq)            convert FASTA format data to FASTQ format data.\n");
    fprintf(stderr, "         re-co (rc)            convert DNA sequence(fa/fq) to its reverse-complementary sequence.\n");
    fprintf(stderr, "         seq-display (sd)      display a specified region of FASTA/FASTQ file.\n");
    fprintf(stderr, "         cigar-parse (cp)      parse the given cigar(stdout).\n");
    fprintf(stderr, "         length-parse (lp)     parse the length of sequences in fa/fq file.\n");
    fprintf(stderr, "         merge-fa (mf)         merge the reads with same read name in fasta/fastq file.\n");
    fprintf(stderr, "         merge-filter-fa (mff) merge and filter the reads with same read name in fasta file.\n");
    fprintf(stderr, "         error-parse (ep)      parse indel and mismatch error based on CIGAR and NM in bam file.\n");
    fprintf(stderr, "         dna2rna (dr)          convert DNA fa/fq to RNA fa/fq.\n");
    fprintf(stderr, "         rna2dna (rd)          convert RNA fa/fq to DNA fa/fq.\n");
    fprintf(stderr, "         peak-seq (ps)         extract m6a peak sequence from bam file.\n");
    fprintf(stderr, "         motif-seq (ms)        extract m6a motif sequence from bam file.\n");
    //fprintf(stderr, "      ./fa_filter in.fa out.fa low-bound upper-bound(-1 for no bound)\n");
    fprintf(stderr, "\n");
    return 1;
}

void print_seq(FILE *out, kseq_t *seq)
{
    if (seq->seq.l == 0) return;
    if (seq->qual.l != 0)
    {
        fprintf(out, "@%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(out, " %s", seq->comment.s);
        fprintf(out, "\n");
        fprintf(out, "%s\n", seq->seq.s);
        fprintf(out, "+\n");
        fprintf(out, "%s\n", seq->qual.s);
    }
    else
    {
        fprintf(out, ">%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(out, " %s", seq->comment.s);
        fprintf(out, "\n");
        fprintf(out, "%s\n", seq->seq.s);
    }
}

int fxt_filter(int argc, char* argv[])
{
    if (argc != 4) 
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools filter <in.fa/fq> <lower-bound> <upper-bound>(-1 for NO bound) > <out.fa/fq>\n");
        fprintf(stderr, "\n");
        exit(-1);
    }
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_filter] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    FILE *out = stdout;
    kseq_t *seq;
    seq = kseq_init(infp);
    int64_t low = atoi(argv[2]);
    int64_t upper = atoi(argv[3]);
    while (kseq_read(seq) >= 0)
    {
        if ((low != -1 && (int64_t)seq->seq.l < low) || (upper != -1 && (int64_t)seq->seq.l > upper))
            continue;
        print_seq(out, seq);
    }

    fclose(out);
    gzclose(infp);
    return 0;
}

int fxt_filter_bam(int argc, char *argv[])
{
    if (argc != 4) 
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools filter-bam <in.bam/sam> <lower-bound> <upper-bound>(-1 for NO bound) > <out.bam>\n");
        fprintf(stderr, "\n");
        exit(-1);
    }

    samFile *in, *out; bam_hdr_t *h; bam1_t *b;
    int seq_len; int64_t low = atoi(argv[2]), upper = atoi(argv[3]);
    if ((in = sam_open(argv[1], "rb")) == NULL) err_fatal_core(__func__, "Cannot open \"%s\"\n", argv[1]);
    if ((h = sam_hdr_read(in)) == NULL) err_fatal(__func__, "Couldn't read header for \"%s\"\n", argv[1]);
    b = bam_init1(); 

    if ((out = sam_open_format("-", "wb", NULL)) == NULL) err_fatal_simple("Cannot open \"-\"\n");
    if (sam_hdr_write(out, h) != 0) err_fatal_simple("Error in writing SAM header\n"); //sam header

    while (sam_read1(in, h, b) >= 0) {
        seq_len = b->core.l_qseq;
        if ((low != -1 && (int64_t)seq_len < low) || (upper != -1 && (int64_t)seq_len > upper))
            continue;
        if (sam_write1(out, h, b) < 0) err_fatal_simple("Error in writing SAM record\n");
    }
    bam_destroy1(b); bam_hdr_destroy(h); sam_close(in); sam_close(out);
    return 0;
}

int fxt_filter_name(int argc, char* argv[])
{
    int c, n=0, m=0; char name[1024], sub_name[1024];
    while ((c = getopt(argc, argv, "n:m:")) >= 0) {
        switch (c) {
            case 'n': n=1, strcpy(name, optarg); break;
            case 'm': m=1, strcpy(sub_name, optarg); break;
            default: err_printf("Error, unknown option: -%c %s\n", c, optarg);
        }
    }
    if (n + m != 1 || argc - optind != 1) 
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools filter-name [-n name] [-m sub-name] <in.fa/fq> > <out.fa/fq>\n");
        fprintf(stderr, "      -n [STR]    only output read with specified name.\n");
        fprintf(stderr, "      -m [STR]    only output read whose name or comment contain specified string.\n");
        fprintf(stderr, "\n");
        exit(-1);
    }
    gzFile infp;
    if (strcmp(argv[optind],"-") == 0 || strcmp(argv[optind], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[optind], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_filter_name] Can't open %s.\n", argv[optind]);
        exit(-1);
    }
    FILE *out = stdout;
    kseq_t *seq;
    seq = kseq_init(infp);
    while (kseq_read(seq) >= 0)
    {
        if (n) {
            if (strcmp(seq->name.s, name) != 0) continue;
        } else { // m
            if (seq->comment.l > 0) {
                if (strstr(seq->name.s, sub_name) == NULL && strstr(seq->comment.s, sub_name) == NULL) continue;
            } else {
                if (strstr(seq->name.s, sub_name) == NULL) continue;
            }
        }
        print_seq(out, seq); 
    }

    fclose(out);
    gzclose(infp);
    return 0;
}

int fxt_filter_bam_name(int argc, char *argv[])
{
    int c, n=0, m=0; char name[1024], sub_name[1024];
    while ((c = getopt(argc, argv, "n:m:")) >= 0) {
        switch (c) {
            case 'n': n=1, strcpy(name, optarg); break;
            case 'm': m=1, strcpy(sub_name, optarg); break;
            default: err_printf("Error, unknown option: -%c %s\n", c, optarg);
        }
    }
    if (n + m != 1 || argc - optind != 1) 
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools filter-bam-name [-n name] [-m sub-name] <in.bam/sam> > <out.bam>\n");
        fprintf(stderr, "      -n [STR]    only output bam record with specified read name.\n");
        fprintf(stderr, "      -m [STR]    only output bam record whose read name contain specified string.\n");
        fprintf(stderr, "\n");
        exit(-1);
    }

    samFile *in, *out; bam_hdr_t *h; bam1_t *b;
    if ((in = sam_open(argv[optind], "rb")) == NULL) err_fatal_core(__func__, "Cannot open \"%s\"\n", argv[optind]);
    if ((h = sam_hdr_read(in)) == NULL) err_fatal(__func__, "Couldn't read header for \"%s\"\n", argv[optind]);
    b = bam_init1(); 

    if ((out = sam_open_format("-", "wb", NULL)) == NULL) err_fatal_simple("Cannot open \"-\"\n");
    if (sam_hdr_write(out, h) != 0) err_fatal_simple("Error in writing SAM header\n"); //sam header

    char qname[1024];
    while (sam_read1(in, h, b) >= 0) {
        strcpy(qname, bam_get_qname(b));
        if (n) {
            if (strcmp(qname, name) != 0) continue;
        } else { // m
            if (strstr(qname, sub_name) == NULL) continue;
        }
        if (sam_write1(out, h, b) < 0) err_fatal_simple("Error in writing SAM record\n");
    }
    bam_destroy1(b); bam_hdr_destroy(h); sam_close(in); sam_close(out);
    return 0;
}

int fxt_fq2fa(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools fq2fa <in.fq> > <out.fa>\n\n");
        exit(-1);
    } 
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_fq2fa] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq;
    seq = kseq_init(infp);
    FILE *outfp = stdout;

    while (kseq_read(seq) >= 0)
    {
        fprintf(outfp, ">%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(outfp, " %s", seq->comment.s);
        fprintf(outfp, "\n");

        fprintf(outfp, "%s\n", seq->seq.s);
    }

    gzclose(infp);
    fclose(outfp);
    return 0;
}

int fxt_fa2fq(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools fa2fq <in.fa> > <out.fq>\n\n");
        exit(-1);
    } 
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_fa2fq] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq;
    seq = kseq_init(infp);
    FILE *outfp = stdout;

    int64_t i;
    while (kseq_read(seq) >= 0)
    {
        fprintf(outfp, "@%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(outfp, " %s", seq->comment.s);
        fprintf(outfp, "\n");

        fprintf(outfp, "%s\n", seq->seq.s);
        fprintf(outfp, "+\n");
        for (i = 0; i < (int64_t)seq->seq.l; ++i) fprintf(outfp, "!");
        fprintf(outfp, "\n");
    }

    gzclose(infp);
    fclose(outfp);
    return 0;
}

int fxt_re_co(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools re-co in.fa/fq > out.fa\n"); fprintf(stderr, "\n");
        return 1;
    }
    gzFile readfp;
    kseq_t *read_seq;
    int seq_len = 100000;
    char *seq = (char*)malloc(seq_len*sizeof(char));
    int8_t *seq_n = (int8_t*)malloc(seq_len*sizeof(int8_t));
    FILE *out = stdout;

    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) readfp = gzdopen(fileno(stdin), "r");
    else readfp = gzopen(argv[1], "r");
    read_seq = kseq_init(readfp);

    int len, i;

    while ((len = kseq_read(read_seq)) > 0)
    {
       if (len > seq_len)
       {
           seq_len <<= 1;
           seq = (char*)realloc(seq, seq_len*sizeof(char));
           seq_n = (int8_t*)realloc(seq_n, seq_len*sizeof(char));
           if (seq == NULL || seq_n == NULL)
           {
               fprintf(stderr, "memory is not enough.\n");
               exit(-1);
           }
       } 
       seq = read_seq->seq.s;
       for (i = 0; i < len; i++)
           seq_n[i] = nt_table[(int)seq[i]];
       fprintf(out, ">%s_re-co:", read_seq->name.s);
       if (read_seq->comment.l > 0) fprintf(out, " %s", read_seq->comment.s);
       fprintf(out, "\n");
       for (i = len - 1; i>=0; i--)
       {
           if (seq_n[i] != 4) 
               fprintf(out, "%c", nt_char[3-(int)seq_n[i]]);
            else
                fprintf(out, "N");
       }
       fprintf(out, "\n");
    }

    gzclose(readfp);
    kseq_destroy(read_seq);
    fclose(out);

    return 0;
}

int fxt_dna2rna(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools dna2rna <in.fa/fq> > <out.fa/fq>\n\n");
        exit(-1);
    } 
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_dna2rna] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq;
    seq = kseq_init(infp);
    FILE *outfp = stdout;

    while (kseq_read(seq) >= 0)
    {
        fprintf(outfp, ">%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(outfp, " %s", seq->comment.s);
        fprintf(outfp, "\n");

        size_t i;
        for (i = 0; i < seq->seq.l; ++i) {
            if (seq->seq.s[i] == 'T') fprintf(outfp, "U");
            else fprintf(outfp, "%c", seq->seq.s[i]);
        }
        fprintf(outfp, "\n");
    }

    gzclose(infp);
    fclose(outfp);
    return 0;
}

int fxt_rna2dna(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools rna2dna <in.fa/fq> > <out.fa/fq>\n\n");
        exit(-1);
    } 
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_rna2dna] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq;
    seq = kseq_init(infp);
    FILE *outfp = stdout;

    while (kseq_read(seq) >= 0)
    {
        fprintf(outfp, ">%s", seq->name.s);
        if (seq->comment.l > 0) fprintf(outfp, " %s", seq->comment.s);
        fprintf(outfp, "\n");

        size_t i;
        for (i = 0; i < seq->seq.l; ++i) {
            if (seq->seq.s[i] == 'U') fprintf(outfp, "T");
            else fprintf(outfp, "%c", seq->seq.s[i]);
        }
        fprintf(outfp, "\n");
    }

    gzclose(infp);
    fclose(outfp);
    return 0;
}

int fxt_seq_dis(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools seq-display <in.fa/fq> <chr/read_name> <start_pos(1-based)> <end_pos>\n");
        fprintf(stderr, "       use negative coordinate to indicate later part of sequence. (e.g., -1 for last bp)\n");
        fprintf(stderr, "\n"); 
        exit(-1);
    }
    faidx_t *fai = fai_load(argv[1]);
    if ( !fai ) {
        fprintf(stderr, "Could not load fai index of %s\n", argv[1]);
        fprintf(stderr, "Building fai index of %s\n", argv[1]);
        if (fai_build(argv[1]) != 0) {
            fprintf(stderr, "Could not build fai index %s.fai\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    int exit_status = EXIT_SUCCESS;

    char reg[1024], chr[102]; int start, end;
    strcpy(chr, argv[2]);
    start = atoi(argv[3]); end = atoi(argv[4]);
    int tot_len = faidx_seq_len(fai, chr);
    if (start < 0) start = tot_len + start + 1;
    if (end < 0) end = tot_len + end + 1;
    sprintf(reg, "%s:%d-%d", chr, start, end);
    printf(">%s\n", reg);
    int seq_len;
    char *seq = fai_fetch(fai, reg, &seq_len);
    if ( seq_len < 0 ) {
        err_printf("Failed to fetch sequence in %s\n", reg);
        exit_status = EXIT_FAILURE;
        return exit_status;
    }
    size_t i, seq_sz = seq_len;
    for (i=0; i<seq_sz; i+=60)
    {
        size_t len = i + 60 < seq_sz ? 60 : seq_sz - i;
        if (fwrite(seq + i, 1, len, stdout) < len ||
                putchar('\n') == EOF) {
            err_fatal_simple("failed to write output");
        }
    }
    free(seq);
    fai_destroy(fai);
    return exit_status;
}

int fxt_cigar_parse(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools cigar-parse <input-cigar>\n\n");
        return 1;
    }
    int cigar_len, i, seq_len, ref_len;
    int c;
    long x, op[11] = {0};
    char *s, *t;

    cigar_len = seq_len = ref_len = 0;
    for (s = argv[1]; *s; )
    {
        x = strtol(s, &t, 10);  
        /*if (x == 0)
        {
            fprintf(stderr, "%s\n",s);
            fprintf(stderr, "[fxtools cigar-parse] Cigar ERROR 1.\n");
            exit(-1);
        }*/
        c = toupper(*t);
        switch (c)
        {
            case 'M':   op[CMATCH]+=x, seq_len+=x, ref_len+=x;    break;
            case 'I':   op[CINS]+=x, seq_len+=x;      break;
            case 'D':   op[CDEL]+=x, ref_len+=x;      break;
            case 'N':   op[CREF_SKIP]+=x, ref_len+=x;     break;
            case 'S':   op[CSOFT_CLIP]+=x, seq_len+=x;    break;
            case 'H':   op[CHARD_CLIP]+=x;    break;
            case 'P':   op[CPAD]+=x;          break;
            case '=':   op[CEQUAL]+=x, seq_len+=x, ref_len+=x;    break;
            case 'X':   op[CDIFF]+=x, seq_len+=x, ref_len+=x; break;
            case 'B':   op[CBACK]+=x; break;  
			case 'V':	op[CINV]+=x, seq_len+=x, ref_len+=x;	break;
            default:    fprintf(stderr, "[fxtools cigar-parse] Cigar ERROR 2.\n"); exit(-1); break;
        }
        //modify variable directly OR use a auxiliary-variable
        ++cigar_len;
        s = t+1;
    }
    for (i = 0; i < 11; ++i)
    {
        if (op[i] != 0) fprintf(stdout, "%ld%c\t", op[i], CIGAR_STR[i]);
    }
    fprintf(stdout, "\nseq-len: %d\nref-len: %d\n", seq_len, ref_len);
    return 0;
}

int fxt_len_parse(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools length-parse <in.fa/fq>\n");
        fprintf(stderr, "\n"); 
        exit(-1);
    }
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_len_parse] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq;
    seq = kseq_init(infp);
    while (kseq_read(seq) >= 0)
    {
        fprintf(stdout, "%s\t%d\n", seq->name.s, (int)seq->seq.l);       
    }

    gzclose(infp);
    return 0;
}
int comp(const void *a, const void *b) {return (*(int*)a-*(int*)b); }

int fxt_merge_filter_fa(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools merge-fil-fa <in.fa> [N] > <out.fa/fq>\n");
        fprintf(stderr, "         optional: use N to separate merged sequences\n");
        fprintf(stderr, "         only work with fasta file.\n"); 
        fprintf(stderr, "\n");
        exit(-1);
    }
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_merge_fa] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq = kseq_init(infp);
    FILE *outfp = stdout;
    char read_name[1024]; 
    char *read_seq = (char*)calloc(10, 1);
    int w_seq_n=0;

    int i, j, i1=-1, i2=-1, n=0;
    int *len=(int*)_err_malloc(100 * sizeof(int)), *tmp=(int*)_err_malloc(100 * sizeof(int));
    char **name=(char**)_err_malloc(100 * sizeof(char*));
    for (i = 0; i < 100; i++) name[i] = (char*)_err_malloc(20 * sizeof(char));
    char sep[5]; 
    if (argc == 3) strcpy(sep, "N");
    else strcpy(sep, "");
    while (kseq_read(seq) >= 0)
    {
        if (strcmp(seq->name.s, read_name) == 0) {
            w_seq_n += seq->seq.l;
            strcpy(name[n], seq->comment.s);
            len[n++] = seq->seq.l;
            read_seq = (char*)realloc(read_seq, w_seq_n+seq->seq.l);
            strcat(read_seq, seq->seq.s);
            read_seq[w_seq_n] = 0;
        } else {
            if (w_seq_n > 0) {
                // cal i1 and i2
                i1 = -1, i2 = -1;
                for (i = 0; i < n; ++i) tmp[i] = len[i];
                qsort(tmp, n, sizeof(int), comp);
                if (n > 1 && n <= 10) {
                    fprintf(outfp, ">%s r1", read_name);
                    for (i = 0; i < n; ++i) {
                        if (len[i] == tmp[(n-1)/2]) {
                            i1 = i;
                            fprintf(outfp, " %s", name[i]);
                            break;
                        }
                    }
                    fprintf(outfp, "\n");
                } else if (n > 10) {
                    fprintf(outfp, ">%s r2", read_name);
                    for (i = 0; i < n; ++i) {
                        if (len[i] == tmp[(n-1)/3]) {
                            i1 = i;
                            fprintf(outfp, " %s", name[i]);
                        } else if (len[i] == tmp[(n-1)*2/3]){
                            i2 = i;
                            fprintf(outfp, " %s", name[i]);
                        }
                    }
                    fprintf(outfp, "\n");
                } else {
                    fprintf(outfp, ">%s\n", read_name);
                }
                // filter with i1 and i2
                int start = 0, end = 0, first = 0;
                for (i = 0; i < n; ++i) {
                    end += len[i];
                    if (i != i1 && i != i2) {
                        if (first) fprintf(outfp, "%s", sep);
                        for (j = start; j < end; ++j)
                            fprintf(outfp, "%c", read_seq[j]);
                        first = 1;
                    }
                    start = end;

                }
                fprintf(outfp, "\n");
            }
            n = 0;
            w_seq_n = seq->seq.l;
            strcpy(name[n], seq->comment.s);
            len[n++] = seq->seq.l;
            strcpy(read_name, seq->name.s);
            read_seq = (char*)realloc(read_seq, w_seq_n+seq->seq.l);
            strcpy(read_seq, seq->seq.s);
            read_seq[w_seq_n] = 0;
        }
    }
    if (w_seq_n > 0) { // last read
        // cal i1 and i2
        i1 = -1, i2 = -1;
        for (i = 0; i < n; ++i) tmp[i] = len[i];
        qsort(tmp, n, sizeof(int), comp);
        if (n > 1 && n < 10) {
            fprintf(outfp, ">%s r1", read_name);
            for (i = 0; i < n; ++i) {
                if (len[i] == tmp[(n-1)/2]) {
                    i1 = i;
                    fprintf(outfp, " %s", name[i]);
                    break;
                }
            }
            fprintf(outfp, "\n");
        } else if (n > 10){
            fprintf(outfp, ">%s r2", read_name);
            for (i = 0; i < n; ++i) {
                if (len[i] == tmp[(n-1)/3]) {
                    i1 = i;
                    fprintf(outfp, " %s", name[i]);
                } else if (len[i] == tmp[(n-1)*2/3]){
                    i2 = i;
                    fprintf(outfp, " %s", name[i]);
                }
            }
            fprintf(outfp, "\n");
        } else {
            fprintf(outfp, ">%s\n", read_name);
        }
        int start = 0, end = 0, first = 0;
        for (i = 0; i < n; ++i) {
            end += len[i];
            if (i != i1 && i != i2) {
                if (first) fprintf(outfp, "%s", sep);
                for (j = start; j < end; ++j)
                    fprintf(outfp, "%c", read_seq[j]);
                first = 1;
            }
            start = end;
        }
        fprintf(outfp, "\n");
    }
    free(name); free(len); free(tmp); gzclose(infp); fclose(outfp);
    return 0;
}

int fxt_merge_fa(int argc, char *argv[])
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: fxtools merge-fa <in.fa/fq> [N] > <out.fa/fq>\n");
        fprintf(stderr, "         optional: use N to separate merged sequences\n");
        fprintf(stderr, "\n");
        exit(-1);
    }
    gzFile infp;
    if (strcmp(argv[1],"-") == 0 || strcmp(argv[1], "stdin") == 0) infp = gzdopen(fileno(stdin), "r");
    else infp = gzopen(argv[1], "r");
    if (infp == NULL)
    {
        fprintf(stderr, "[fxt_merge_fa] Can't open %s.\n", argv[1]);
        exit(-1);
    }
    kseq_t *seq = kseq_init(infp);
    FILE *outfp = stdout;
    char read_name[1024]; 
    char *read_seq = (char*)calloc(10, 1);
    char *read_qual = (char*)calloc(10, 1);
    int w_seq_n=0;

    char sep[5];
    if (argc == 3) strcpy(sep, "N");
    else strcpy(sep, "");
    while (kseq_read(seq) >= 0)
    {
        if (strcmp(seq->name.s, read_name) == 0) {
            w_seq_n += seq->seq.l;
            read_seq = (char*)realloc(read_seq, w_seq_n+seq->seq.l);
            strcat(read_seq, sep);
            strcat(read_seq, seq->seq.s);
            read_seq[w_seq_n] = 0;
            if (seq->qual.l > 0) {
                read_qual = (char*)realloc(read_qual, w_seq_n+seq->seq.l);
                strcat(read_qual, "!");
                strcat(read_qual, seq->qual.s);
                read_qual[w_seq_n] = 0;
            }
        } else {
            if (w_seq_n > 0) {
                if (seq->qual.l > 0) { // fastq
                    fprintf(outfp, "@%s\n", read_name);
                    fprintf(outfp, "%s\n", read_seq);
                    fprintf(outfp, "+\n");
                    fprintf(outfp, "%s\n", read_qual);
                } else { // fasta
                    fprintf(outfp, ">%s\n", read_name);
                    fprintf(outfp, "%s\n", read_seq);
                }
            }
            w_seq_n = seq->seq.l;
            strcpy(read_name, seq->name.s);
            read_seq = (char*)realloc(read_seq, w_seq_n+seq->seq.l);
            strcpy(read_seq, seq->seq.s);
            read_seq[w_seq_n] = 0;
            if (seq->qual.l > 0) {
                read_qual = (char*)realloc(read_qual, w_seq_n+seq->seq.l);
                strcpy(read_qual, seq->qual.s);
                read_qual[w_seq_n] = 0;
            }
        }
    }
    if (w_seq_n > 0) { // last read
        if (seq->qual.l > 0) { // fastq
            fprintf(outfp, "@%s\n", read_name);
            fprintf(outfp, "%s\n", read_seq);
            fprintf(outfp, "+\n");
            fprintf(outfp, "%s\n", read_qual);
        } else { // fasta
            fprintf(outfp, ">%s\n", read_name);
            fprintf(outfp, "%s\n", read_seq);
        }
    }
    gzclose(infp);
    fclose(outfp);
    return 0;
}

#define bam_unmap(b) ((b)->core.flag & BAM_FUNMAP)

int fxt_error_parse(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "\n"); fprintf(stderr, "Usage: fxtools error-parse <input.bam> > error.out\n\n");
        return 1;
    }
    fprintf(stdout, "READ_NAME\tREAD_LEN\tUNMAP\tINS\tDEL\tMIS\tMATCH\tCLIP\tSKIP\n");
    long long tol_n=0, unmap=0, tol_len=0, tol_ins=0, tol_del=0, tol_mis=0, tol_match=0, tol_clip=0, tol_skip=0;
    int i, seq_len, unmap_flag=0, md, ins, del, mis, match, clip, skip;

    samFile *in; bam_hdr_t *h; bam1_t *b;
    if ((in = sam_open(argv[optind], "rb")) == NULL) err_fatal_core(__func__, "Cannot open \"%s\"\n", argv[optind]);
    if ((h = sam_hdr_read(in)) == NULL) err_fatal(__func__, "Couldn't read header for \"%s\"\n", argv[optind]);
    b = bam_init1(); 

    while (sam_read1(in, h, b) >= 0) {
        tol_n++;
        unmap_flag = 0;
        seq_len = b->core.l_qseq;
        md = 0, ins = 0, del = 0, mis = 0, match = 0, clip = 0, skip = 0;
        if (!bam_unmap(b)) {
            uint32_t *cigar = bam_get_cigar(b); int cigar_len = b->core.n_cigar;
            for (i = 0; i < cigar_len; ++i) {
                uint32_t c = cigar[i];
                int len = bam_cigar_oplen(c);
                switch (bam_cigar_op(c)) {
                    case BAM_CMATCH: match += len; break;
                    case BAM_CINS: ins += len; break;
                    case BAM_CDEL: del += len; break;
                    case BAM_CREF_SKIP: skip += len; break;
                    case BAM_CSOFT_CLIP: clip += len; break;
                    case BAM_CHARD_CLIP: clip += len; break;
                    default : err_fatal_simple("Cigar ERROR.\n");
                }
            }
            uint8_t *p = bam_aux_get(b, "NM");
            if (p == 0) p = bam_aux_get(b, "nM");
            if (p == 0) {
                err_fatal_core(__func__, "%s No \"NM\" tag.\n", bam_get_qname(b));
                return 0;
            }
            md = bam_aux2i(p);
            mis = md - ins - del;
            match = match - mis;
        } else {
            unmap++;
            unmap_flag = 1;
        }
        tol_len += seq_len; tol_ins += ins; tol_del += del; tol_mis += mis; tol_match += match; tol_clip += clip; tol_skip += skip;

        fprintf(stdout, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", bam_get_qname(b), seq_len, unmap_flag, ins, del, mis, match, clip, skip);
    }
    fprintf(stdout, "%s\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n", "Total", tol_len, unmap, tol_ins, tol_del, tol_mis, tol_match, tol_clip, tol_skip);
    fprintf(stdout, "Total mapped read: %lld\nTotal unmapped read: %lld\nTotal read: %lld\n", tol_n-unmap, unmap, tol_n);
    return 0;
}

int get_peak_seq(bam1_t *b, int is_rev, int start, int end, int *s, int *e)
{
    int n_cigar = b->core.n_cigar; uint32_t *c = bam_get_cigar(b);
    int len = 0, i, read_len = b->core.l_qseq;
    int read_i = 1, ref_i = b->core.pos+1;
    *s = 0, *e = 0;
    for (i = 0; i < n_cigar; ++i) {
        int l = bam_cigar_oplen(c[i]);
        switch (bam_cigar_op(c[i])) {
            case BAM_CDEL : // D(0 1)
            case BAM_CREF_SKIP: // N(0 1)
                ref_i += l;
                if (*s == 0 && ref_i-1 >= start) {
                    *s = read_i;
                }
                if (*e == 0 && ref_i-1 >= end) {
                    *e = read_i-1;
                }
                break;
            case BAM_CMATCH: // 1 1
            case BAM_CEQUAL:
            case BAM_CDIFF:
                ref_i += l;
                read_i += l;
                if (*s == 0 && ref_i-1 >= start) {
                    *s = read_i - (ref_i-start);
                }
                if (*e == 0 && ref_i-1 >= end) {
                    *e = read_i - (ref_i-end);
                }
                break;
            case BAM_CINS: // 1 0
            case BAM_CSOFT_CLIP:
            case BAM_CHARD_CLIP:
                read_i += l;
                break;

            default:
                err_printf("Error: unknown cigar type: %d.\n", bam_cigar_op(c[i]));
                break;
        }
    }
    if (is_rev) {
        int tmp = read_len+1 - *e;
        *e = read_len+1 - *s;
        *s = tmp;
    }
    len = *e - *s + 1;
    return len;
}

//argv[1]: bam
//argv[2]: peak pos
int fxt_peak_seq(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "\n"); 
        fprintf(stderr, "Usage: fxtools peak-seq <input.bam> <input.peak> > peak_seq.out\n\n");
        return 1;
    }
    char bamfn[1024], peakfn[1024], reg[1024], rname[1024];
    samFile *in; hts_idx_t *idx; bam_hdr_t *h; bam1_t *b; FILE *peakfp;
    strcpy(bamfn, argv[1]); strcpy(peakfn, argv[2]);
    if ((in = sam_open(bamfn, "rb")) == NULL) err_fatal(__func__, "fail to open \"%s\"\n", bamfn);
    if ((h = sam_hdr_read(in)) == NULL) err_fatal(__func__, "fail to read header for \"%s\"\n", bamfn);
    if ((idx = sam_index_load(in, bamfn)) == NULL) err_fatal(__func__, "fail to load the BAM index for \"%s\"\n", bamfn);
    b = bam_init1();

    if ((peakfp = fopen(peakfn, "r")) == NULL) err_fatal(__func__, "fail to open \"%s\"\n", peakfn); 

    int start, end, s, e;

    int r, xs_rev, is_rev; char chr[1024];
    while (fgets(reg, 1024, peakfp) != NULL) {
        sscanf(reg, "%s %d %d", chr, &start, &end);
        sprintf(reg, "%s:%d-%d", chr, start, end);
        hts_itr_t *itr = sam_itr_querys(idx, h, reg);
        while ((r = sam_itr_next(in, itr, b)) >= 0) {
            strcpy(rname, bam_get_qname(b));
            uint8_t *p;
            p = bam_aux_get(b, "XS"); // strand orientation for a splice
            is_rev = bam_is_rev(b);
            if (p == 0) {
                xs_rev = is_rev;
            } else {
                xs_rev = ((bam_aux2A(p) == '+' )? 0 : 1);
            }

            is_rev = bam_is_rev(b);

            int len = get_peak_seq(b, is_rev, start, end, &s, &e);
            if (len >= 30) 
                fprintf(stdout, "%s\t%c\t%c\t%d\t%d\t%d\t%s\t%d\t%d\n", rname, "+-"[xs_rev], "+-"[is_rev], len, s, e, chr, start, end);
        }
    }
    hts_idx_destroy(idx); bam_hdr_destroy(h); sam_close(in); bam_destroy1(b);
    fclose(peakfp);
    return 0;
}

int get_motif_seq(bam1_t *b, bam_hdr_t *h, char *ref, int is_rev, char motif[]) {
    int ref_s, ref_e, read_s, read_e;
    // cigar, MD, seq
    uint8_t *seq = bam_get_seq(b); uint32_t *cigar = bam_get_cigar(b); bam1_core_t c = b->core;
    int len = strlen(motif);
    int i, j, k, ref_i, read_i;
    for (i = 0, read_i = 0, ref_i = c.pos; i < c.n_cigar; ++i) {
        int l = cigar[i]>>4, op=cigar[i]&0xf;
        if (op == BAM_CMATCH || op == BAM_CEQUAL) { // ref:1, read:1
            if (l >= len) {
                for (j = 0; j <= l-len; ++j) {
                    if (strncmp(ref+j, motif, len) == 0) {
                        int unmatch = 0;
                        for (k = 0; k < len; ++k) {
                            if ("=ACMGRSVTWYHKDBN"[bam_seqi(seq, read_i+j+k)] != ref[j+k]) {
                                unmatch = 1;
                            }
                        }
                        if (unmatch == 0) {
                            ref_s = ref_i+j+1, read_s = read_i+j+1;
                            ref_e = ref_s+len-1, read_e = read_s+len-1;
                            fprintf(stdout, "%s\t%c\t%d\t%d\t%d\t%s\t%d\t%d\n", bam_get_qname(b), "+-"[is_rev], len, read_s, read_e, h->target_name[b->core.tid], ref_s, ref_e);
                        }
                    }
                }
            }
            read_i += l, ref_i += l;
        } else if (op == BAM_CDIFF) {
            read_i += l, ref_i += l;
        } else if (op == BAM_CDEL || op == BAM_CREF_SKIP) { // ref:1, read:0
            ref_i += l;
        } else if (op == BAM_CINS || op == BAM_CSOFT_CLIP) { // ref:0, read:1
            read_i += l;
        }
    }
    return 0;
}

//argv[1]: bam
//argv[2]: ref.fa
int fxt_motif_seq(int argc, char *argv[])
{
   char bamfn[1024], motif[10], rc_motif[10], rname[1024];
   samFile *in; bam_hdr_t *h; bam1_t *b;
   size_t i;
   int c;
   strcpy(motif, "GGACT");
   while ((c = getopt(argc, argv, "m:")) >= 0) {
       switch (c) {
           case 'm': strcpy(motif, optarg); break;
           default: err_printf("Unknown option: '-%c'\n\n", c); break;
       }
   }
   for (i = 0; i < strlen(motif); ++i) {
       rc_motif[strlen(motif)-1-i] = nt_char[3-nt_table[(int)motif[i]]];
   }

   if (argc-optind != 2) {
        fprintf(stderr, "\n"); 
        fprintf(stderr, "Usage: fxtools motif-seq <input.bam> [options] > motif_seq.out\n\n");
        fprintf(stderr, "Options:       -m        motif sequence. [GGACT]\n");
        return 1;
   }
   strcpy(bamfn, argv[optind]);
   if ((in = sam_open(bamfn, "rb")) == NULL) err_fatal(__func__, "fail to open \"%s\"\n", bamfn);
   if ((h = sam_hdr_read(in)) == NULL) err_fatal(__func__, "fail to read header for \"%s\"\n", bamfn);
   b = bam_init1();
   faidx_t *fai = fai_load(argv[optind+1]);
   if ( !fai ) {
       fprintf(stderr, "Could not load fai index of %s\n", argv[2]);
       fprintf(stderr, "Building fai index of %s\n", argv[2]);
       if (fai_build(argv[1]) != 0) {
           fprintf(stderr, "Could not build fai index %s.fai\n", argv[2]);
           return EXIT_FAILURE;
       }
   }
   
   int r, xs_rev, is_rev;
   while ((r = sam_read1(in, h, b)) >= 0) {
       strcpy(rname, bam_get_qname(b));
       uint8_t *p;
       p = bam_aux_get(b, "XS"); // strand orientation for a splice
       is_rev = bam_is_rev(b);
       if (p == 0) {
           xs_rev = is_rev;
       } else {
           xs_rev = ((bam_aux2A(p) == '+' )? 0 : 1);
       }

       is_rev = bam_is_rev(b);

       // fetch ref sequence
       char reg[1024];
       sprintf(reg, "%s:%d-%d", h->target_name[b->core.tid], b->core.pos+1, b->core.pos+bam_cigar2rlen(b->core.n_cigar, bam_get_cigar(b)));
       int seq_len;
       char *seq = fai_fetch(fai, reg, &seq_len);
       if ( seq_len < 0 ) {
           err_printf("Failed to fetch sequence in %s\n", reg);
           return 0;
       }
       int len;
       len = is_rev ? get_motif_seq(b, h, seq, is_rev, rc_motif) : get_motif_seq(b, h, seq, is_rev, motif);
       free(seq);
   }
   fai_destroy(fai);
   bam_hdr_destroy(h); sam_close(in); bam_destroy1(b);
   return 0;
}

int main(int argc, char*argv[])
{
    if (argc < 2) return usage();
    if (strcmp(argv[1], "filter") == 0 || strcmp(argv[1], "fl") == 0) fxt_filter(argc-1, argv+1);
    else if (strcmp(argv[1], "filter-name") == 0 || strcmp(argv[1], "fn") == 0) fxt_filter_name(argc-1, argv+1);
    else if (strcmp(argv[1], "filter-bam") == 0 || strcmp(argv[1], "fb") == 0) fxt_filter_bam(argc-1, argv+1);
    else if (strcmp(argv[1], "filter-bam-name") == 0 || strcmp(argv[1], "fbn") == 0) fxt_filter_bam_name(argc-1, argv+1);
    else if (strcmp(argv[1], "fq2fa") == 0 || strcmp(argv[1], "qa") == 0) fxt_fq2fa(argc-1, argv+1);
    else if (strcmp(argv[1], "fa2fq") == 0 || strcmp(argv[1], "aq") == 0) fxt_fa2fq(argc-1, argv+1);
    else if (strcmp(argv[1], "re-co") == 0 || strcmp(argv[1], "rc") == 0) fxt_re_co(argc-1, argv+1);
    else if (strcmp(argv[1], "seq-display") == 0 || strcmp(argv[1], "sd") == 0) fxt_seq_dis(argc-1, argv+1);
    else if (strcmp(argv[1], "cigar-parse") == 0 || strcmp(argv[1], "cp") == 0) fxt_cigar_parse(argc-1, argv+1);
    else if (strcmp(argv[1], "length-parse") == 0 || strcmp(argv[1], "lp") == 0) fxt_len_parse(argc-1, argv+1);
    else if (strcmp(argv[1], "merge-fa") == 0 || strcmp(argv[1], "mf") == 0) fxt_merge_fa(argc-1, argv+1);
    else if (strcmp(argv[1], "merge-filter-fa") == 0 || strcmp(argv[1], "mff") == 0) fxt_merge_filter_fa(argc-1, argv+1);
    else if (strcmp(argv[1], "error-parse") == 0 || strcmp(argv[1], "ep") == 0) fxt_error_parse(argc-1, argv+1);
    else if (strcmp(argv[1], "dna2rna") == 0 || strcmp(argv[1], "dr") == 0) fxt_dna2rna(argc-1, argv+1);
    else if (strcmp(argv[1], "rna2dna") == 0 || strcmp(argv[1], "rd") == 0) fxt_rna2dna(argc-1, argv+1);
    else if (strcmp(argv[1], "peak-seq") == 0 || strcmp(argv[1], "ps") == 0) fxt_peak_seq(argc-1, argv+1);
    else if (strcmp(argv[1], "motif-seq") == 0 || strcmp(argv[1], "ms") == 0) fxt_motif_seq(argc-1, argv+1);
    else {fprintf(stderr, "unknow command [%s].\n", argv[1]); return 1; }

    return 0;
}
