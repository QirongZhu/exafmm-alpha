#ifndef helmholtz_h
#define helmholtz_h
#include "spherical.h"
#if EXAFMM_USE_SIMD
#include "simdvec.h"
#endif

namespace exafmm {
  int nquad, nquad2;
  real_t * xquad, * xquad2;
  real_t * wquad, * wquad2;
  real_t * Anm1, * Anm2;

  template<int _P>
  class HelmholtzKernel : public KernelBase {
  public:
    static const Equation equation = Helmholtz;                 //!< Set equation to Helmholtz
    static const int P = _P;                                    //!< Set order of expansion
    static const int NTERM = P*P;                               //!< # of terms in Helmholtz expansion
    typedef vec<NTERM,complex_t> vecP;                          //!< Vector type for expansion terms
    typedef std::vector<Body<equation> > Bodies;                //!< Vector of bodies for Helmholtz
    typedef typename Bodies::iterator B_iter;                   //!< Iterator of body vector
    typedef std::vector<Cell<B_iter,vecP> > Cells;              //!< Vector of cells for Helmholtz
    typedef typename Cells::iterator C_iter;                    //!< Iterator of cell vector
    using KernelBase::Xperiodic;
    using KernelBase::wavek;

  private:
    static void polynomial(real_t x, int n, real_t & pol, real_t & der, real_t & sum) {
      sum = 0.5 + x * x * 1.5;
      real_t pk = 1;
      real_t pkp1 = x;
      if (n < 2) {
        der = 0;
        sum = 0.5;
        if (n == 0) return;
        der = 1;
        sum += x * x * 1.5;
        return;
      }
      for (int k=1; k<n; k++) {
        real_t pkm1 = pk;
        pk = pkp1;
        pkp1 = ((2 * k + 1) * x * pk - k * pkm1) / (k + 1);
        sum += pkp1 * pkp1 * (k + 1.5);
      }
      pol = pkp1;
      der = n * (x * pkp1 - pk) / (x * x - 1);
    }

    static void legendre(int nq, real_t * xq, real_t * wq) {
      real_t pol = 0, der, sum;
      real_t h = M_PI / (2 * nq);
      for (int i=1; i<=nq; i++) {
        xq[nq-i] = std::cos((2 * i - 1) * h);
      }
      xq[nq/2] = 0;
      for (int i=0; i<nq/2; i++) {
        real_t xk = xq[i];
        int ifout = 0;
        for (int k=0; k<10; k++) {
          polynomial(xk,nq,pol,der,sum);
          real_t delta = -pol / der;
          xk += delta;
          if (fabs(delta) < EPS) ifout++;
          if (ifout == 3) break;
        }
        xq[i] = xk;
        xq[nq-i-1] = -xk;
      }
      for (int i=0; i<(nq+1)/2; i++) {
        polynomial(xq[i],nq,pol,der,sum);
        wq[i] = 1 / sum;
        wq[nq-i-1] = wq[i];
      }
    }

    static void getAnm() {
      Anm1[0] = 1;
      Anm2[0] = 1;
      for (int m=0; m<=P; m++) {
        int ms = m * (m + 1) / 2 + m;
        int mps = (m + 1) * (m + 2) / 2 + m;
        if(m>0) Anm1[ms] = sqrt((2 * m - 1.0) / (2 * m));
        if(m<P) Anm1[mps] = sqrt(2 * m + 1.0);
        for (int n=m+2; n<=P; n++) {
          int nms = n * (n + 1) / 2 + m;
          Anm1[nms] = 2 * n - 1;
          Anm2[nms] = sqrt((n + m - 1.0) * (n - m - 1.0));
          Anm1[nms] /= sqrt(real_t(n - m) * (n + m));
          Anm2[nms] /= sqrt(real_t(n - m) * (n + m));
        }
      }
    }

    static void rotate(real_t theta, int nterms, complex_t Mnm[P*P],
                       complex_t Mrot[P*P]) {
      real_t Rnm1[P][2*P];
      real_t Rnm2[P][2*P];
      real_t sqrtCnm[2*P][2];
      for (int m=0; m<2*nterms; m++) {
        sqrtCnm[m][0] = sqrt(m+0.0);
      }
      sqrtCnm[0][1] = 0;
      sqrtCnm[1][1] = 0;
      for (int m=2; m<2*nterms; m++) {
        sqrtCnm[m][1] = sqrt(m * (m - 1) / 2.0);
      }
      real_t ctheta = std::cos(theta);
      if (fabs(ctheta) < EPS) ctheta = 0;
      real_t stheta = std::sin(-theta);
      if (fabs(stheta) < EPS) stheta = 0;
      real_t hsthta = stheta / sqrt(2.0);
      real_t cthtap = sqrt(2.0) * std::cos(theta * .5) * std::cos(theta * .5);
      real_t cthtan =-sqrt(2.0) * std::sin(theta * .5) * std::sin(theta * .5);
      Rnm1[0][P] = 1;
      Mrot[0] = Mnm[0] * Rnm1[0][P];
      for (int n=1; n<nterms; n++) {
        for (int m=-n; m<0; m++) {
          Rnm2[0][P+m] = -sqrtCnm[n-m][1] * Rnm1[0][P+m+1];
          if (m > (1 - n)) {
            Rnm2[0][P+m] += sqrtCnm[n+m][1] * Rnm1[0][P+m-1];
          }
          Rnm2[0][P+m] *= hsthta;
          if (m > -n) {
            Rnm2[0][P+m] += Rnm1[0][P+m] * ctheta * sqrtCnm[n+m][0] * sqrtCnm[n-m][0];
          }
          Rnm2[0][P+m] /= n;
        }
        Rnm2[0][P] = Rnm1[0][P] * ctheta;
        if (n > 1) {
          Rnm2[0][P] += hsthta * sqrtCnm[n][1] * (2 * Rnm1[0][P-1]) / n;
        }
        for (int m=1; m<=n; m++) {
          Rnm2[0][P+m] = Rnm2[0][P-m];
          if (m % 2 == 0) {
            Rnm2[m][P] = Rnm2[0][P+m];
          } else {
            Rnm2[m][P] =-Rnm2[0][P+m];
          }
        }
        for (int mp=1; mp<=n; mp++) {
          real_t scale = 1 / (sqrt(2.0) * sqrtCnm[n+mp][1]);
          for (int m=mp; m<=n; m++) {
            Rnm2[mp][P+m] = Rnm1[mp-1][P+m-1] * cthtap * sqrtCnm[n+m][1];
            Rnm2[mp][P-m] = Rnm1[mp-1][P-m+1] * cthtan * sqrtCnm[n+m][1];
            if (m < (n - 1)) {
              Rnm2[mp][P+m] -= Rnm1[mp-1][P+m+1] * cthtan * sqrtCnm[n-m][1];
              Rnm2[mp][P-m] -= Rnm1[mp-1][P-m-1] * cthtap * sqrtCnm[n-m][1];
            }
            if (m < n) {
              real_t d = stheta * sqrtCnm[n+m][0] * sqrtCnm[n-m][0];
              Rnm2[mp][P+m] += Rnm1[mp-1][P+m] * d;
              Rnm2[mp][P-m] += Rnm1[mp-1][P-m] * d;
            }
            Rnm2[mp][P+m] *= scale;
            Rnm2[mp][P-m] *= scale;
            if (m > mp) {
              if ((mp+m) % 2 == 0) {
                Rnm2[m][P+mp] = Rnm2[mp][P+m];
                Rnm2[m][P-mp] = Rnm2[mp][P-m];
              } else {
                Rnm2[m][P+mp] =-Rnm2[mp][P+m];
                Rnm2[m][P-mp] =-Rnm2[mp][P-m];
              }
            }
          }
        }
        for (int m=-n; m<=n; m++) {
          int nn = n * n + n;
          int nm = n * n + n + m;
          Mrot[nm] = Mnm[nn] * Rnm2[0][P+m];
          for (int mp=1; mp<=n; mp++) {
            nm = n * n + n + m;
            int npm = n * n + n + mp;
            int nmm = n * n + n - mp;
            Mrot[nm] += Mnm[npm] * Rnm2[mp][P+m] + Mnm[nmm] * Rnm2[mp][P-m];
          }
        }
        for (int m=-n; m<=n; m++) {
          for (int mp=0; mp<=n; mp++) {
            Rnm1[mp][P+m] = Rnm2[mp][P+m];
          }
        }
      }
    }

    static void get_Ynm(int nterms, real_t x, real_t Ynm[P*(P+1)/2]) {
      real_t y = -sqrt((1 - x) * (1 + x));
      Ynm[0] = 1;
      for (int m=0; m<nterms; m++) {
        int ms = m * (m + 1) / 2 + m;
        int mms = m * (m - 1) / 2 + m - 1;
        int mps = (m + 1) * (m + 2) / 2 + m;
        if (m > 0) Ynm[ms] = Ynm[mms] * y * Anm1[ms];
        if (m < nterms-1) Ynm[mps] = x * Ynm[ms] * Anm1[mps];
        for (int n=m+2; n<nterms; n++) {
          int nms = n * (n + 1) / 2 + m;
          int nm1 = n * (n - 1) / 2 + m;
          int nm2 = (n - 1) * (n - 2) / 2 + m;
          Ynm[nms] = Anm1[nms] * x * Ynm[nm1] - Anm2[nms] * Ynm[nm2];
        }
      }
      for (int n=0; n<nterms; n++) {
        for (int m=0; m<=n; m++) {
          int nms = n * (n + 1) / 2 + m;
          Ynm[nms] *= sqrt(2 * n + 1.0);
        }
      }
    }

    static void get_Ynmd(int nterms, real_t x, real_t Ynm[P*(P+1)/2], real_t Ynmd[P*(P+1)/2]) {
      real_t y = -sqrt((1 - x) * (1 + x));
      real_t y2 = y * y;
      Ynm[0] = 1;
      Ynmd[0] = 0;
      Ynm[1] = x * Ynm[0] * Anm1[1];
      Ynmd[1] = (x * Ynmd[0] + Ynm[0]) * Anm1[1];
      for (int n=2; n<nterms; n++) {
        int ns = n * (n + 1) / 2;
        int nm1 = n * (n - 1) / 2;
        int nm2 = (n - 1) * (n - 2) / 2;
        Ynm[ns] = Anm1[ns] * x * Ynm[nm1] - Anm2[ns] * Ynm[nm2];
        Ynmd[ns] = Anm1[ns] * (x * Ynmd[nm1] + Ynm[nm1]) - Anm2[ns] * Ynmd[nm2];
      }
      for (int m=1; m<nterms; m++) {
        int ms = m * (m + 1) / 2 + m;
        int mms = m * (m - 1) / 2 + m - 1;
        int mps = (m + 1) * (m + 2) / 2 + m;
        if (m == 1) Ynm[ms] = -Ynm[mms] * Anm1[ms];
        if (m > 1) Ynm[ms] = Ynm[mms] * y * Anm1[ms];
        if (m > 0) Ynmd[ms] = -Ynm[ms] * m * x;
        if (m < nterms-1) Ynm[mps] = x * Ynm[ms] * Anm1[mps];
        if (m < nterms-1) Ynmd[mps] = (x * Ynmd[ms] + y2 * Ynm[ms]) * Anm1[mps];
        for (int n=m+2; n<nterms; n++) {
          int nms = n * (n + 1) / 2 + m;
          int nm1 = n * (n - 1) / 2 + m;
          int nm2 = (n - 1) * (n - 2) / 2 + m;
          Ynm[nms] = Anm1[nms] * x * Ynm[nm1] - Anm2[nms] * Ynm[nm2];
          Ynmd[nms] = Anm1[nms] * (x * Ynmd[nm1] + y2 * Ynm[nm1]) - Anm2[nms] * Ynmd[nm2];
        }
      }
      for (int n=0; n<nterms; n++) {
        for (int m=0; m<=n; m++) {
          int nms = n * (n + 1) / 2 + m;
          Ynm[nms] *= sqrt(2 * n + 1.0);
          Ynmd[nms] *= sqrt(2 * n + 1.0);
        }
      }
    }

    static void get_hn(int nterms, complex_t z, real_t scale, complex_t * hn) {
      if (abs(z) < EPS) {
        for (int i=0; i<nterms; i++) {
          hn[i] = 0;
        }
        return;
      }
      complex_t zi = I * z;
      complex_t zinv = scale / z;
      hn[0] = exp(zi) / zi;
      hn[1] = hn[0] * (zinv - I * scale);
      real_t scale2 = scale * scale;
      for (int i=2; i<nterms; i++) {
        hn[i] = zinv * real_t(2 * i - 1.0) * hn[i-1] - scale2 * hn[i-2];
      }
    }

    static void get_hnd(int nterms, complex_t z, real_t scale, complex_t * hn, complex_t * hnd) {
      if (abs(z) < EPS) {
        for (int i=0; i<nterms; i++) {
          hn[i] = 0;
          hnd[i] = 0;
        }
        return;
      }
      complex_t zi = I * z;
      complex_t zinv = real_t(1.0) / z;
      hn[0] = exp(zi) / zi;
      hn[1] = hn[0] * (zinv - I) * scale;
      hnd[0] = -hn[1] / scale;
      hnd[1] = -zinv * real_t(2.0) * hn[1] + scale * hn[0];
      for (int i=2; i<nterms; i++) {
        hn[i] = (zinv * real_t(2 * i - 1.0) * hn[i-1] - scale * hn[i-2]) * scale;
        hnd[i] = -zinv * real_t(i + 1.0) * hn[i] + scale * hn[i-1];
      }
    }

    static void get_jn(int nterms, complex_t z, real_t scale, complex_t * jn, int ifder, complex_t * jnd) {
      int iscale[P+1];
      if (abs(z) < EPS) {
        jn[0] = 1;
        for (int i=1; i<nterms; i++) {
          jn[i] = 0;
        }
        if (ifder) {
          for (int i=0; i<nterms; i++) {
            jnd[i] = 0;
          }
          jnd[1] = 1.0 / (3 * scale);
        }
        return;
      }
      complex_t zinv = real_t(1.0) / z;
      jn[nterms-2] = 0;
      jn[nterms-1] = 1;
      real_t coef = 2 * nterms - 1;
      complex_t ztmp = coef * zinv;
      jn[nterms] = ztmp;
      int ntop = nterms;
      for (int i=0; i<ntop; i++) {
        iscale[i] = 0;
      }
      jn[ntop] = 0;
      jn[ntop-1] = 1;
      for (int i=ntop-1; i>0; i--) {
        coef = 2 * i + 1;
        ztmp = coef * zinv * jn[i] - jn[i+1];
        jn[i-1] = ztmp;
        if (abs(ztmp) > 1.0/EPS) {
          jn[i] *= EPS;
          jn[i-1] *= EPS;
          iscale[i] = 1;
        }
      }
      real_t scalinv = 1.0 / scale;
      coef = 1;
      for (int i=1; i<ntop; i++) {
        coef *= scalinv;
        if(iscale[i-1] == 1) coef *= EPS;
        jn[i] *= coef;
      }
      complex_t fj0 = sin(z) * zinv;
      complex_t fj1 = fj0 * zinv - cos(z) * zinv;
      if (abs(fj1) > abs(fj0)) {
        ztmp = fj1 / (jn[1] * scale);
      } else {
        ztmp = fj0 / jn[0];
      }
      for (int i=0; i<nterms; i++) {
        jn[i] *= ztmp;
      }
      if (ifder) {
        jn[nterms] *= ztmp;
        jnd[0] = -jn[1] * scale;
        for (int i=1; i<nterms; i++) {
          coef = i / (2 * i + 1.0);
          jnd[i] = coef * scalinv * jn[i-1] - (1 - coef) * scale * jn[i+1];
        }
      }
    }

  public:
    static void init() {
      xquad = new real_t [P];
      xquad2 = new real_t [2*P];
      wquad = new real_t [P];
      wquad2 = new real_t [2*P];
      Anm1 = new real_t [(P+1)*(P+2)/2];
      Anm2 = new real_t [(P+1)*(P+2)/2];
      nquad = fmax(6, P);
      legendre(nquad, xquad, wquad);
      nquad2 = fmax(6, 2*P);
      legendre(nquad2, xquad2, wquad2);
      getAnm();
    }

    static void finalize() {
      delete[] xquad;
      delete[] xquad2;
      delete[] wquad;
      delete[] wquad2;
      delete[] Anm1;
      delete[] Anm2;
    }

    static void normalize(Bodies & bodies) {
      for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {
        B->TRG /= B->SRC;
      }
    }

    static void P2P(C_iter Ci, C_iter Cj, bool mutual) {
      real_t wave_r = std::real(wavek);
      real_t wave_i = std::imag(wavek);
      B_iter Bi = Ci->BODY;
      B_iter Bj = Cj->BODY;
      int ni = Ci->NBODY;
      int nj = Cj->NBODY;
      int i = 0;
#if EXAFMM_USE_SIMD
      simdvec wave_rvec = wave_r;
      simdvec wave_ivec = wave_i;
      for ( ; i<=ni-NSIMD; i+=NSIMD) {
	simdvec zero = 0.0;
	simdvec one = 1.0;
	ksimdvec pot_r = zero;
	ksimdvec pot_i = zero;
	ksimdvec ax_r = zero;
	ksimdvec ax_i = zero;
	ksimdvec ay_r = zero;
	ksimdvec ay_i = zero;
	ksimdvec az_r = zero;
	ksimdvec az_i = zero;

	simdvec xi = SIMD<simdvec,B_iter,0,NSIMD>::setBody(Bi,i);
	simdvec yi = SIMD<simdvec,B_iter,1,NSIMD>::setBody(Bi,i);
	simdvec zi = SIMD<simdvec,B_iter,2,NSIMD>::setBody(Bi,i);
	simdvec mi_r = SIMD<simdvec,B_iter,4,NSIMD>::setBody(Bi,i);
	simdvec mi_i = SIMD<simdvec,B_iter,5,NSIMD>::setBody(Bi,i);

	simdvec dx = Xperiodic[0];
	xi -= dx;
	simdvec dy = Xperiodic[1];
	yi -= dy;
	simdvec dz = Xperiodic[2];
	zi -= dz;

	for (int j=0; j<nj; j++) {
	  dx = Bj[j].X[0];
	  dx -= xi;
	  dy = Bj[j].X[1];
	  dy -= yi;
	  dz = Bj[j].X[2];
	  dz -= zi;

	  simdvec R2 = eps2;
	  R2 += dx * dx;
	  simdvec mj_r = std::real(Bj[j].SRC);
	  R2 += dy * dy;
	  simdvec mj_i = std::imag(Bj[j].SRC);
	  R2 += dz * dz;
	  simdvec invR = rsqrt(R2);
	  simdvec R = one / invR;
	  invR &= R2 > zero;
	  R &= R2 > zero;

	  simdvec tmp = mi_r * mj_r - mi_i * mj_i;
	  mj_i = mi_r * mj_i + mi_i * mj_r;
	  mj_r = tmp;
	  tmp = invR / exp(wave_ivec * R);
	  simdvec coef_r = cos(wave_rvec * R) * tmp;
	  simdvec coef_i = sin(wave_rvec * R) * tmp;
	  tmp = mj_r * coef_r - mj_i * coef_i;
	  coef_i = mj_r * coef_i + mj_i * coef_r;
	  coef_r = tmp;
	  mj_r = (one + wave_ivec * R) * invR * invR;
	  mj_i = - wave_rvec * invR;
	  pot_r += coef_r;
	  pot_i += coef_i;
	  if (mutual) Bj[j].TRG[0] += kcomplex_t(sum(coef_r), sum(coef_i));
	  tmp = mj_r * coef_r - mj_i * coef_i;
	  coef_i = mj_r * coef_i + mj_i * coef_r;
	  coef_r = tmp;
	  ax_r += coef_r * dx;
	  ax_i += coef_i * dx;
	  if (mutual) Bj[j].TRG[1] += kcomplex_t(sum(coef_r * dx), sum(coef_i * dx));
	  ay_r += coef_r * dy;
	  ay_i += coef_i * dy;
	  if (mutual) Bj[j].TRG[2] += kcomplex_t(sum(coef_r * dy), sum(coef_i * dy));
	  az_r += coef_r * dz;
	  az_i += coef_i * dz;
	  if (mutual) Bj[j].TRG[3] += kcomplex_t(sum(coef_r * dz), sum(coef_i * dz));
	}
	for (int k=0; k<NSIMD; k++) {
	  Bi[i+k].TRG[0] += transpose(pot_r, pot_i, k);
	  Bi[i+k].TRG[1] -= transpose(ax_r, ax_i, k);
	  Bi[i+k].TRG[2] -= transpose(ay_r, ay_i, k);
	  Bi[i+k].TRG[3] -= transpose(az_r, az_i, k);
	}
      }
#endif
      for ( ; i<ni; i++) {
	real_t pot_r = 0.0;
	real_t pot_i = 0.0;
	real_t ax_r = 0.0;
	real_t ax_i = 0.0;
	real_t ay_r = 0.0;
	real_t ay_i = 0.0;
	real_t az_r = 0.0;
	real_t az_i = 0.0;
	real_t mi_r = std::real(Bi[i].SRC);
	real_t mi_i = std::imag(Bi[i].SRC);
	for (int j=0; j<nj; j++) {
	  real_t mj_r = std::real(Bj[j].SRC);
	  real_t mj_i = std::imag(Bj[j].SRC);
	  vec3 dX = Bi[i].X - Bj[j].X - Xperiodic;
	  real_t R2 = norm(dX) + eps2;
	  if (R2 != 0) {
	    real_t R = sqrt(R2);
	    real_t src2_r = mi_r * mj_r - mi_i * mj_i;
	    real_t src2_i = mi_r * mj_i + mi_i * mj_r;
	    real_t expikr = std::exp(wave_i * R) * R;
	    real_t expikr_r = std::cos(wave_r * R) / expikr;
	    real_t expikr_i = std::sin(wave_r * R) / expikr;
	    real_t coef1_r = src2_r * expikr_r - src2_i * expikr_i;
	    real_t coef1_i = src2_r * expikr_i + src2_i * expikr_r;
	    real_t kr_r = (1 + wave_i * R) / R2;
	    real_t kr_i = - wave_r / R;
	    real_t coef2_r = kr_r * coef1_r - kr_i * coef1_i;
	    real_t coef2_i = kr_r * coef1_i + kr_i * coef1_r;
	    pot_r += coef1_r;
	    pot_i += coef1_i;
	    ax_r += coef2_r * dX[0];
	    ax_i += coef2_i * dX[0];
	    ay_r += coef2_r * dX[1];
	    ay_i += coef2_i * dX[1];
	    az_r += coef2_r * dX[2];
	    az_i += coef2_i * dX[2];
	    if (mutual) {
	      Bj[j].TRG[0] += complex_t(coef1_r, coef1_i);
	      Bj[j].TRG[1] += complex_t(coef2_r, coef2_i) * dX[0];
	      Bj[j].TRG[2] += complex_t(coef2_r, coef2_i) * dX[1];
	      Bj[j].TRG[3] += complex_t(coef2_r, coef2_i) * dX[2];
	    }
	  }
	}
	Bi[i].TRG[0] += complex_t(pot_r, pot_i);
	Bi[i].TRG[1] += complex_t(ax_r, ax_i);
	Bi[i].TRG[2] += complex_t(ay_r, ay_i);
	Bi[i].TRG[3] += complex_t(az_r, az_i);
      }
    }

    static void P2P(C_iter C) {
      real_t wave_r = std::real(wavek);
      real_t wave_i = std::imag(wavek);
      B_iter B = C->BODY;
      int n = C->NBODY;
      int i = 0;
#if EXAFMM_USE_SIMD
      simdvec wave_rvec = wave_r;
      simdvec wave_ivec = wave_i;
      for ( ; i<=n-NSIMD; i+=NSIMD) {
	simdvec zero = 0.0;
	simdvec one = 1.0;
	ksimdvec pot_r = zero;
	ksimdvec pot_i = zero;
	ksimdvec ax_r = zero;
	ksimdvec ax_i = zero;
	ksimdvec ay_r = zero;
	ksimdvec ay_i = zero;
	ksimdvec az_r = zero;
	ksimdvec az_i = zero;

	simdvec index = SIMD<simdvec,B_iter,0,NSIMD>::setIndex(i);
	simdvec xi = SIMD<simdvec,B_iter,0,NSIMD>::setBody(B,i);
	simdvec yi = SIMD<simdvec,B_iter,1,NSIMD>::setBody(B,i);
	simdvec zi = SIMD<simdvec,B_iter,2,NSIMD>::setBody(B,i);
	simdvec mi_r = SIMD<simdvec,B_iter,4,NSIMD>::setBody(B,i);
	simdvec mi_i = SIMD<simdvec,B_iter,5,NSIMD>::setBody(B,i);
	for (int j=i+1; j<n; j++) {
	  simdvec dx = B[j].X[0];
	  dx -= xi;
	  simdvec dy = B[j].X[1];
	  dy -= yi;
	  simdvec dz = B[j].X[2];
	  dz -= zi;

	  simdvec R2 = eps2;
	  R2 += dx * dx;
	  simdvec mj_r = std::real(B[j].SRC);
	  R2 += dy * dy;
	  simdvec mj_i = std::imag(B[j].SRC);
	  R2 += dz * dz;
	  simdvec invR = rsqrt(R2);
	  simdvec R = one / invR;
	  invR &= index < j;
	  invR &= R2 > zero;
	  R &= index < j;
	  R &= R2 > zero;

	  simdvec tmp = mi_r * mj_r - mi_i * mj_i;
	  mj_i = mi_r * mj_i + mi_i * mj_r;
	  mj_r = tmp;
	  tmp = invR / exp(wave_ivec * R);
	  simdvec coef_r = cos(wave_rvec * R) * tmp;
	  simdvec coef_i = sin(wave_rvec * R) * tmp;
	  tmp = mj_r * coef_r - mj_i * coef_i;
	  coef_i = mj_r * coef_i + mj_i * coef_r;
	  coef_r = tmp;
	  mj_r = (one + wave_ivec * R) * invR * invR;
	  mj_i = - wave_rvec * invR;
	  pot_r += coef_r;
	  pot_i += coef_i;
	  B[j].TRG[0] += kcomplex_t(sum(coef_r), sum(coef_i));
	  tmp = mj_r * coef_r - mj_i * coef_i;
	  coef_i = mj_r * coef_i + mj_i * coef_r;
	  coef_r = tmp;
	  ax_r += coef_r * dx;
	  ax_i += coef_i * dx;
	  B[j].TRG[1] += kcomplex_t(sum(coef_r * dx), sum(coef_i * dx));
	  ay_r += coef_r * dy;
	  ay_i += coef_i * dy;
	  B[j].TRG[2] += kcomplex_t(sum(coef_r * dy), sum(coef_i * dy));
	  az_r += coef_r * dz;
	  az_i += coef_i * dz;
	  B[j].TRG[3] += kcomplex_t(sum(coef_r * dz), sum(coef_i * dz));
	}
	for (int k=0; k<NSIMD; k++) {
	  B[i+k].TRG[0] += transpose(pot_r, pot_i, k);
	  B[i+k].TRG[1] -= transpose(ax_r, ax_i, k);
	  B[i+k].TRG[2] -= transpose(ay_r, ay_i, k);
	  B[i+k].TRG[3] -= transpose(az_r, az_i, k);
	}
      }
#endif
      for ( ; i<n; i++) {
	kreal_t pot_r = 0;
	kreal_t pot_i = 0;
	kreal_t ax_r = 0;
	kreal_t ax_i = 0;
	kreal_t ay_r = 0;
	kreal_t ay_i = 0;
	kreal_t az_r = 0;
	kreal_t az_i = 0;
	real_t mi_r = std::real(B[i].SRC);
	real_t mi_i = std::imag(B[i].SRC);
	for (int j=i+1; j<n; j++) {
	  real_t mj_r = std::real(B[j].SRC);
	  real_t mj_i = std::imag(B[j].SRC);
	  vec3 dX = B[j].X - B[i].X;
	  real_t R2 = norm(dX) + eps2;
	  real_t R = sqrt(R2);
	  real_t src2_r = mi_r * mj_r - mi_i * mj_i;
	  real_t src2_i = mi_r * mj_i + mi_i * mj_r;
	  real_t expikr = std::exp(wave_i * R) * R;
	  real_t expikr_r = std::cos(wave_r * R) / expikr;
	  real_t expikr_i = std::sin(wave_r * R) / expikr;
	  real_t coef1_r = src2_r * expikr_r - src2_i * expikr_i;
	  real_t coef1_i = src2_r * expikr_i + src2_i * expikr_r;
	  real_t kr_r = (1 + wave_i * R) / R2;
	  real_t kr_i = - wave_r / R;
	  real_t coef2_r = kr_r * coef1_r - kr_i * coef1_i;
	  real_t coef2_i = kr_r * coef1_i + kr_i * coef1_r;
	  pot_r += coef1_r;
	  pot_i += coef1_i;
	  ax_r += coef2_r * dX[0];
	  ax_i += coef2_i * dX[0];
	  ay_r += coef2_r * dX[1];
	  ay_i += coef2_i * dX[1];
	  az_r += coef2_r * dX[2];
	  az_i += coef2_i * dX[2];
	  B[j].TRG[0] += complex_t(coef1_r, coef1_i);
	  B[j].TRG[1] += complex_t(coef2_r, coef2_i) * dX[0];
	  B[j].TRG[2] += complex_t(coef2_r, coef2_i) * dX[1];
	  B[j].TRG[3] += complex_t(coef2_r, coef2_i) * dX[2];
	}
	B[i].TRG[0] += complex_t(pot_r, pot_i);
	B[i].TRG[1] -= complex_t(ax_r, ax_i);
	B[i].TRG[2] -= complex_t(ay_r, ay_i);
	B[i].TRG[3] -= complex_t(az_r, az_i);
      }
    }

    static void P2M(C_iter C) {
      real_t Ynm[P*(P+1)/2];
      complex_t ephi[P], jn[P+1], jnd[P+1];
      vecP Mnm = complex_t(0,0);
      real_t kscale = C->SCALE * abs(wavek);
      for (B_iter B=C->BODY; B!=C->BODY+C->NBODY; B++) {
	vec3 dX = B->X - C->X;
	real_t r, theta, phi;
	cart2sph(dX, r, theta, phi);
	real_t ctheta = std::cos(theta);
	ephi[1] = exp(I * phi);
	for (int n=2; n<P; n++) {
	  ephi[n] = ephi[n-1] * ephi[1];
	}
	get_Ynm(P, ctheta, Ynm);
	complex_t z = wavek * r;
	get_jn(P, z, kscale, jn, 0, jnd);
	for (int n=0; n<P; n++) {
	  jn[n] *= B->SRC;
	}
	for (int n=0; n<P; n++) {
	  int nm = n * n + n;
	  int nms = n * (n + 1) / 2;
	  Mnm[nm] += Ynm[nms] * jn[n];
	  for (int m=1; m<=n; m++) {
	    nms = n * (n + 1) / 2 + m;
	    int npm = n * n + n + m;
	    int nmm = n * n + n - m;
	    complex_t Ynmjn = Ynm[nms] * jn[n];
	    Mnm[npm] += Ynmjn * conj(ephi[m]);
	    Mnm[nmm] += Ynmjn * ephi[m];
	  }
	}
      }
      C->M += Mnm * I * wavek;
    }

    static void M2M(C_iter Ci, C_iter C0) {
      real_t Ynm[P*(P+1)/2];
      complex_t phitemp[2*P], hn[P], ephi[2*P];
      vecP Mnm = complex_t(0,0);
      vecP Mrot = complex_t(0,0);
      real_t kscalei = Ci->SCALE * abs(wavek);
      for (C_iter Cj=C0+Ci->ICHILD; Cj!=C0+Ci->ICHILD+Ci->NCHILD; Cj++) {
	real_t kscalej = Cj->SCALE * abs(wavek);
	real_t radius = Cj->SCALE * sqrt(3.0);
	vec3 dX = Ci->X - Cj->X;
	real_t r, theta, phi;
	cart2sph(dX, r, theta, phi);
	ephi[P+1] = exp(I * phi);
	ephi[P] = 1;
	ephi[P-1] = conj(ephi[P+1]);
	for (int n=2; n<P; n++) {
	  ephi[P+n] = ephi[P+n-1] * ephi[P+1];
	  ephi[P-n] = conj(ephi[P+n]);
	}
	for (int n=0; n<P; n++) {
	  for (int m=-n; m<=n; m++) {
	    int nm = n * n + n + m;
	    Mnm[nm] = Cj->M[nm] * ephi[P+m];
	  }
	}
	rotate(theta, P, Mnm, Mrot);
	for (int n=0; n<P; n++) {
	  for (int m=-n; m<=n; m++) {
	    int nm = n * n + n + m;
	    Mnm[nm] = 0;
	  }
	}
	for (int l=0; l<nquad2; l++) {
	  real_t ctheta = xquad2[l];
	  real_t stheta = sqrt(1 - ctheta * ctheta);
	  real_t rj = (r + radius * ctheta) * (r + radius * ctheta) + (radius * stheta) * (radius * stheta);
	  rj = sqrt(rj);
	  real_t cthetaj = (r + radius * ctheta) / rj;
	  complex_t z = wavek * rj;
	  get_Ynm(P, cthetaj, Ynm);
	  get_hn(P, z, kscalej, hn);
	  for (int m=-P+1; m<P; m++) {
	    int mabs = abs(m);
	    phitemp[P+m] = 0;
	    for (int n=mabs; n<P; n++) {
	      int nm = n * n + n + m;
	      int nms = n * (n + 1) / 2 + mabs;
	      phitemp[P+m] += Mrot[nm] * hn[n] * Ynm[nms];
	    }
	  }
	  get_Ynm(P, xquad2[l], Ynm);
	  for (int m=-P+1; m<P; m++) {
	    int mabs = abs(m);
	    z = phitemp[P+m] * wquad2[l] * real_t(.5);
	    for (int n=mabs; n<P; n++) {
	      int nm = n * n + n + m;
	      int nms = n * (n + 1) / 2 + mabs;
	      Mnm[nm] += z * Ynm[nms];
	    }
	  }
	}
	complex_t z = wavek * radius;
	get_hn(P, z, kscalei, hn);
	for (int n=0; n<P; n++) {
	  for (int m=-n; m<=n; m++) {
	    int nm = n * n + n + m;
	    Mnm[nm] /= hn[n];
	  }
	}
	rotate(-theta, P, Mnm, Mrot);
	for (int n=0; n<P; n++) {
	  for (int m=-n; m<=n; m++) {
	    int nm = n * n + n + m;
	    Mnm[nm] = ephi[P-m] * Mrot[nm];
	  }
	}
	Ci->M += Mnm;
      }
    }

    static void M2L(C_iter Ci, C_iter Cj, bool mutual) {
      assert(mutual == false);
      real_t Ynm[P*(P+1)/2], Ynmd[P*(P+1)/2];
      complex_t phitemp[2*P], phitempn[2*P];
      complex_t hn[P], hnd[P], jn[P+1], jnd[P+1], ephi[2*P];
      vecP Lnm = complex_t(0,0);
      vecP Lnmd = complex_t(0,0);
      vecP Mnm, Mrot, Lrot;
      real_t kscalej = Cj->SCALE * abs(wavek);
      real_t kscalei = Ci->SCALE * abs(wavek);
      real_t radius = Cj->SCALE * sqrt(3.0) * .5;
      vec3 dX = Ci->X - Cj->X - Xperiodic;
      real_t r, theta, phi;
      cart2sph(dX, r, theta, phi);
      dX /= Cj->SCALE;
      if (fabs(dX[0]) > EPS) dX[0] = fabs(dX[0]) - .5;
      if (fabs(dX[1]) > EPS) dX[1] = fabs(dX[1]) - .5;
      if (fabs(dX[2]) > EPS) dX[2] = fabs(dX[2]) - .5;
      real_t rr = sqrt(norm(dX));
      real_t coef1 = P * 1.65 - 15.5;
      real_t coef2 = P * 0.25 + 3.0;
      int Popt = coef1 / (rr * rr) + coef2;
      assert(0 < Popt);
      assert(Popt <= 2*P);
      if(Popt > P) Popt = P;
      ephi[P+1] = exp(I * phi);
      ephi[P] = 1;
      ephi[P-1] = conj(ephi[P+1]);
      for (int n=2; n<P; n++) {
	ephi[P+n] = ephi[P+n-1] * ephi[P+1];
	ephi[P-n] = conj(ephi[P+n]);
      }
      for (int n=0; n<Popt; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  Mnm[nm] = Cj->M[nm] * ephi[P+m];
	}
      }
      rotate(theta, Popt, Mnm, Mrot);
      for (int l=0; l<nquad; l++) {
	real_t ctheta = xquad[l];
	real_t stheta = sqrt(1 - ctheta * ctheta);
	real_t rj = (r + radius * ctheta) * (r + radius * ctheta) + (radius * stheta) * (radius * stheta);
	rj = sqrt(rj);
	real_t cthetaj = (r + radius * ctheta) / rj;
	real_t sthetaj = sqrt(1 - cthetaj * cthetaj);
	real_t rn = sthetaj * stheta + cthetaj * ctheta;
	real_t thetan = (cthetaj * stheta - ctheta * sthetaj) / rj;
	complex_t z = wavek * rj;
	get_Ynmd(Popt, cthetaj, Ynm, Ynmd);
	get_hnd(Popt, z, kscalej, hn, hnd);
	for (int n=0; n<Popt; n++) {
	  hnd[n] *= wavek;
	}
	for (int n=1; n<Popt; n++) {
	  for (int m=1; m<=n; m++) {
	    int nms = n * (n + 1) / 2 + m;
	    Ynm[nms] *= sthetaj;
	  }
	}
	for (int m=-Popt+1; m<Popt; m++) {
	  phitemp[Popt+m] = 0;
	  phitempn[Popt+m] = 0;
	}
	phitemp[Popt] = Mrot[0] * hn[0];
	phitempn[Popt] = Mrot[0] * hnd[0] * rn;
	for (int n=1; n<Popt; n++) {
	  int nm = n * n + n;
	  int nms = n * (n + 1) / 2;
	  phitemp[Popt] += Mrot[nm] * hn[n] * Ynm[nms];
	  complex_t ut1 = hnd[n] * rn;
	  complex_t ut2 = hn[n] * thetan;
	  complex_t ut3 = ut1 * Ynm[nms] - ut2 * Ynmd[nms] * sthetaj;
	  phitempn[Popt] += ut3 * Mrot[nm];
	  for (int m=1; m<=n; m++) {
	    nms = n * (n + 1) / 2 + m;
	    int npm = n * n + n + m;
	    int nmm = n * n + n - m;
	    z = hn[n] * Ynm[nms];
	    phitemp[Popt+m] += Mrot[npm] * z;
	    phitemp[Popt-m] += Mrot[nmm] * z;
	    ut3 = ut1 * Ynm[nms] - ut2 * Ynmd[nms];
	    phitempn[Popt+m] += ut3 * Mrot[npm];
	    phitempn[Popt-m] += ut3 * Mrot[nmm];
	  }
	}
	get_Ynm(Popt, xquad[l], Ynm);
	for (int m=-Popt+1; m<Popt; m++) {
	  int mabs = abs(m);
	  z = phitemp[Popt+m] * wquad[l] * real_t(.5);
	  for (int n=mabs; n<Popt; n++) {
	    int nm = n * n + n + m;
	    int nms = n * (n + 1) / 2 + mabs;
	    Lnm[nm] += z * Ynm[nms];
	  }
	  z = phitempn[Popt+m] * wquad[l] * real_t(.5);
	  for (int n=mabs; n<Popt; n++) {
	    int nm = n * n + n + m;
	    int nms = n * (n + 1) / 2 + mabs;
	    Lnmd[nm] += z * Ynm[nms];
	  }
	}
      }
      complex_t z = wavek * radius;
      get_jn(Popt, z, kscalei, jn, 1, jnd);
      for (int n=0; n<Popt; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  complex_t zh = jn[n];
	  complex_t zhn = jnd[n] * wavek;
	  z = zh * zh + zhn * zhn;
	  Lnm[nm] = (zh * Lnm[nm] + zhn * Lnmd[nm]) / z;
	}
      }
      rotate(-theta, Popt, Lnm, Lrot);
      for (int n=0; n<Popt; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  Lnm[nm] = ephi[P-m] * Lrot[nm];
	}
      }
      Ci->L += Lnm;
    }

    static void L2L(C_iter Ci, C_iter C0) {
      real_t Ynm[P*(P+1)/2], Ynmd[P*(P+1)/2];
      complex_t phitemp[2*P], phitempn[2*P];
      complex_t jn[P+1], jnd[P+1], ephi[2*P];
      vecP Lnm, Lnmd, Lrot;
      real_t kscalei = Ci->SCALE * abs(wavek);
      C_iter Cj = C0 + Ci->IPARENT;
      real_t kscalej = Cj->SCALE * abs(wavek);
      real_t radius = Cj->SCALE * sqrt(3.0) * .5;
      vec3 dX = Ci->X - Cj->X;
      real_t r, theta, phi;
      cart2sph(dX, r, theta, phi);
      ephi[P+1] = exp(I * phi);
      ephi[P] = 1;
      ephi[P-1] = conj(ephi[P+1]);
      for (int n=2; n<P; n++) {
	ephi[P+n] = ephi[P+n-1] * ephi[P+1];
	ephi[P-n] = conj(ephi[P+n]);
      }
      for (int n=0; n<P; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  Lnm[nm] = Cj->L[nm] * ephi[P+m];
	}
      }
      rotate(theta, P, Lnm, Lrot);
      for (int n=0; n<P; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  Lnm[nm] = 0;
	  Lnmd[nm] = 0;
	}
      }
      for (int l=0; l<nquad; l++) {
	real_t ctheta = xquad[l];
	real_t stheta = sqrt(1 - ctheta * ctheta);
	real_t rj = (r + radius * ctheta) * (r + radius * ctheta) + (radius * stheta) * (radius * stheta);
	rj = sqrt(rj);
	real_t cthetaj = (r + radius * ctheta) / rj;
	real_t sthetaj = sqrt(1 - cthetaj * cthetaj);
	real_t rn = sthetaj * stheta + cthetaj * ctheta;
	real_t thetan = (cthetaj * stheta - ctheta * sthetaj) / rj;
	complex_t z = wavek * rj;
	get_Ynmd(P, cthetaj, Ynm, Ynmd);
	get_jn(P, z, kscalej, jn, 1, jnd);
	for (int n=0; n<P; n++) {
	  jnd[n] *= wavek;
	}
	for (int n=1; n<P; n++) {
	  for (int m=1; m<=n; m++) {
	    int nms = n * (n + 1) / 2 + m;
	    Ynm[nms] *= sthetaj;
	  }
	}
	for (int m=-P+1; m<P; m++) {
	  phitemp[P+m] = 0;
	  phitempn[P+m] = 0;
	}
	phitemp[P] = Lrot[0] * jn[0];
	phitempn[P] = Lrot[0] * jnd[0] * rn;
	for (int n=1; n<P; n++) {
	  int nm = n * n + n;
	  int nms = n * (n + 1) / 2;
	  phitemp[P] += Lrot[nm] * jn[n] * Ynm[nms];
	  complex_t ut1 = jnd[n] * rn;
	  complex_t ut2 = jn[n] * thetan;
	  complex_t ut3 = ut1 * Ynm[nms] - ut2 * Ynmd[nms] * sthetaj;
	  phitempn[P] += ut3 * Lrot[nm];
	  for (int m=1; m<=n; m++) {
	    nms = n * (n + 1) / 2 + m;
	    int npm = n * n + n + m;
	    int nmm = n * n + n - m;
	    z = jn[n] * Ynm[nms];
	    phitemp[P+m] += Lrot[npm] * z;
	    phitemp[P-m] += Lrot[nmm] * z;
	    ut3 = ut1 * Ynm[nms] - ut2 * Ynmd[nms];
	    phitempn[P+m] += ut3 * Lrot[npm];
	    phitempn[P-m] += ut3 * Lrot[nmm];
	  }
	}
	get_Ynm(P, xquad[l], Ynm);
	for (int m=-P+1; m<P; m++) {
	  int mabs = abs(m);
	  z = phitemp[P+m] * wquad[l] * real_t(.5);
	  for (int n=mabs; n<P; n++) {
	    int nm = n * n + n + m;
	    int nms = n * (n + 1) / 2 + mabs;
	    Lnm[nm] += z * Ynm[nms];
	  }
	  z = phitempn[P+m] * wquad[l] * real_t(.5);
	  for (int n=mabs; n<P; n++) {
	    int nm = n * n + n + m;
	    int nms = n * (n + 1) / 2 + mabs;
	    Lnmd[nm] += z * Ynm[nms];
	  }
	}
      }
      complex_t z = wavek * radius;
      get_jn(P, z, kscalei, jn, 1, jnd);
      for (int n=0; n<P; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  complex_t zh = jn[n];
	  complex_t zhn = jnd[n] * wavek;
	  z = zh * zh + zhn * zhn;
	  Lnm[nm] = (zh * Lnm[nm] + zhn * Lnmd[nm]) / z;
	}
      }
      rotate(-theta, P, Lnm, Lrot);
      for (int n=0; n<P; n++) {
	for (int m=-n; m<=n; m++) {
	  int nm = n * n + n + m;
	  Lnm[nm] = ephi[P-m] * Lrot[nm];
	}
      }
      Ci->L += Lnm;
    }

    static void L2P(C_iter C) {
      real_t Ynm[P*(P+1)/2], Ynmd[P*(P+1)/2];
      complex_t ephi[P], jn[P+1], jnd[P+1];
      real_t kscale = C->SCALE * abs(wavek);
      for (B_iter B=C->BODY; B!=C->BODY+C->NBODY; B++) {
	B->TRG /= B->SRC;
	vecP Lj = C->L;
	kcvec4 TRG = kcomplex_t(0,0);
	vec3 dX = B->X - C->X;
	real_t r, theta, phi;
	cart2sph(dX, r, theta, phi);
	real_t ctheta = std::cos(theta);
	real_t stheta = std::sin(theta);
	real_t cphi = std::cos(phi);
	real_t sphi = std::sin(phi);
	ephi[1] = std::exp(I * phi);
	for (int n=2; n<P; n++) {
	  ephi[n] = ephi[n-1] * ephi[1];
	}
	real_t rx = stheta * cphi;
	real_t thetax = ctheta * cphi;
	real_t phix = -sphi;
	real_t ry = stheta * sphi;
	real_t thetay = ctheta * sphi;
	real_t phiy = cphi;
	real_t rz = ctheta;
	real_t thetaz = -stheta;
	real_t phiz = 0;
	get_Ynmd(P, ctheta, Ynm, Ynmd);
	complex_t z = wavek * r;
	get_jn(P, z, kscale, jn, 1, jnd);
	TRG[0] += Lj[0] * jn[0];
	for (int n=0; n<P; n++) {
	  jnd[n] *= wavek;
	}
	complex_t ur = Lj[0] * jnd[0];
	complex_t utheta = 0;
	complex_t uphi = 0;
	for (int n=1; n<P; n++) {
	  int nm = n * n + n;
	  int nms = n * (n + 1) / 2;
	  TRG[0] += Lj[nm] * jn[n] * Ynm[nms];
	  ur += jnd[n] * Ynm[nms] * Lj[nm];
	  complex_t jnuse = jn[n+1] * kscale + jn[n-1] / kscale;
	  jnuse = wavek * jnuse / real_t(2 * n + 1.0);
	  utheta -= Lj[nm] * jnuse * Ynmd[nms] * stheta;
	  for (int m=1; m<=n; m++) {
	    int npm = n * n + n + m;
	    int nmm = n * n + n - m;
	    nms = n * (n + 1) / 2 + m;
	    complex_t ztmp1 = jn[n] * Ynm[nms] * stheta;
	    complex_t ztmp2 = Lj[npm] * ephi[m];
	    complex_t ztmp3 = Lj[nmm] * conj(ephi[m]);
	    complex_t ztmpsum = ztmp2 + ztmp3;
	    TRG[0] += ztmp1 * ztmpsum;
	    ur += jnd[n] * Ynm[nms] * stheta * ztmpsum;
	    utheta -= ztmpsum * jnuse * Ynmd[nms];
	    ztmpsum = real_t(m) * I * (ztmp2 - ztmp3);
	    uphi += jnuse * Ynm[nms] * ztmpsum;
	  }
	}
	complex_t ux = ur * rx + utheta * thetax + uphi * phix;
	complex_t uy = ur * ry + utheta * thetay + uphi * phiy;
	complex_t uz = ur * rz + utheta * thetaz + uphi * phiz;
	TRG[1] -= ux;
	TRG[2] -= uy;
	TRG[3] -= uz;
	B->TRG += TRG;
      }
    }
  };
}

#endif