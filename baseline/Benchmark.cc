/// @copyright (c) 2007 CSIRO
/// Australia Telescope National Facility (ATNF)
/// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
/// PO Box 76, Epping NSW 1710, Australia
///
/// This file is part of the ASKAP software distribution.
///
/// The ASKAP software distribution is free software: you can redistribute it
/// and/or modify it under the terms of the GNU General Public License as
/// published by the Free Software Foundation; either version 2 of the License,
/// or (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
///
/// This program was modified so as to use it in the contest.
/// The last modification was on April 2, 2015.
///

// Include own header file first
#include "Benchmark.h"
#include "Stopwatch.h"

// System includes
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>

Benchmark::Benchmark()
        : next(1)
{
}

// Return a pseudo-random integer in the range 0..2147483647
// Based on an algorithm in Kernighan & Ritchie, "The C Programming Language"
int Benchmark::randomInt()
{
    const unsigned int maxint = std::numeric_limits<int>::max();
    next = next * 1103515245 + 12345;
    return ((unsigned int)(next / 65536) % maxint);
}

void Benchmark::init()
{
    int rd1_tag,rd2_tag,rd3_tag,rd4_tag,id,nl;
    MPI_Status status;

    rd1_tag=0;
    rd2_tag=1;
    rd3_tag=2;
    rd4_tag=3;

    nSamples=BLOCK_SIZE(myid,np,nSamples_a);

    if(myid == np-1)
    {
      nl=nSamples+1;
    }
    else
    {
      nl=nSamples;
    }
    rd1 = new Coord[nl];
    rd2 = new Coord[nl];
    rd3 = new Coord[nl];
    rd4 = new Coord[nl];
    

    // Initialize the data to be gridded
    u.resize(nSamples);
    v.resize(nSamples);
    w.resize(nSamples);
    samples.resize(nSamples*nChan);

    if(myid == np-1){   

      Coord rd;
      FILE * fp;
      if( (fp=fopen("randnum.dat","rb"))==NULL )
      {
        printf("cannot open file\n");
        return;
      }
      for (id = 0; id < np-1; id++){
        nl=BLOCK_SIZE(id,np,nSamples_a);
        for (int i = 0; i < nl; i++) {
          if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
          rd1[i]=rd;
          if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
          rd2[i]=rd;
          if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
          rd3[i]=rd;
          if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
          rd4[i]=rd;
        }
        MPI_Send(rd1,nl,MPI_DOUBLE_PRECISION,id,rd1_tag,comm);
        MPI_Send(rd2,nl,MPI_DOUBLE_PRECISION,id,rd2_tag,comm);
        MPI_Send(rd3,nl,MPI_DOUBLE_PRECISION,id,rd3_tag,comm);
        MPI_Send(rd4,nl,MPI_DOUBLE_PRECISION,id,rd4_tag,comm);
      }
     
      for (int i = 0; i < nSamples; i++) {
        if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
        rd1[i]=rd;
        if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
        rd2[i]=rd;
        if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
        rd3[i]=rd;
        if(fread(&rd,sizeof(Coord),1,fp)!=1){printf("Rand number read error!\n");}
        rd4[i]=rd;
      }

      fclose(fp);
    }else{
      MPI_Recv(rd1,nSamples,MPI_DOUBLE_PRECISION,np-1,rd1_tag,comm,&status);
      MPI_Recv(rd2,nSamples,MPI_DOUBLE_PRECISION,np-1,rd2_tag,comm,&status);
      MPI_Recv(rd3,nSamples,MPI_DOUBLE_PRECISION,np-1,rd3_tag,comm,&status);
      MPI_Recv(rd4,nSamples,MPI_DOUBLE_PRECISION,np-1,rd4_tag,comm,&status);
    }

    for (int i = 0; i < nSamples; i++) {
      u[i] = baseline * rd1[i] - baseline / 2;
      v[i] = baseline * rd2[i] - baseline / 2;
      w[i] = baseline * rd3[i] - baseline / 2;
      for (int chan = 0; chan < nChan; chan++) {
        Coord c2=Coord(chan)/Coord(nChan);
        samples[i*nChan+chan].data=Value(rd4[i]+c2,rd4[i]-c2);
      }
    }

    
    grid =new Value[gSize*gSize];
    for (int i = 0; i < gSize*gSize; i++){
      grid[i]=Value(0.0);
    }
    if(myid == 0){
      grid0 =new Value[gSize*gSize];
      for (int i = 0; i < gSize*gSize; i++){
        grid0[i]=grid[i];
      }
    }


    // Measure frequency in inverse wavelengths
    std::vector<Coord> freq(nChan);

    for (int i = 0; i < nChan; i++) {
        freq[i] = (1.4e9 - 2.0e5 * Coord(i) / Coord(nChan)) / 2.998e8;
    }

    // Initialize convolution function and offsets
    initC(freq, cellSize, wSize, m_support, overSample, wCellSize, C);
    initCOffset(u, v, w, freq, cellSize, wCellSize, wSize, gSize,
                m_support, overSample);

    delete [] rd1;
    delete [] rd2;
    delete [] rd3;
    delete [] rd4;
}

void Benchmark::runGrid()
{
    gridKernel(m_support, C, grid, gSize);
}

/////////////////////////////////////////////////////////////////////////////////
// The next function is the kernel of the gridding.
// The data are presented as a vector. Offsets for the convolution function
// and for the grid location are precalculated so that the kernel does
// not need to know anything about world coordinates or the shape of
// the convolution function. The ordering of cOffset and iu, iv is
// random.
//
// Perform gridding
//
// data - values to be gridded in a 1D vector
// support - Total width of convolution function=2*support+1
// C - convolution function shape: (2*support+1, 2*support+1, *)
// cOffset - offset into convolution function per data point
// iu, iv - integer locations of grid points
// grid - Output grid: shape (gSize, *)
// gSize - size of one axis of grid
void Benchmark::gridKernel(const int support,
                           const std::vector<Value>& C,
                           Value * grid, const int gSize)
{

    for (int dind = 0; dind < int(samples.size()); ++dind) {
        // The actual grid point from which we offset
        int gind = samples[dind].iu + gSize * samples[dind].iv - support;

        // The Convoluton function point from which we offset
        int cind = samples[dind].cOffset;

        for (int suppv = 0; suppv < sSize; suppv++) {
            Value* gptr = &grid[gind];
            const Value* cptr = &C[cind];
            const Value d = samples[dind].data;
            for (int suppu = 0; suppu < sSize; suppu++) {
                *(gptr++) += d * (*(cptr++));
            }

            gind += gSize;
            cind += sSize;
        }
    }
    MPI_Reduce(grid,grid0,gSize*gSize,MPI_DOUBLE_COMPLEX,MPI_SUM,0,comm); 
}

/////////////////////////////////////////////////////////////////////////////////
// Initialize W project convolution function
// - This is application specific and should not need any changes.
//
// freq - temporal frequency (inverse wavelengths)
// cellSize - size of one grid cell in wavelengths
// wSize - Size of lookup table in w
// support - Total width of convolution function=2*support+1
// wCellSize - size of one w grid cell in wavelengths
void Benchmark::initC(const std::vector<Coord>& freq,
                      const Coord cellSize, const int wSize,
                      int& support, int& overSample,
                      Coord& wCellSize, std::vector<Value>& C)
{
    support = static_cast<int>(1.5 * sqrt(std::abs(baseline) * static_cast<Coord>(cellSize)
                                          * freq[0]) / cellSize);

    overSample = 8;
    wCellSize = 2 * baseline * freq[0] / wSize;

    Stopwatch sw1;
	sw1.start();

    // Convolution function. This should be the convolution of the
    // w projection kernel (the Fresnel term) with the convolution
    // function used in the standard case. The latter is needed to
    // suppress aliasing. In practice, we calculate entire function
    // by Fourier transformation. Here we take an approximation that
    // is good enough.
    sSize = 2 * support + 1;

    const int cCenter = (sSize - 1) / 2;

    C.resize(sSize*sSize*overSample*overSample*wSize);

    double rr,ri;
    for (int k = 0; k < wSize; k++) {
        double w = double(k - wSize / 2);
        double fScale = sqrt(std::abs(w) * wCellSize * freq[0]) / cellSize;

        for (int osj = 0; osj < overSample; osj++) {
            for (int osi = 0; osi < overSample; osi++) {
                for (int j = 0; j < sSize; j++) {
                    double j2 = std::pow((double(j - cCenter) + double(osj) / double(overSample)), 2);

                    for (int i = 0; i < sSize; i++) {
                        double i2 = std::pow((double(i - cCenter) + double(osi) / double(overSample)), 2);
                        double r2 = j2 + i2 + sqrt(j2*i2);
                        long int cind = i + sSize * (j + sSize * (osi + overSample * (osj + overSample * k)));

                        if (w != 0.0) {
                            rr=std::cos(r2 / (w * fScale));
                            ri=std::sin(r2 / (w * fScale));
                            C[cind] = static_cast<Value>(rr,ri);
                        } else {
                            rr=std::exp(-r2);
                            C[cind] = static_cast<Value>(rr);
                        }
                    }
                }
            }
        }
    }

    // Now normalise the convolution function
    Coord sumC = 0.0;

    for (int i = 0; i < sSize*sSize*overSample*overSample*wSize; i++) {
        sumC += std::abs(C[i]);
    }

    for (int i = 0; i < sSize*sSize*overSample*overSample*wSize; i++) {
        C[i] *= Value(wSize * overSample * overSample / sumC);
    }

	double t1 = sw1.stop();
	printf("initC used time = %.3f (s)\n", t1);
}

// Initialize Lookup function
// - This is application specific and should not need any changes.
//
// freq - temporal frequency (inverse wavelengths)
// cellSize - size of one grid cell in wavelengths
// gSize - size of grid in pixels (per axis)
// support - Total width of convolution function=2*support+1
// wCellSize - size of one w grid cell in wavelengths
// wSize - Size of lookup table in w
void Benchmark::initCOffset(const std::vector<Coord>& u, const std::vector<Coord>& v,
                            const std::vector<Coord>& w, const std::vector<Coord>& freq,
                            const Coord cellSize, const Coord wCellSize,
                            const int wSize, const int gSize, const int support,
                            const int overSample)
{
    const int nSamples = u.size();
    const int nChan = freq.size();

    // Now calculate the offset for each visibility point
    for (int i = 0; i < nSamples; i++) {
        for (int chan = 0; chan < nChan; chan++) {

            int dind = i * nChan + chan;

            Coord uScaled = freq[chan] * u[i] / cellSize;
            samples[dind].iu = int(uScaled);

            if (uScaled < Coord(samples[dind].iu)) {
                samples[dind].iu -= 1;
            }

            int fracu = int(overSample * (uScaled - Coord(samples[dind].iu)));
            samples[dind].iu += gSize / 2;

            Coord vScaled = freq[chan] * v[i] / cellSize;
            samples[dind].iv = int(vScaled);

            if (vScaled < Coord(samples[dind].iv)) {
                samples[dind].iv -= 1;
            }

            int fracv = int(overSample * (vScaled - Coord(samples[dind].iv)));
            samples[dind].iv += gSize / 2;

            // The beginning of the convolution function for this point
            Coord wScaled = freq[chan] * w[i] / wCellSize;
            int woff = wSize / 2 + int(wScaled);
            samples[dind].cOffset = sSize * sSize * (fracu + overSample * (fracv + overSample * woff));
        }
    }
}

void Benchmark::printGrid()
{
  FILE * fp;
  if( (fp=fopen("grid.dat","wb"))==NULL )
  {
    printf("cannot open file\n");
    return;
  }  

  unsigned ij;
  for (int i = 0; i < gSize; i++)
  {
    for (int j = 0; j < gSize; j++)
    {
      ij=j+i*gSize;
      if(fwrite(&grid0[ij],sizeof(Value),1,fp)!=1)
        printf("File write error!\n"); 

    }
  }
  
  fclose(fp);
}

int Benchmark::getsSize()
{
    return sSize;
}

int Benchmark::getSupport()
{
    return m_support;
};
