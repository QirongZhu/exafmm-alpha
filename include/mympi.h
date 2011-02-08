#ifndef mympi_h
#define mympi_h
#include <mpi.h>
#include <typeinfo>
#include "types.h"

class MyMPI {                                                   // My own MPI utilities
private:
  int const WAIT;                                               // Waiting time between output of different ranks
protected:
  int       SIZE;                                               // Number of MPI processes
  int       RANK;                                               // Index of current MPI process
  int       SIZES;                                              // Number of MPI processes for split communicator
  int       RANKS;                                              // Index of current MPI process for split communicator
public:
  MyMPI() : WAIT(100) {                                         // Constructor, initialize WAIT time
    int argc(0);                                                // Dummy argument count
    char **argv;                                                // Dummy argument value
    MPI_Init(&argc,&argv);                                      // Initialize MPI communicator
    MPI_Comm_size(MPI_COMM_WORLD,&SIZE);                        // Get number of MPI processes
    MPI_Comm_rank(MPI_COMM_WORLD,&RANK);                        // Get index of current MPI process
    MPISIZE = SIZE;                                             // Set global variable MPISIZE
    MPIRANK = RANK;                                             // Set global variable MPIRANK
  }

  ~MyMPI() {                                                    // Destructor
    MPI_Finalize();                                             // Finalize MPI communicator
  }

  int commSize() { return SIZE; }                               // Number of MPI processes
  int commRank() { return RANK; }                               // Index of current MPI process

  bool isPowerOfTwo(int const n) {                              // If n is power of two return true
    return ((n != 0) && !(n & (n - 1)));                        // Decrement and compare bits
  }

  template<typename T>
  int getType(T object) {
    int type;
    if       ( typeid(object) == typeid(char) ) {
      type = MPI_CHAR;
    } else if( typeid(object) == typeid(short) ) {
      type = MPI_SHORT;
    } else if( typeid(object) == typeid(int) ) {
      type = MPI_INT;
    } else if( typeid(object) == typeid(long) ) {
      type = MPI_LONG;
    } else if( typeid(object) == typeid(long long) ) {
      type = MPI_LONG_LONG;
    } else if( typeid(object) == typeid(unsigned char) ) {
      type = MPI_UNSIGNED_CHAR;
    } else if( typeid(object) == typeid(unsigned short) ) {
      type = MPI_UNSIGNED_SHORT;
    } else if( typeid(object) == typeid(unsigned int) ) {
      type = MPI_UNSIGNED;
    } else if( typeid(object) == typeid(unsigned long) ) {
      type = MPI_UNSIGNED_LONG;
    } else if( typeid(object) == typeid(unsigned long long) ) {
      type = MPI_UNSIGNED_LONG_LONG;
    } else if( typeid(object) == typeid(float) ) {
      type = MPI_FLOAT;
    } else if( typeid(object) == typeid(double) ) {
      type = MPI_DOUBLE;
    } else if( typeid(object) == typeid(long double) ) {
      type = MPI_LONG_DOUBLE;
    } else if( typeid(object) == typeid(std::complex<float>) ) {
      type = MPI_COMPLEX;
    } else if( typeid(object) == typeid(std::complex<double>) ) {
      type = MPI_DOUBLE_COMPLEX;
    }
    return type;
  }

  template<typename T>                                          // Detect data type to print
  void print(T data) {                                          // Print a scalar value on all ranks
    for( int irank=0; irank!=SIZE; ++irank ) {                  // Loop over ranks
      MPI_Barrier(MPI_COMM_WORLD);                              //  Sync processes
      usleep(WAIT);                                             //  Wait "WAIT" milliseconds
      if( RANK == irank )                                       //  If it's my turn
        std::cout << data << " ";                               //   Print "data"
    }                                                           // End loop over ranks
    MPI_Barrier(MPI_COMM_WORLD);                                // Sync processes
    usleep(WAIT);                                               // Wait "WAIT" milliseconds
    if( RANK == 0 ) std::cout << std::endl;                     // New lint
  }

  template<typename T>
  void print(T data, int const irank) {
    MPI_Barrier(MPI_COMM_WORLD);
    usleep(WAIT);
    if( RANK == irank ) std::cout << data;
  }

  template<typename T>
  void print(T *data, int const begin, int const end) {
    for( int irank=0; irank!=SIZE; ++irank ) {
      MPI_Barrier(MPI_COMM_WORLD);
      usleep(WAIT);
      if( RANK == irank ) {
        std::cout << RANK << " : ";
        for( int i=begin; i!=end; ++i )
          std::cout << data[i] << " ";
        std::cout << std::endl;
      }
    }
  }

  template<typename T>
  void print(T *data, int const begin, int const end, int const irank) {
    MPI_Barrier(MPI_COMM_WORLD);
    usleep(WAIT);
    if( RANK == irank ) {
      std::cout << RANK << " : ";
      for( int i=begin; i!=end; ++i )
        std::cout << data[i] << " ";
      std::cout << std::endl;
    }
  }

  template<typename T>
  int getBucket(T *data, int numData, int lOffset, std::vector<T> &send, std::vector<T> &recv, MPI_Comm MPI_COMM) {
    int maxBucket = send.size();                                // Maximum number of buckets
    int numBucket;                                              // Number of buckets
    int numSample = std::min(maxBucket/SIZES,numData);          // Number of local samples
    int const MPI_TYPE = getType(data[0]);                      // Get MPI data type
    int *rcnt = new int [SIZES];                                // MPI receive count
    int *rdsp = new int [SIZES];                                // MPI receive displacement
    for( int i=0; i!=numSample; ++i ) {                         // Loop over local samples
      int stride = numData/numSample;                           //  Sampling stride
      send[i] = data[lOffset + i * stride];                     //  Put sampled data in send buffer
    }                                                           // End loop over samples
    MPI_Gather(&numSample,1,MPI_INT,                            // Gather size of sample data to rank 0
               rcnt,      1,MPI_INT,
               0,MPI_COMM);
    if( RANKS == 0 ) {                                          // Only rank 0 operates on gathered info
      numBucket = 0;                                            //  Initialize number of buckets
      for( int irank=0; irank!=SIZES; ++irank ) {               //  Loop over all processes
        rdsp[irank] = numBucket;                                //   Calculate receive displacement
        numBucket += rcnt[irank];                               //   Accumulate receive count to get number of buckets
      }                                                         //  End loop over all processes
      recv.resize(numBucket);                                   //  Resize recv so that end() is valid
    }                                                           // Endif for rank 0
    MPI_Gatherv(&send[0],numSample,MPI_TYPE,                    // Gather sample data to rank 0
                &recv[0],rcnt,rdsp,MPI_TYPE,
                0,MPI_COMM);
    if( RANKS == 0 ) {                                          // Only rank 0 operates on gathered info
      std::sort(recv.begin(),recv.end());                       //  Sort the bucket data
      numBucket = std::unique(recv.begin(),recv.end()) - recv.begin();// Remove duplicate bucket data
      recv.resize(numBucket);                                   //  Resize recv again
    }                                                           // Endif for rank 0
    MPI_Bcast(&numBucket,1,MPI_INT,0,MPI_COMM);                 // Broadcast number of buckets
    MPI_Bcast(&recv[0],numBucket,MPI_TYPE,0,MPI_COMM);          // Broadcast bucket data
    delete[] rcnt;                                              // Delete receive count
    delete[] rdsp;                                              // Delete receive displacement
    return numBucket;                                           // Return number of buckets
  }

  template<typename T, typename T2>
  T2 nth_element(T *data, int numData, T2 n, MPI_Comm MPI_COMM=0) {// Find nth element of global data
    if( MPI_COMM == 0 ) {                                       // If MPI_COMM is not specified
      MPI_Comm_split(MPI_COMM_WORLD,0,RANK,&MPI_COMM);          //  Create an artificial MPI_COMM
    }
    MPI_Comm_size(MPI_COMM,&SIZES);                             //  Get number of MPI processes for that comm
    MPI_Comm_rank(MPI_COMM,&RANKS);                             //  Get index of current MPI process for that comm
    int maxBucket = 1000;                                       // Maximum number of buckets
    int numBucket;                                              // Number of buckets
    int lOffset = 0;                                            // Local offset of region being considered
    int const MPI_TYPE = getType(n);                            // Get MPI data type
    int *rcnt = new int [SIZES];                                // MPI receive count
    std::vector<T> send(maxBucket);                             // MPI send buffer for data
    std::vector<T> recv(maxBucket);                             // MPI receive buffer for data
    T2 gOffset = 0;                                             // Global offset of region being considered
    T2 *isend = new T2 [maxBucket];                             // MPI send buffer for index
    T2 *irecv = new T2 [maxBucket];                             // MPI receive buffer for index
    T2 *iredu = new T2 [maxBucket];                             // Local scan of index buffer
    numBucket = getBucket(data,numData,lOffset,send,recv,MPI_COMM);// Get global bucket data
    MPI_Barrier(MPI_COMM_WORLD);

    while( numBucket > 1 ) {                                    // While there are multipole candidates
      int ic=0, nth=0;                                          //  Initialize counters
      for( int i=0; i!=maxBucket; ++i ) isend[i] = 0;           //  Initialize bucket counter
      for( int i=0; i!=numData; ++i ) {                         //  Loop over range of data
        while( data[lOffset + i] > recv[ic] && ic < numBucket-1 ) ++ic;// Set counter to current bucket
        isend[ic]++;                                            //   Increment bucket counter
      }                                                         //  End loop over data
      MPI_Reduce(isend,irecv,numBucket,MPI_TYPE,                //  Reduce bucket counter
                 MPI_SUM,0,MPI_COMM);
      if( RANKS == 0 ) {                                        //  Only rank 0 operates on reduced data
        iredu[0] = 0;                                           //   Initialize global scan index
        for( int i=0; i!=numBucket-1; ++i )                     //   Loop over buckets
          iredu[i+1] = iredu[i] + irecv[i];                     //    Increment global scan index
        nth = 0;                                                //   Initialize index for bucket containing nth element
        while( n - gOffset > iredu[nth] && nth < numBucket ) ++nth;// Find index for bucket containing nth element
        --nth;                                                  //   Account for overshoot (do while?)
        if( nth == -1 ) nth = 0;                                //   If nth is -1 don't split
        gOffset += iredu[nth];                                  //   Increment global offset of region being considered
      }                                                         //  Endif for rank 0
      MPI_Bcast(&nth,1,MPI_INT,0,MPI_COMM);                     //  Broadcast index for bucket containing nth element
      MPI_Bcast(&gOffset,1,MPI_TYPE,0,MPI_COMM);                //  Broadcast global offset
      iredu[0] = 0;                                             //  Initialize local scan index
      for( int i=0; i!=numBucket-1; ++i )                       //  Loop over buckets
        iredu[i+1] = iredu[i] + isend[i];                       //   Increment local scan index
      if( nth == numBucket-1 )                                  //  If nth is last bucket
        numData = numData-iredu[nth];                           //   Region of interest is that bucket
      else                                                      //  If nth is not the last bucket
        numData = iredu[nth+1]-iredu[nth];                      //   Region of interest is that bucket
      lOffset += iredu[nth];                                    //  Increment local offset to that bucket
      numBucket = getBucket(data,numData,lOffset,send,recv,MPI_COMM);// Get global bucket data
    }                                                           // End while loop
    delete[] rcnt;                                              // Delete receive count
    delete[] isend;                                             // Delete send buffer for index
    delete[] irecv;                                             // Delete receive buffer for index
    delete[] iredu;                                             // Delete local scan of index buffer
    return recv[0];                                             // Return nth element
  }

};

#endif
