/*************************************************************************/
/*  timer_spikefilter.cpp                                                */
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

#include "timer_spikefilter.h"

#include <algorithm>

TimerSpikeFilter::TimerSpikeFilter() :
		deficit(0.0f), delta_index(0) {
	for (int i = FILTER_STEPS - 1; i > 0; --i)
		deltas[i] = 1E+8;
}

float TimerSpikeFilter::filter(float p_delta) {
	// determine maximum delta in collected samples so far
	float max_delta = 0;
	for (int i = FILTER_STEPS - 1; i >= 0; --i)
		max_delta = std::max(max_delta, deltas[i]);

	// add new sample (raw)
	deltas[delta_index] = p_delta;
	delta_index = (delta_index + 1) % FILTER_STEPS;

	// apply deficit
	p_delta += deficit;

	// if the new delta is small, just return it
	if (p_delta <= max_delta) {
		deficit = 0;
		return p_delta;
	}

	// if the new delta is not exceptionally large, return the maximum recorded delta instead,
	// memorize deficit for later
	if (p_delta <= 2 * max_delta) {
		deficit = p_delta - max_delta;
		return max_delta;
	}

	// p_delta is exceptionally large. Keep half of it for later, apply the other half now.
	deficit = p_delta * .5f;
	return deficit;
}
