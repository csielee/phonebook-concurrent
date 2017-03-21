#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    FILE *fp = fopen("orig.txt", "r");
    FILE *output = fopen("output.txt", "w");
    FILE *opt2 = fopen("opt2.txt", "w");
    if (!fp) {
        printf("ERROR opening input file orig.txt\n");
        exit(0);
    }
    double orig_sum_i = 0.0 ,orig_sum_a = 0.0, orig_sum_f = 0.0, orig_sum_fr = 0.0;
    double orig_i,orig_a, orig_f,orig_fr;
    for (int i = 0; i < 100; i++) {
        if (feof(fp)) {
            printf("ERROR: You need 100 datum instead of %d\n", i);
            printf("run 'make run' longer to get enough information\n\n");
            exit(0);
        }
        fscanf(fp, "%lf %lf %lf %lf\n", &orig_i,&orig_a, &orig_f, &orig_fr);
        orig_sum_i += orig_i;
        orig_sum_a += orig_a;
        orig_sum_f += orig_f;
        orig_sum_fr += orig_fr;
    }
    fclose(fp);

    fp = fopen("opt.txt", "r");
    if (!fp) {
        fp = fopen("orig.txt", "r");
        if (!fp) {
            printf("ERROR opening input file opt.txt\n");
            exit(0);
        }
    }
    double opt_sum_i = 0.0, opt_sum_a = 0.0, opt_sum_f = 0.0, opt_sum_fr = 0.0;
    double opt_i, opt_a, opt_f,opt_fr;
    for (int i = 0; i < 100; i++) {
        if (feof(fp)) {
            printf("ERROR: You need 100 datum instead of %d\n", i);
            printf("run 'make run' longer to get enough information\n\n");
            exit(0);
        }
        fscanf(fp, "%lf %lf %lf %lf\n", &opt_i, &opt_a, &opt_f, &opt_fr);
        opt_sum_i += opt_i;
        opt_sum_a += opt_a;
        opt_sum_f += opt_f;
        opt_sum_fr += opt_fr;
        fprintf(opt2, "%d %lf %lf %lf %lf\n" , i+1, opt_i, opt_a, opt_f, opt_fr);
    }
    fprintf(output, "create() %lf %lf\n", orig_sum_i / 100.0,
            opt_sum_i / 100.0);
    fprintf(output, "appendByFile() %lf %lf\n", orig_sum_a / 100.0,
            opt_sum_a / 100.0);
    fprintf(output, "findName() %lf %lf\n", orig_sum_f / 100.0,
            opt_sum_f / 100.0);
    fprintf(output, "free() %lf %lf", orig_sum_fr / 100.0,
            opt_sum_fr / 100.0);
    fclose(output);
    fclose(opt2);
    fclose(fp);
    return 0;
}
