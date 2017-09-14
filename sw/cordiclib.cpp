////////////////////////////////////////////////////////////////////////////////
//
// Filename: 	cordiclib.cpp
//
// Project:	A series of CORDIC related projects
//
// Purpose:	
//
// Creator:	Dan Gisselquist, Ph.D.
//		Gisselquist Technology, LLC
//
////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2017, Gisselquist Technology, LLC
//
// This program is free software (firmware): you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at
// your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTIBILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  (It's in the $(ROOT)/doc directory.  Run make with no
// target there if the PDF file isn't present.)  If not, see
// <http://www.gnu.org/licenses/> for a copy.
//
// License:	GPL, v3, as defined and found on www.gnu.org,
//		http://www.gnu.org/licenses/gpl.html
//
//
////////////////////////////////////////////////////////////////////////////////
//
//
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "cordiclib.h"

double	cordic_gain(int nstages, int phase_bits) {
	double	gain = 1.0;

	for(int k=0; k<nstages; k++) {
		double		dgain;

		dgain = 1.0 + pow(2.0,-2.*(k+1));
		dgain = sqrt(dgain);
		gain = gain * dgain;
	}

	return gain;
}

double	cordic_variance(int nstages, int phase_bits) {
	double	variance = 0.0;

	for(unsigned k=0; k<(unsigned)nstages; k++) {
		double		x, err, scale;
		unsigned long	phase_value;

		x = atan2(1., pow(2,k+1));
		scale = (4.0 * (1ul<<(phase_bits-2))) / (M_PI * 2.0);
		x *= scale;
		phase_value = (unsigned)x;
		err = phase_value - x;
		// Convert the error back to radians
		err /= scale;
		// Square it to turn it into a variance.
		err *= err;
		// Accumulate it with the rest of the variance(s)
		// from the cordic angles
		variance += err;
	} return variance;
}

double	transform_quantization_variance(int nstages, int dropped_bits) {
	// integral _0 ^1 x^2 dx = x^3/3 = 1/3
	//	* number of stages
	//	* 2 (x + y)
	double	stage_variance, drop_scale;

	stage_variance = (2.0 * nstages / 3.0);
	drop_scale = 1.0 / ((double)(1ul<<dropped_bits));
	stage_variance *= (drop_scale * drop_scale);

	return stage_variance + (2./12.);
}

void	cordic_angles(FILE *fp, int nstages, int phase_bits) {
	fprintf(fp,
		"\t//\n"
		"\t// In many ways, the key to this whole algorithm lies in the angles\n"
		"\t// necessary to do this.  These angles are also our basic reason for\n"
		"\t// building this CORDIC in C++: Verilog just can't parameterize this\n"
		"\t// much.  Further, these angle\'s risk becoming unsupportable magic\n"
		"\t// numbers, hence we define these and set them in C++, based upon\n"
		"\t// the needs of our problem, specifically the number of stages and\n"
		"\t// the number of bits required in our phase accumulator\n"
		"\t//\n");
	fprintf(fp, "\twire\t[%d:0]\tcordic_angle [0:(NSTAGES-1)];\n\n",
	phase_bits-1);

	// assert(phase_bits <= 32);

	for(unsigned k=0; k<(unsigned)nstages; k++) {
		double		x, deg;
		unsigned long	phase_value;

		x = atan2(1., pow(2,k+1));
		deg = x * 180.0 / M_PI;
		x *= (4.0 * (1ul<<(phase_bits-2))) / (M_PI * 2.0);
		phase_value = (unsigned)x;
		if (phase_bits <= 16) {
			fprintf(fp, "\tassign\tcordic_angle[%2d] = %2d\'h%0*lx; //%11.6f deg\n",
				k, phase_bits, (phase_bits+3)/4, phase_value,
				deg);
		} else { // if (phase_bits <= 32)
			unsigned long hibits, lobits;
			lobits = (phase_value & 0x0ffff);
			hibits = (phase_value >> 16);
			fprintf(fp, "\tassign\tcordic_angle[%2d] = %2d\'h%0*lx_%04lx; //%11.6f deg\n",
				k, phase_bits, (phase_bits-16+3)/4,
				hibits, lobits, deg);
		}
	}

	fprintf(fp, "\t// Std-Dev    : %.2f (Units)\n",
			cordic_variance(nstages, phase_bits));
	fprintf(fp, "\t// Phase Quantization: %.6f (Radians)\n",
			sqrt(cordic_variance(nstages, phase_bits)));
	fprintf(fp, "\t// Gain is %.6f\n", cordic_gain(nstages, phase_bits));
	fprintf(fp, "\t// You can annihilate this gain by multiplying by 32\'h%08x\n",
			(unsigned)(1.0/cordic_gain(nstages, phase_bits)
					*(4.0 * (1ul<<30))));
	fprintf(fp, "\t// and right shifting by 32 bits.\n");

}

int	calc_stages(const int working_width, const int phase_bits) {
	unsigned	nstages = 0;

	for(nstages=0; nstages<64; nstages++) {
		double		x;
		unsigned long	phase_value;

		x = atan2(1., pow(2,nstages+1));
		x *= (4.0 * (1ul<<(phase_bits-2))) / (M_PI * 2.0);
		phase_value = (unsigned)x;
		if (phase_value == 0l)
			break;
		if (working_width <= (int)nstages)
			break;
	} return nstages;
}

int	calc_stages(const int phase_bits) {
	unsigned	nstages = 0;

	for(nstages=0; nstages<64; nstages++) {
		double		x;
		unsigned long	phase_value;

		x = atan2(1., pow(2,nstages+1));
		x *= (4.0 * (1ul<<(phase_bits-2))) / (M_PI * 2.0);
		phase_value = (unsigned)x;
		if (phase_value == 0l)
			break;
	} return nstages;
}

int	calc_phase_bits(const int output_width) {
	// The number of phase bits required needs to be such that
	// the sine of the minimum phase must produce less than a sample
	// of value.  Further bits won't mean much in reality.
	//
	// sin(2*pi/(2^phase_bits))*(2^(output_width-1)-1) < 1/2
	unsigned	phase_bits;

	for(phase_bits=3; phase_bits < 64; phase_bits++) {
		double	ds, a;

		a = (2.0*M_PI/(double)(1ul<<phase_bits));
		ds = sin(a);
		ds *= ((1ul<<output_width)-1);
		// printf("// Angle = %f, Sine-wave output = %f\n", a, ds);
		if (ds < 0.5)
			break;
	}

	if (phase_bits < 3)
		phase_bits = 3;
	return phase_bits;
}