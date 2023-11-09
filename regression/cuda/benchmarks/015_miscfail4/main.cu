//xfail:BOOGIE_ERROR
//main.cu: error: possible read-write race
//however, this didn't happen in the tests
// In CUDA providing static and __attribute__((always_inline)) SHOUD NOT
// keep a copy of inlined function around.
//ps: the values from A[N-1-offset] to A[N-1] always will receive unpredictable values,
//because they acess values because they access memory positions that were not initiated

#include <stdio.h>
#include <cuda_runtime_api.h>
#define tid threadIdx.x
#define N 2//32

__device__ static __attribute__((always_inline))
void inlined(int *A, int offset)
{
   int temp = A[tid + offset];
   A[tid] += temp;
}

__global__ void inline_test(int *A, int offset) {
  inlined(A, offset);
}

int main(){

	int *a;
	int *dev_a;
	int size = N*sizeof(int);

	cudaMalloc((void**)&dev_a, size);

	a = (int*)malloc(N*size);

	for (int i = 0; i < N; i++)
		a[i] = i;

	cudaMemcpy(dev_a,a,size, cudaMemcpyHostToDevice);

	printf("a:  ");
	for (int i = 0; i < N; i++)
		printf("%d	", a[i]);

	//inline_test<<<1,N>>>(dev_a, 2);		//you can change this offset for tests
	ESBMC_verify_kernel_intt(inline_test, 1, N, dev_a, 1);

	cudaMemcpy(a,dev_a,size,cudaMemcpyDeviceToHost);

	printf("\nFunction Results:\n   ");

	for (int i = 0; i < N; i++)
		printf("%d	", a[i]);

	free(a);

	cudaFree(dev_a);

	return 0;
}
