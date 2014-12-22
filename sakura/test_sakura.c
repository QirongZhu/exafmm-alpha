#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "utils.h"
#include "cilk/cilk.h"
#include "cilk/cilk_api.h"
/* additional includes */
#include <asm-generic/unistd.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define DIM 3
#define LDIM 12
#define NP3 64
#define NN 26
#define FN 37
#define CN 152
#define ML 1000000

int main(int argc, char** argv){
  struct Body *data, *permuted_data, *data2, *permuted_data2;
  const bool printNow = true;
  if (argc != 6) {
    printf("Usage: %s N dist pop rep P\n"
	   " where\n"
	   " N    : number of points\n"
	   " dist : distribution code (1-3)\n"
	   " pop  : population threshold\n"
	   " rep  : repetitions\n"
	   " P    : number of threads.\n", argv[0]);
    return (1);
  }
  int N = atoi(argv[1]); // Number of points
  int dist = atoi(argv[2]); // Distribution identifier 
  int population_threshold = atoi(argv[3]);
  int repeat = atoi(argv[4]);
  int nworkers = atoi(argv[5]);
  __cilkrts_set_param("nworkers",argv[5]); // the number of working threads
  printf("N = %d, T=%d\n", N, nworkers);
  start_timer();
  float *X = (float *)sakura_malloc(N, LDIM*sizeof(float), "Particle array");
  uint64_t particle_memory = (uint64_t) N*LDIM*sizeof(float);
  float *X2 = (float *)sakura_malloc(N, LDIM*sizeof(float), "Particle array");
  particle_memory += (uint64_t) N*LDIM*sizeof(float);
  stop_timer("Data mem. alloc.");
  printf("%-20s:   %luMB\n", "Particle mem", particle_memory / ML);
  int maxlev = 20; // Maximum level of the tree
  int maxheight = 20; // Maximun height of the tree (extra control for debugin)
  int nbins = (1 << maxlev); // maximum number of boxes at the leaf level
  for (int it=0; it<repeat; it++) {
    start_timer();
    create_dataset_TL(X, N, dist);
    create_dataset_TL(X2, N, dist);
    stop_timer("Create data");
    start_timer();
    float min[DIM], max[DIM];
    for(int i=0; i<DIM; i++){
      min[i] = __sec_reduce_min(X[i:N:LDIM]);
      max[i] = __sec_reduce_max(X[i:N:LDIM]);
    }
    float min2[DIM], max2[DIM];
    for(int i=0; i<DIM; i++){
      min2[i] = __sec_reduce_min(X2[i:N:LDIM]);
      max2[i] = __sec_reduce_max(X2[i:N:LDIM]);
    }
    min[:] = MIN(min[:], min2[:]);
    max[:] = MAX(max[:], max2[:]);
    stop_timer("Box bounds");
    uint64_t *particle_codes = (uint64_t *)sakura_malloc(N, sizeof(uint64_t), "Morton code array");
    uint32_t *bit_map = (uint32_t *)sakura_calloc(N, sizeof(uint32_t), "Bit map");
    uint32_t *permutation_vector = (uint32_t *)sakura_malloc(N, sizeof(uint32_t),"Permutation vector");
    uint64_t *particle_codes2 = (uint64_t *)sakura_malloc(N, sizeof(uint64_t), "Morton code array");
    uint32_t *bit_map2 = (uint32_t *)sakura_calloc(N, sizeof(uint32_t), "Bit map");
    uint32_t *permutation_vector2 = (uint32_t *)sakura_malloc(N, sizeof(uint32_t),"Permutation vector");
    encodeParticles(N, X, min, 
		    max, particle_codes, 
		    maxlev);
    decomposeSpace(N, (void **)&particle_codes, 
		   permutation_vector, (void*)bit_map, &X,
		   maxlev, population_threshold, dist);
    relocateParticles(N, &X, permutation_vector);

    encodeParticles(N, X2, min, max, particle_codes2, maxlev);
    decomposeSpace(N, (void**)&particle_codes2, 
		   permutation_vector2, (void*)bit_map2, &X2,
		   maxlev, population_threshold, dist);
    relocateParticles(N, &X2, permutation_vector2);

    int nodes_per_level[20];
    int **node_pointers = (int **)malloc(maxlev*sizeof(int *)); 
    int **num_children = (int **)malloc(maxlev*sizeof(int *)); 
    int **children_first = (int **)malloc(maxlev*sizeof(int *)); 
    int **node_codes = (int **)malloc(maxlev*sizeof(int *));
    int nodes_per_level2[20];
    int **node_pointers2 = (int **)malloc(maxlev*sizeof(int *)); 
    int **num_children2 = (int **)malloc(maxlev*sizeof(int *)); 
    int **children_first2 = (int **)malloc(maxlev*sizeof(int *)); 
    int **node_codes2 = (int **)malloc(maxlev*sizeof(int *));
    int height = tree_formation((void *)bit_map, (void *)particle_codes, 
				nodes_per_level, node_pointers, 
				num_children, children_first, 
				(void**)node_codes, maxlev, N);
    int height2 = tree_formation((void *)bit_map2, (void *)particle_codes2, 
				 nodes_per_level2, node_pointers2, 
				 num_children2, children_first2, 
				 (void**)node_codes2, maxlev, N);
    int **clgs_link_list = (int **)malloc(height*sizeof(int *)); 
    int **nn_link_list = (int **)malloc(height*sizeof(int *)); 
    int **common_list = (int **)malloc(height*sizeof(int *));
    uint32_t **nn_count = (uint32_t **)malloc(height*sizeof(uint32_t *)); 
    uint32_t **clgs_count = (uint32_t **)malloc(height*sizeof(uint32_t *)); 
    uint32_t **common_count = (uint32_t **)malloc(height*sizeof(uint32_t *));
    form_interaction_lists(node_codes, children_first,
			   node_codes2, children_first2, 
			   nn_count, 
			   clgs_count, 
			   common_count,
			   nn_link_list, 
			   clgs_link_list,
			   common_list,
			   NULL,
			   NULL,
			   NULL,
			   node_pointers, 
			   nodes_per_level, 
			   nodes_per_level2, 
			   height, 
			   height2, 
			   N);
    verify_all(node_pointers, 
	       node_pointers2,
	       children_first, 
	       children_first2,
	       nodes_per_level, nodes_per_level2,
	       bit_map, bit_map2,
	       clgs_link_list,
	       nn_link_list,
	       common_list,
	       nn_count,
	       clgs_count,
	       common_count,
	       height, height2, N);
    for(int i=0; i<height; i++){
      free(nn_count[i]);
      free(clgs_count[i]);
      free(clgs_link_list[i]);
      free(nn_link_list[i]);
      free(common_count[i]);
      free(common_list[i]);
    }

    free(nn_count);
    free(clgs_link_list);
    free(nn_link_list);
    free(common_count);
    free(common_list);
    for(int i=0; i<height; i++){
      free(node_pointers[i]);
      free(num_children[i]);
      free(children_first[i]);
      free(node_codes[i]);
    }
    for(int i=0; i<height2; i++){
      free(node_pointers2[i]);
      free(num_children2[i]);
      free(children_first2[i]);
      free(node_codes2[i]);
    }
    free(node_pointers);
    free(num_children);
    free(children_first);
    free(node_codes);
    free(node_pointers2);
    free(num_children2);
    free(children_first2);
    free(node_codes2);
    free(bit_map);
    free(particle_codes);
    free(permutation_vector);
    free(bit_map2);
    free(particle_codes2);
    free(permutation_vector2);
  }
  free(X);
  free(X2);
}
