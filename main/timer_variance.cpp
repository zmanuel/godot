/*************************************************************************/
/*  timer_variance.cpp                                                   */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "timer_variance.h"

#include <algorithm>

TimerVariance::TimerVariance() :
		variance_step(0) {
	for (int i = VARIANCE_STEPS - 1; i >= 0; --i) {
		raw_steps[i] = 0;
		deficits[i] = 0;
	}
}

float TimerVariance::update_variance() const {
	// find min and max of each
	float min_step = raw_steps[VARIANCE_STEPS - 1];
	float max_step = min_step;
	float min_deficit = deficits[VARIANCE_STEPS - 1];
	float max_deficit = min_deficit;
	for (int i = VARIANCE_STEPS - 2; i >= 0; --i) {
		const float step = raw_steps[i];
		if (step < min_step)
			min_step = step;
		else if (step > max_step)
			max_step = step;

		const float deficit = deficits[i];
		if (deficit < min_deficit)
			min_deficit = deficit;
		else if (deficit > max_deficit)
			max_deficit = deficit;
	}

	// calculate width of value distribution for each
	const float step_variance = max_step - min_step;
	const float deficit_variance = max_deficit - min_deficit;

	// deficit_variance is larger than the actual jitter because the deficit
	// we get fed here is already subject to corrections and may change
	// even on perfectly equal frame time input.
	// step_variance is usually twice the actual jitter because it measures
	// frame duration variance, not absolute frame end time variance.
	// Both is fine; we just need a reliable upper bound.

	// return the bigger one or a small default value
	float raw_variance = 1E-6f;
	if (step_variance > raw_variance)
		raw_variance = step_variance;
	if (deficit_variance > raw_variance)
		raw_variance = deficit_variance;

	const float min_variance_a = variance * VARIANCE_STEPS / (VARIANCE_STEPS + 0.2f);
	const float min_variance_b = variance - min_step * 1 / 13.0f;
	const float min_variance_ab = min_variance_a > min_variance_b ? min_variance_a : min_variance_b;
	const float min_variance_c = std::max(max_deficit, -min_deficit);
	const float min_variance = std::min(min_variance_c, min_variance_ab);
	const float new_variance = raw_variance > min_variance ? raw_variance : min_variance;

	return new_variance;
}

void TimerVariance::collect(float p_step, float p_deficit, float p_max_variance) {
	// store values in ring buffer
	raw_steps[variance_step] = p_step;
	deficits[variance_step] = p_deficit;
	variance_step = (variance_step + 1) % VARIANCE_STEPS;

	const float new_variance = update_variance();
	variance = new_variance < p_max_variance ? new_variance : p_max_variance;
}
