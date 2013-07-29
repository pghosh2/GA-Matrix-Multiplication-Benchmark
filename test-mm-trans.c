/* Sample matrix-matrix multiplication */

#include "ga.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "macdecls.h"
#include <mpi.h>

//#include "/usr/include/gsl/gsl_cblas.h"
#include <cblas.h>

#define   NDIMS   2

/* Naive dgemm */
/* void _my_dgemm_ (double *a, int lda, double *b, int ldb, double *c, int ldc, int M, int N, int K, double alpha, double beta) */
void _my_dgemm_ (double *a, int lda, double *b, int ldb, 
		double *c, int ldc, int M, int N, int K, 
		double alpha, double beta)
{
	for(int i = 0; i < M; i++) 
	{
		for(int j = 0; j < N; j++) 
		{
			c[i*ldc + j] = beta * c[i*ldc + j];

			for(int k = 0; k < K; k++) 
			{
				c[i*ldc + j] += alpha * (a[i*lda + k] * b[k*ldb + j]);
			}
		} 
	}
}

/* Correctness test */
void verify(int g_a, int g_b, int g_c, int *lo, int *hi, int *ld, int N);

/* Square matrix-matrix multiplication */
void matrix_multiply(int M, int N, int K, 
		int blockX_len, int blockY_len) 
{
	/* Local buffers and Global arrays declaration */
	double *a=NULL, *b=NULL, *c=NULL, *btrans=NULL;

	int dims[NDIMS], ld[NDIMS], chunks[NDIMS];
	int lo[NDIMS], hi[NDIMS], cdims[NDIMS]; /* dim of blocks */

	int g_a, g_b, g_c, g_cnt, g_cnt2;
	int offset;
	double alpha = 1.0, beta=0.0;
	int count_p = 0, next_p = 0;
	int count_gac = 0, next_gac = 0;
	double t1,t2,seconds;
        int count_acc = 0;
        int invert_b = 0;
        int i = 0, j=0;

	/* Find local processor ID and the number of processes */
	int proc=GA_Nodeid(), nprocs=GA_Nnodes();

	if ((M % blockX_len) != 0 || (M % blockY_len) != 0 || (N % blockX_len) != 0 || (N % blockY_len) != 0 
			|| (K % blockX_len) != 0 || (K % blockY_len) != 0)
		GA_Error("Dimension size M/N/K is not divisible by X/Y block sizes", 101);

	/* Allocate/Set process local buffers */
	a = malloc (blockX_len * blockY_len * sizeof(double)); 
	b = malloc (blockX_len * blockY_len * sizeof(double)); 
	btrans = malloc (blockX_len * blockY_len * sizeof(double)); 
	c = malloc (blockX_len * blockY_len * sizeof(double));

	cdims[0] = blockX_len;
	cdims[1] = blockY_len;	

	/* Configure array dimensions */
	for(int i = 0; i < NDIMS; i++) {
		dims[i]  = N;
		chunks[i] = -1;
		ld[i]    = cdims[i]; /* leading dimension/stride of the local buffer */
	}

	/* create a global array g_a and duplicate it to get g_b and g_c*/
	g_a = NGA_Create(C_DBL, NDIMS, dims, "array A", chunks);

	if (!g_a) 
		GA_Error("NGA_Create failed: A", NDIMS);

#if DEBUG>1
	if (proc == 0) 
		printf("  Created Array A\n");
#endif
	/* Ditto for C and B */
	g_b = GA_Duplicate(g_a, "array B");
	g_c = GA_Duplicate(g_a, "array C");

	if (!g_b || !g_c) 
		GA_Error("GA_Duplicate failed",NDIMS);
	if (proc == 0) 
		printf("Created Arrays B and C\n");

	/* Subscript array for read-incr, which is nothing but proc */
	int * rdcnt = malloc (nprocs * sizeof(int));
	memset (rdcnt, 0, nprocs * sizeof(int));
	int * rdcnt2 = malloc (nprocs * sizeof(int));
	memset (rdcnt2, 0, nprocs * sizeof(int));

	/* Create global array of nprocs elements for nxtval */	
	int counter_dim[1];
	counter_dim[0] = nprocs;

	g_cnt = NGA_Create(C_INT, 1, counter_dim, "Shared counter", NULL);

	if (!g_cnt) 
		GA_Error("Shared counter failed",1);

	g_cnt2 = GA_Duplicate(g_cnt, "another shared counter");

	if (!g_cnt2) 
		GA_Error("Another shared counter failed",1);

	GA_Zero(g_cnt);
	GA_Zero(g_cnt2);

#if DEBUG>1	
	/* initialize data in matrices a and b */
	if(proc == 0)
		printf("Initializing local buffers - a and b\n");
#endif
	int w = 0; 
	int l = 7;
	for(int i = 0; i < cdims[0]; i++) {
		for(int j = 0; j < cdims[1]; j++) {
			a[i*cdims[1] + j] = (double)(++w%29);
			b[i*cdims[1] + j] = (double)(++l%37);
		}
	}

	/* Copy data to global arrays g_a and g_b from local buffers */
	next_p = NGA_Read_inc(g_cnt2,&rdcnt[proc],(long)1);
	for (int i = 0; i < N; i+=cdims[0]) 
	{
		if (next_p == count_p) {
			for (int j = 0; j < N; j+=cdims[1])
			{
				/* Indices of patch */
				lo[0] = i;
				lo[1] = j;
				hi[0] = lo[0] + cdims[0];
				hi[1] = lo[1] + cdims[1];

				hi[0] = hi[0]-1;
				hi[1] = hi[1]-1;
#if DEBUG>1
				printf ("%d: PUT_GA_A_B: lo[0,1] = %d,%d and hi[0,1] = %d,%d\n",proc,lo[0],lo[1],hi[0],hi[1]);
#endif
				NGA_Put(g_a, lo, hi, a, ld);
				NGA_Put(g_b, lo, hi, b, ld);

			}
			next_p = NGA_Read_inc(g_cnt2,&rdcnt[proc],(long)1);
		}		
		count_p++;
	}


#if DEBUG>1
	printf ("After NGA_PUT to global - A and B arrays\n");
#endif
	/* Synchronize all processors to make sure puts from 
	   nprocs has finished before proceeding with dgemm */
	GA_Sync();

	t1 = GA_Wtime();

	next_gac = NGA_Read_inc(g_cnt,&rdcnt2[proc],(long)1);
	for (int m = 0; m < N; m+=cdims[0])
	{
		for (int n = 0; n < N; n+=cdims[1])
		{
			if (next_gac == count_gac)	
			{
				memset (c, 0, sizeof(double) * cdims[0] * cdims[1]);
				for (int k = 0; k < N; k+=cdims[0])
				{
					/* A = m x k */
					lo[0] = m; lo[1] = k;
					hi[0] = cdims[0] + lo[0]; hi[1] = cdims[1] + lo[1];

					hi[0] = hi[0]-1; hi[1] = hi[1]-1;
#if DEBUG>3
					printf ("%d: GET GA_A: lo[0,1] = %d,%d and hi[0,1] = %d,%d\n",proc,lo[0],lo[1],hi[0],hi[1]);
#endif
					NGA_Get(g_a, lo, hi, a, ld);

					/* B = k x n */
					lo[0] = k; lo[1] = n;
					hi[0] = cdims[0] + lo[0]; hi[1] = cdims[1] + lo[1];				

					hi[0] = hi[0]-1; hi[1] = hi[1]-1;
#if DEBUG>3
					printf ("%d: GET_GA_B: lo[0,1] = %d,%d and hi[0,1] = %d,%d\n",proc,lo[0],lo[1],hi[0],hi[1]);
#endif
					NGA_Get(g_b, lo, hi, b, ld);
                                        /* inverting data locally */
                                        invert_b = hi[0] - lo[0] + 1;
                                        for (i=0; i<invert_b; i++) 
                                          for (j=0; j<invert_b; j++)
                                               btrans[j*ld[0]+i] = b[i*ld[0]+j];

					/*
					_my_dgemm_ (a, local_N, b, local_N, c, local_N, local_N, local_N, local_N, alpha, beta=1.0);
					*/
					/* TODO I am assuming square matrix blocks, further testing/work 
					   required for rectangular matrices */
					cblas_dgemm ( CblasRowMajor, CblasNoTrans, /* TransA */CblasTrans, /* TransB */
							cdims[0] /* M */, cdims[1] /* N */, cdims[0] /* K */, alpha,
							a, cdims[0], /* lda */ btrans, cdims[1], /* ldb */
							beta=1.0, c, cdims[0] /* ldc */);

				} /* END LOOP K */
				/* C = m x n */
				lo[0] = m; lo[1] = n;
				hi[0] = cdims[0] + lo[0]; hi[1] = cdims[1] + lo[1];				

				hi[0] = hi[0]-1; hi[1] = hi[1]-1;
#if DEBUG>3
				printf ("%d: ACC_GA_C: lo[0,1] = %d,%d and hi[0,1] = %d,%d\n",proc,lo[0],lo[1],hi[0],hi[1]);
#endif
				NGA_Acc(g_c, lo, hi, c, ld, &alpha);
                                count_acc += 1;
				next_gac = NGA_Read_inc(g_cnt,&rdcnt2[proc],(long)1);
			} /* ENDIF if count == next */
			count_gac++;
		} /* END LOOP N */
	} /* END LOOP M */

	GA_Sync();
	t2 = GA_Wtime();
	seconds = t2 - t1;
	if (proc == 0)
		printf("Time taken for MM (secs):%lf \n", seconds);

        printf("Number of ACC: %d\n", count_acc);

	/* Correctness test - modify data again before this function */
	for (int i = 0; i < NDIMS; i++) {
		lo[i] = 0;
		hi[i] = dims[i]-1;
		ld[i] = dims[i];
	}

	verify(g_a, g_b, g_c, lo, hi, ld, N);

	/* Clear local buffers */
	free(a);
	free(b);
	free(btrans);
	free(c);
	free(rdcnt);
	free(rdcnt2);

	GA_Sync();

	/* Deallocate arrays */
	GA_Destroy(g_a);
	GA_Destroy(g_b);
	GA_Destroy(g_c);
	GA_Destroy(g_cnt);
	GA_Destroy(g_cnt2);
}

/*
 * Check to see if inversion is correct.
 */
#define TOLERANCE 0.1
void verify(int g_a, int g_b, int g_c, int *lo, int *hi, int *ld, int N) 
{

	double rchk, alpha=1.0, beta=0.0;
	int g_chk, me=GA_Nodeid();

	g_chk = GA_Duplicate(g_a, "array Check");
	if(!g_chk) GA_Error("duplicate failed",NDIMS);
	GA_Sync();

	GA_Dgemm('n', 'n', N, N, N, 1.0, g_a,
			g_b, 0.0, g_chk);

	GA_Sync();

	alpha=1.0, beta=-1.0;
	GA_Add(&alpha, g_c, &beta, g_chk, g_chk);
	rchk = GA_Ddot(g_chk, g_chk);

	if (me==0) {
		printf("Normed difference in matrices: %12.4e\n", rchk);
		if(rchk < -TOLERANCE || rchk > TOLERANCE)
			GA_Error("Matrix multiply verify failed",0);
		else
			printf("Matrix Mutiply OK\n");
	}

	GA_Destroy(g_chk);
}


int main(int argc, char **argv) 
{
	int proc, nprocs;
	int M, N, K; /* */
	int blockX_len, blockY_len;
	int heap=3000000, stack=3000000;

	if (argc == 6) {
		M = atoi(argv[1]);
		N = atoi(argv[2]);
		K = atoi(argv[3]);
		blockX_len = atoi(argv[4]);
		blockY_len = atoi(argv[5]);
	}
	else {
		printf("Please enter ./a.out <M> <N> <K> <BLOCK-X-LEN> <BLOCK-Y-LEN>");
		exit(-1);
	}

	MPI_Init(&argc, &argv);

	GA_Initialize();

	if(! MA_init(C_DBL, stack, heap) ) GA_Error("MA_init failed",stack+heap);

	proc   = GA_Nodeid();
	nprocs = GA_Nnodes();

	if(proc == 0) {
		printf("Using %d processes\n", nprocs); 
		fflush(stdout);
	}

	matrix_multiply(M, N, K, blockX_len, blockY_len);

	if(proc == 0)
		printf("\nTerminating ..\n");

	GA_Terminate();

	MPI_Finalize();    

	return 0;
}
