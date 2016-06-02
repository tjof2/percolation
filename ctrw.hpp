/***************************************************************************

    Copyright (C) 2016 Tom Furnival
    Email: tjof2@cam.ac.uk

    This file is part of CTRWfractal

    Percolation clusters developed from C code by Mark Newman.
    http://www-personal.umich.edu/~mejn/percolation/
    "A fast Monte Carlo algorithm for site or bond percolation"
    M. E. J. Newman and R. M. Ziff, Phys. Rev. E 64, 016706 (2001).

    See http://dx.doi.org/10.1088/1751-8113/47/13/135001
    for details on thresholds for percolation:
      - Square:     0.592746
      - Honeycomb:  0.697040230

***************************************************************************/

#ifndef CTRW_H
#define CTRW_H

// C++ headers
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <random>

// OpenMP
#include <omp.h>

// Armadillo
#include <armadillo>

// PCG RNG
#include "pcg/pcg_random.hpp"

template <class T>
class CTRWfractal {
public:
  CTRWfractal() {};
  ~CTRWfractal() {};

  void Initialize(int size,
                  double pc,
                  int rngseed,
                  std::string type,
                  int nwalks,
                  int nsteps,
                  double power_beta) {
    // Initialize threshold
    threshold = pc;

    // Number of random walks (>0)
    n_walks = nwalks;
    walk_length = nsteps;
    beta = power_beta;

    // Get dimensions
    L = size;

    // Check mode
    std::cout<<"Searching neighbours...    ";
    if (type.compare("Honeycomb") == 0) {
      lattice_mode = 1;
      nearest = 3;
      N = L * L * 4;
      nn.set_size(nearest, N);
      first_row.set_size(2 * L);
      last_row.set_size(2 * L);
      for (int i = 1; i <= 2 * L; i++) {
        first_row(i-1) = 1 - (3 * L) / 2. + (std::pow(-1, i) * L) / 2.
                           + 2 * i * L - 1;
        last_row(i-1) = L/2 * (4*i + std::pow(-1, i + 1) - 1) - 1;
      }
      time_start = GetTime();
      BoundariesHoneycomb();
      time_end = GetTime();
      run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
        time_end - time_start).count() / 1E6);
      std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;
    }
    else if (type.compare("Square") == 0) {
      lattice_mode = 0;
      nearest = 4;
      N = L * L;
      nn.set_size(nearest, N);
      time_start = GetTime();
      BoundariesSquare();
    	time_end = GetTime();
  		run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
        time_end - time_start).count() / 1E6);
  		std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;
    }
    else {
      std::cerr << "!!! WARNING: "
                << type.c_str()
                << " must be either 'Square' or 'Honeycomb' !!!"
                << std::endl;
    }

    // Define empty index
    EMPTY = (-N - 1);

    // Set array sizes
    lattice.set_size(N);
    occupation.set_size(N);
    lattice_coords.set_size(3, N);
    walks.set_size(walk_length);
    ctrw_times.set_size(walk_length);
    true_walks.set_size(walk_length);
    walks_coords.set_size(2, walk_length, n_walks);
    eaMSD.set_size(walk_length);
    eaMSD_all.set_size(walk_length, n_walks);
    taMSD.set_size(walk_length, n_walks);
    eataMSD.set_size(walk_length);
    eataMSD_all.set_size(walk_length, n_walks);
    ergodicity.set_size(walk_length);
    analysis.set_size(walk_length, n_walks + 3);

    // Seed the generator
    RNG = SeedRNG(rngseed);
    return;
  }

  void Run() {
    // First randomise the order in which the
    // sites are occupied
    std::cout<<"Randomising occupations... ";
		time_start = GetTime();
    Permutation();
  	time_end = GetTime();
		run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
      time_end - time_start).count() / 1E6);
		std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;

    // Now run the percolation algorithm
    std::cout<<"Running percolation...     ";
    time_start = GetTime();
    Percolate();
  	time_end = GetTime();
		run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
      time_end - time_start).count() / 1E6);
		std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;

    // Now build the lattice coordinates
    std::cout<<"Building lattice...        ";
    time_start = GetTime();
    BuildLattice();
  	time_end = GetTime();
		run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
      time_end - time_start).count() / 1E6);
		std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;

    // Now run the random walks and analyse
    if (n_walks > 0) {
      std::cout<<std::endl;
      std::cout<<"Simulating random walks... ";
      time_start = GetTime();
      RandomWalks();
      time_end = GetTime();
      run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
        time_end - time_start).count() / 1E6);
      std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;

      std::cout<<"Analysing random walks...  ";
      time_start = GetTime();
      AnalyseWalks();
      time_end = GetTime();
      run_time = (std::chrono::duration_cast<std::chrono::microseconds>(
        time_end - time_start).count() / 1E6);
      std::cout<<std::setprecision(6)<<run_time<<" s"<<std::endl;
    }

    return;
  }

  void Save(std::string filename) {
    std::cout<<std::endl<<"Saving files: "<<std::endl;
    lattice_coords.save(filename + ".cluster", arma::raw_binary);
    std::cout<<"   Cluster saved to:    "<<filename<<".cluster"<<std::endl;
    walks_coords.save(filename + ".walks", arma::raw_binary);
    std::cout<<"   Walks saved to:      "<<filename<<".walks"<<std::endl;
    analysis.save(filename + ".data", arma::raw_binary);
    std::cout<<"   Analysis saved to:   "<<filename<<".data"<<std::endl;
    return;
  }

private:

  int L, N, EMPTY, lattice_mode, n_walks, walk_length, nearest;

  arma::Col<T> lattice, occupation, walks, true_walks, first_row, last_row;
  arma::Mat<T> nn;
  arma::vec unit_cell, ctrw_times, eaMSD, eataMSD, ergodicity;
  arma::mat lattice_coords, eaMSD_all, eataMSD_all, taMSD;
  arma::mat analysis;
  arma::cube walks_coords;

  double threshold, beta, run_time;
  const double sqrt3 = 1.7320508075688772;

  pcg64 RNG;
  std::uniform_int_distribution<uint32_t> UniformDistribution {0, 4294967294};

	#if __cplusplus <= 199711L
	 std::chrono::time_point<std::chrono::monotonic_clock> time_start, time_end;
   std::chrono::time_point<std::chrono::monotonic_clock> GetTime() {
     return std::chrono::monotonic_clock::now();
   }
	#else
	 std::chrono::time_point<std::chrono::steady_clock> time_start, time_end;
   std::chrono::time_point<std::chrono::steady_clock> GetTime() {
     return std::chrono::steady_clock::now();
   }
	#endif

  void AnalyseWalks() {
    // Zero the placeholders
    eaMSD.zeros();
    taMSD.zeros();
    eataMSD.zeros();
    ergodicity.zeros();

    // Parallelize over n_walks
    #pragma omp parallel for
    for (int i = 0; i < n_walks; i++) {
      arma::vec2 walk_origin, walk_step;
      walk_origin = walks_coords.slice(i).col(0);
      for (int j = 0; j < walk_length; j++) {
        // Ensemble-average MSD
        walk_step = walks_coords.slice(i).col(j);
        eaMSD_all(j, i) += std::pow(walk_step(0) - walk_origin(0), 2)
                            + std::pow(walk_step(1) - walk_origin(1), 2);
        // Time-average MSD
        taMSD(j, i) = TAMSD(walks_coords.slice(i), walk_length, j);

        // Ensemble-time-average MSD
        eataMSD_all(j, i) = TAMSD(walks_coords.slice(i), j, 1);
      }
    }
    eaMSD = arma::mean(eaMSD_all, 1);
    eataMSD = arma::mean(eataMSD_all, 1);

    // Ergodicity breaking over s
    arma::mat mean_taMSD = arma::square(arma::mean(taMSD, 1));
    arma::mat mean_taMSD2 = arma::mean(arma::square(taMSD), 1);
    ergodicity = (mean_taMSD2 - mean_taMSD) / mean_taMSD;
    ergodicity.elem( arma::find_nonfinite(ergodicity) ).zeros();
    ergodicity /= arma::regspace<arma::vec>(1, walk_length);
    ergodicity.elem( arma::find_nonfinite(ergodicity) ).zeros();

    analysis.col(0) = eaMSD;
    analysis.col(1) = eataMSD;
    analysis.col(2) = ergodicity;
    analysis.cols(3, n_walks + 2) = taMSD;

    return;
  }

  double TAMSD(const arma::mat &walk, int t, int delta) {
    double integral = 0.;
    int diff = t - delta;
    for (int i = 0; i < diff; i++) {
      integral += std::pow(walk(0, i + delta) - walk(0, i), 2)
                    + std::pow(walk(1, i + delta) - walk(1, i), 2);
    }
    return integral / diff;
  }

  void RandomWalks() {
    // Set up selection of random start point
    arma::Col<T> latticeones = arma::regspace<arma::Col<T>>(0, N - 1);
    latticeones = latticeones.elem( find(lattice != EMPTY) );
    std::uniform_int_distribution<T> RandSample(0, static_cast<int>(latticeones.n_elem) - 1);
    std::exponential_distribution<double> ExponentialDistribution(beta);

    arma::uvec boundary_detect(walk_length);
    arma::uvec true_boundary(walk_length);

    // Simulate a random walk on the lattice
    for (int i = 0; i < n_walks; i++) {
      bool ok_start = false;
      int pos;
      int count_loop = 0;
      int count_max = (N > 1E6) ? N : 1E6;
      // Search for a random start position
      do {
        pos = latticeones(RandSample(RNG));
        // Check start position has >= 1 occupied nearest neighbours
        arma::Col<T> neighbours = GetOccupiedNeighbours(pos);
        if(neighbours.n_elem > 0 || count_loop >= count_max) {
          ok_start = true;
        }
        else {
          count_loop++;
        }
      } while (!ok_start);

      // If stuck on a site with no nearest neighbours,
      // set the whole walk to that site
      if (count_loop == count_max) {
        walks = pos * arma::ones<arma::Col<T>>(walk_length);
        boundary_detect.zeros();
      }
      else {
        walks(0) = pos;
        boundary_detect(0) = 0;
        for (int j = 1; j < walk_length; j++) {
          arma::Col<T> neighbours = GetOccupiedNeighbours(pos);
          std::uniform_int_distribution<T> RandChoice(0, static_cast<int>(neighbours.n_elem) - 1);
          pos = neighbours(RandChoice(RNG));
          walks(j) = pos;

          // Check for walks that hit the top boundary
          if (arma::any(first_row == walks(j - 1))
              && arma::any(last_row == pos)) {
            boundary_detect(j) = 1;
          }
          // Check for walks that hit the bottom boundary
          else if (arma::any(last_row == walks(j - 1))
                   && arma::any(first_row == pos)) {
            boundary_detect(j) = 2;
          }
          // Check for walks that hit the RHS
          else if (walks(j - 1) > (N - L)
                   && pos < L) {
            boundary_detect(j) = 3;
          }
          // Check for walks that hit the LHS
          else if (walks(j - 1) < L
                   && pos > (N - L)) {
            boundary_detect(j) = 4;
          }
          // Else do nothing
          else {
            boundary_detect(j) = 0;
          }
        }
      }

      // Draw CTRW variates from exponential distribution
      ctrw_times.set_size(walk_length);
      ctrw_times.imbue( [&]() { return ExponentialDistribution(RNG); } );

      // Transform to Pareto distribution and accumulate
      ctrw_times = arma::cumsum(arma::exp(ctrw_times));

      // Only keep times within range [0, walk_length]
      arma::uvec temp_time_boundary = arma::find(ctrw_times >= walk_length, 1, "first");
      int time_boundary = temp_time_boundary(0);
      ctrw_times = ctrw_times(arma::span(0,time_boundary));
      ctrw_times(time_boundary) = walk_length;

      // Subordinate fractal walk with CTRW
      int counter = 0;
      true_boundary.zeros();
      for (int j = 0; j < walk_length; j++) {
        if (j > ctrw_times(counter)) {
          counter++;
          true_boundary(j) = boundary_detect(counter);
        }
        true_walks(j) = walks(counter);
      }

      // Finally convert the walk to the coordinate system
      int nx_cell = 0;
      int ny_cell = 0;
      for (int nstep = 0; nstep < walk_length; nstep++) {
        switch (true_boundary(nstep)) {
          case 1:
            ny_cell++;
            break;
          case 2:
            ny_cell--;
            break;
          case 3:
            nx_cell++;
            break;
          case 4:
            nx_cell--;
              break;
          case 0:
          default:
              break;
        }
        walks_coords(0, nstep, i) = lattice_coords(0, true_walks(nstep))
                                          + nx_cell * unit_cell(0);
        walks_coords(1, nstep, i) = lattice_coords(1, true_walks(nstep))
                                          + ny_cell * unit_cell(1);
      }
    }
    return;
  }

  void BuildLattice() {
    // Populate the honeycomb lattice coordinates
    if (lattice_mode == 1) {
      double xx, yy;
      int count = 0;
      int cur_col = 0;
      for (int i = 0; i < 4*L; i++) {
        for (int j = L - 1; j >= 0; j--) {
          cur_col = i % 4;
          switch (cur_col) {
              case 0:
              default:
                xx = i / 4 * 3;
                yy = j * sqrt3 + sqrt3/2;
                break;
              case 1:
                xx = i / 4 * 3 + 1./2;
                yy = j * sqrt3;
                break;
              case 2:
                xx = i / 4 * 3 + 3./2;
                yy = j * sqrt3;
                break;
              case 3:
                xx = i / 4 * 3 + 2.;
                yy = j * sqrt3 + sqrt3/2;
                break;
          }
          lattice_coords(0, count) = xx;
          lattice_coords(1, count) = yy;
          lattice_coords(2, count) = (lattice(count) == EMPTY) ? 0 : 1;
          count++;
        }
      }
      // Get unit cell size
      unit_cell = arma::max(lattice_coords, 1);
      unit_cell(0) += 3/2;
      unit_cell(1) += sqrt3/2;
    }
    // To-do - build lattice for square lattice
    return;
  }

  // Check occupied neighbours of a point
  arma::Col<T> GetOccupiedNeighbours(int pos) {
    arma::Col<T> neighbours = nn.col(pos);
    arma::Col<T> neighbour_check(3);
    for (int k = 0; k < nearest; k++) {
      neighbour_check(k) = (lattice(neighbours(k)) == EMPTY) ? 0 : 1;
    }
    neighbours = neighbours.elem( find(neighbour_check == 1) );
    return neighbours;
  }

  // Randomise the order in which sites are occupied
  void Permutation() {
    T j;
    T temp;

    for (int i = 0; i < N; i++) {
      occupation(i) = i;
    }
    for (int i = 0; i < N; i++) {
      j = i + (N-i) * 2.3283064e-10 * UniformDistribution(RNG);
      temp = occupation(i);
      occupation(i) = occupation(j);
      occupation(j) = temp;
    }
    return;
  }

  // Find root of branch
  int FindRoot(int i) {
    if (lattice(i) < 0) {
       return i;
    }
    return lattice(i) = FindRoot(lattice(i));
  }

  // Percolation algorithm
  void Percolate() {
    int s1, s2;
    int r1, r2;
    T big = 0;

    for (int i = 0; i < N; i++) {
      lattice(i) = EMPTY;
    }
    for (int i = 0; i < (threshold * N) - 1; i++) {
      r1 = s1 = occupation[i];
      lattice(s1) = -1;
      for (int j = 0; j < nearest; j++) {
        s2 = nn(j, s1);
        if (lattice(s2) != EMPTY) {
          r2 = FindRoot(s2);
          if (r2 != r1) {
            if (lattice(r1) > lattice(r2)) {
              lattice(r2) += lattice(r1);
              lattice(r1) = r2;
              r1 = r2;
            } else {
              lattice(r1) += lattice(r2);
              lattice(r2) = r1;
            }
            if (-lattice(r1) > big) {
              big = -lattice(r1);
            }
          }
        }
      }
    }
    return;
  }

  // Nearest neighbours of a honeycomb lattice with
  // periodic boundary conditions
  void BoundariesHoneycomb() {
    int cur_col = 0;
    int count = 0;
    for (int i = 0; i < N; i++) {
      // First site
      if (i == 0) {
        nn(0, i) = i + L;
        nn(1, i) = i + 2*L - 1;
        nn(2, i) = i + N - L;
      }
      // Top right-hand corner
      else if (i == N - L) {
        nn(0, i) = i - 1;
        nn(1, i) = i - L;
        nn(2, i) = i - N + L;
      }
      // Bottom right-hand corner
      else if (i == N - L - 1) {
        nn(0, i) = i - L;
        nn(1, i) = i + L;
        nn(2, i) = i + 1;
      }
      // First column
      else if (i < L) {
        nn(0, i) = i + L - 1;
        nn(1, i) = i + L;
        nn(2, i) = i + N - L;
      }
      // Last column
      else if (i > (N - L)) {
        nn(0, i) = i - L - 1;
        nn(1, i) = i - L;
        nn(2, i) = i - N + L;
      }
      // Run through the rest of the tests
      else {
        switch (cur_col) {
          case 0:
            // First row
            if (arma::any(first_row == i)) {
              nn(0, i) = i - L;
              nn(1, i) = i + L;
              nn(2, i) = i + 2*L - 1;
            }
            // Otherwise
            else {
              nn(0, i) = i - L;
              nn(1, i) = i + L - 1;
              nn(2, i) = i + L;
            }
            break;
          case 1:
            // Last row
            if (arma::any(last_row == i)) {
              nn(0, i) = i - L;
              nn(1, i) = i + L;
              nn(2, i) = i - 2*L + 1;
            }
            // Otherwise
            else {
              nn(0, i) = i - L;
              nn(1, i) = i - L + 1;
              nn(2, i) = i + L;
            }
            break;
          case 2:
            // Last row
            if (arma::any(last_row == i)) {
              nn(0, i) = i - L;
              nn(1, i) = i + L;
              nn(2, i) = i + 1;
            }
            // Otherwise
            else {
              nn(0, i) = i - L;
              nn(1, i) = i + L;
              nn(2, i) = i + L + 1;
            }
            break;
          case 3:
            // First row
            if (arma::any(first_row == i)) {
              nn(0, i) = i - 1;
              nn(1, i) = i - L;
              nn(2, i) = i + L;
            }
            // Otherwise
            else {
              nn(0, i) = i - L - 1;
              nn(1, i) = i - L;
              nn(2, i) = i + L;
            }
            break;
        }
      }

      // Update current column
      if ((i + 1) % L == 0) {
        count++;
        cur_col = count % 4;
      }
    }
    return;
  }

  // Nearest neighbours of a square lattice
  // with periodic boundary conditions
  void BoundariesSquare() {
    for (int i = 0; i < N; i++) {
      nn(0, i) = (i + 1) % N;
      nn(1, i) = (i + N - 1) % N;
      nn(2, i) = (i + L) % N;
      nn(3, i) = (i + N - L) % N;
      if (i % L == 0) {
        nn(1, i) = i + L - 1;
      }
      if ((i + 1) % L == 0) {
        nn(0, i) = i - L + 1;
      }
    }
    return;
  }


  // Random number generator
  pcg64 SeedRNG(int seed) {
    // Check for user-defined seed
    if(seed > 0) {
      return pcg64(seed);
    }
    else {
      // Initialize random seed
      pcg_extras::seed_seq_from<std::random_device> seed_source;
      return pcg64(seed_source);
    }
  }
};

#endif