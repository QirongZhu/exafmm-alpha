#include "ewald.h"
#include "fmm.h"
#include "logger.h"


#define DIM 3

#define NN 26
#define FN 37
#define CN 152

void formInteractionStencil(int *common_stencil, int *far_stencil,
                            int *near_stencil);

int main() {
  Fmm FMM;
  const int numBodies = 10000;
  const int ncrit = 100;
  const int maxLevel = numBodies >= ncrit ? 1 + int(log(numBodies / ncrit)/M_LN2/3) : 0;
  const int numNeighbors = 1;
  const int numImages = 3;
  const real cycle = 10 * M_PI;
  real potDif = 0, potNrm = 0, accDif = 0, accNrm = 0;

  printf("Height: %d\n", maxLevel);

  int common_stencil[DIM*CN] = {0};
  int far_stencil[8*DIM*FN] = {0};
  int near_stencil[8*DIM*NN] = {0};

  formInteractionStencil(common_stencil, far_stencil,
                         near_stencil);

  logger::verbose = true;
  logger::printTitle("FMM Profiling");

  logger::startTimer("Allocate");
  FMM.allocate(numBodies, maxLevel, numNeighbors, numImages);
  logger::stopTimer("Allocate");

  logger::startTimer("Init bodies");
  FMM.initBodies(cycle);
  logger::stopTimer("Init bodies");
  
  logger::startTimer("Sort bodies");
  //FMM.sortBodies();
  FMM.sortBodies_sakura();
  logger::stopTimer("Sort bodies");


  logger::startTimer("Fill leafs");
  FMM.fillLeafs();
  logger::stopTimer("Fill leafs");
  
  logger::startTimer("P2M");
  FMM.P2M();
  logger::stopTimer("P2M");

  logger::startTimer("M2M");
  FMM.M2M();
  logger::stopTimer("M2M");

  logger::startTimer("M2L");
  //FMM.M2L();
  FMM.M2L(common_stencil, far_stencil);
  logger::stopTimer("M2L");

  logger::startTimer("L2L");
  FMM.L2L();
  logger::stopTimer("L2L");

  logger::startTimer("L2P");
  FMM.L2P();
  logger::stopTimer("L2P");

  logger::startTimer("P2P");
  //FMM.P2P();
  FMM.P2P(near_stencil);
  logger::stopTimer("P2P");

  logger::startTimer("Verify");

#if 0
  FMM.direct();
  FMM.verify(100, potDif, potNrm, accDif, accNrm);
#else
  Ewald ewald(numBodies, maxLevel, cycle);
  ewald.dipoleCorrection(FMM.Ibodies, FMM.Jbodies);
  ewald.wavePart(FMM.Ibodies2, FMM.Jbodies);
  ewald.realPart(FMM.Ibodies2, FMM.Jbodies, FMM.Leafs);
  FMM.verify(numBodies, potDif, potNrm, accDif, accNrm);
#endif

  logger::stopTimer("Verify");

  logger::startTimer("Deallocate");
  FMM.deallocate();
  logger::stopTimer("Deallocate");

  logger::printTitle("FMM vs. direct");
  logger::printError("Rel. L2 Error (pot)",std::sqrt(potDif/potNrm));
  logger::printError("Rel. L2 Error (acc)",std::sqrt(accDif/accNrm));
}