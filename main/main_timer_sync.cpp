/*************************************************************************/
/*  main_timer_sync.cpp                                                  */
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

#include "main_timer_sync.h"

#ifdef DEBUG_ENABLED
// enable to get extra diagnostics from here
#define SYNC_TIMER_DEBUG_ENABLED
#endif

/////////////////////////////////

// returns the fraction of p_frame_slice required for the timer to overshoot
// before advance_core considers changing the physics_steps return from
// the typical values as defined by typical_physics_steps
float MainTimerSync::get_physics_jitter_fix() {
	return Engine::get_singleton()->get_physics_jitter_fix();
}

// calls advance_core, keeps track of deficit it adds to animaption_step, make sure the deficit sum stays close to zero
MainFrameTime MainTimerSync::advance_checked(float p_physics_delta, int p_physics_iterations_per_second, float p_delta) {
	if (p_delta <= 0) {
		WARN_PRINT_ONCE("p_idle_step not positive");
	}

	const float jitter_fix = get_physics_jitter_fix();

	if (fixed_fps != -1)
		p_delta = 1.0f / fixed_fps;
	else if (jitter_fix > 0)
		p_delta = spike_filter.filter(p_delta);

	// the canonical stepper always gets updated with jtter_fix of 0.5; that is the maximal value that
	// won't ever lead to bouncing from border to border, and we want the maximal value possible because
	// that makes it most likely to find a stable rhythm.
	canonical_stepper.advance_unclamped(p_delta, p_physics_delta, p_physics_iterations_per_second, 0.5f, rhythm);

	// update the rhythm from it
	rhythm.update(canonical_stepper);

	const float min_output_delta = p_delta > 0 ? p_delta * .25f : 1E-6f;

	// compensate for last deficit
	p_delta += time_deficit;

	// update the main stepper with the proper configured jitter_fix
	PlannedStep step = stepper.plan_step(p_delta, p_physics_delta, p_physics_iterations_per_second, get_physics_jitter_fix(), rhythm);

	// first, least important clamping: keep ret.idle_step consistent with typical_physics_steps.
	// this smoothes out the idle steps and culls small but quick variations.
	{
		float min_average_physics_steps, max_average_physics_steps;
		int consistent_steps = rhythm.get_average_physics_steps(min_average_physics_steps, max_average_physics_steps);
		if (consistent_steps > 3) {
			step.clamp_delta(min_average_physics_steps * p_physics_delta, max_average_physics_steps * p_physics_delta);
		}
	}

	// second clamping: keep abs(time_deficit) < jitter_fix * p_physics_delta
	float max_clock_deviation = jitter_fix * p_physics_delta;
	step.clamp_delta(p_delta - max_clock_deviation, p_delta + max_clock_deviation);

	// apply the planned step (this performs the last clamping to keep time_accum in bounds)
	stepper.execute_step(step, p_physics_delta, min_output_delta);

	// keep the canonical stepper half a physics tick ahead (or behind, there is no difference due to the wraparound)
	// (the other canonical choice would be zero offset, but that leads to the regular
	// stepper getting 'stuck' on hysteresis thresholds in more situations)
	canonical_stepper.sync_from(stepper, p_physics_delta, p_physics_delta * .5f);

	// assemble result
	MainFrameTime ret;
	ret.idle_step = step.delta;
	ret.physics_steps = step.physics_steps;

	// p_frame_slice is 1.0 / iterations_per_sec
	// i.e. the time in seconds taken by a physics tick
	ret.interpolation_fraction = stepper.get_time_accum() * p_physics_iterations_per_second;

	// track deficit
	time_deficit = p_delta - ret.idle_step;

	return ret;
}

// determine wall clock step since last iteration
float MainTimerSync::get_cpu_idle_step() {
	uint64_t cpu_ticks_elapsed = current_cpu_ticks_usec - last_cpu_ticks_usec;
	last_cpu_ticks_usec = current_cpu_ticks_usec;

	return cpu_ticks_elapsed / 1000000.0f;
}

MainTimerSync::MainTimerSync() :
		last_cpu_ticks_usec(0),
		current_cpu_ticks_usec(0),
		time_deficit(0),
		fixed_fps(0) {
}

// start the clock
void MainTimerSync::init(uint64_t p_cpu_ticks_usec) {
	// put the canonical stepper half a physics tick ahead
	int physics_fps = Engine::get_singleton()->get_iterations_per_second();
	canonical_stepper.advance_unclamped(.5f / physics_fps, 1.0f / physics_fps, physics_fps, 0, rhythm);

	current_cpu_ticks_usec = last_cpu_ticks_usec = p_cpu_ticks_usec;
}

// set measured wall clock time
void MainTimerSync::set_cpu_ticks_usec(uint64_t p_cpu_ticks_usec) {
	current_cpu_ticks_usec = p_cpu_ticks_usec;
}

void MainTimerSync::set_fixed_fps(int p_fixed_fps) {
	fixed_fps = p_fixed_fps;
}

// advance one frame, return timesteps to take
MainFrameTime MainTimerSync::advance(float p_frame_slice, int p_iterations_per_second) {
	float cpu_idle_step = get_cpu_idle_step();

	return advance_checked(p_frame_slice, p_iterations_per_second, cpu_idle_step);
}

/////////////////////////////////

void MainTimerSync::PlannedStep::clamp_delta(float min_delta, float max_delta) {
	if (delta < min_delta) {
		delta = min_delta;
	} else if (delta > max_delta) {
		delta = max_delta;
	}
}

/////////////////////////////////

MainTimerSync::Stepper::Stepper() {
	for (int i = CONTROL_STEPS - 1; i >= 0; --i) {
		accumulated_physics_steps[i] = i;
	}
}

MainTimerSync::PlannedStep MainTimerSync::Stepper::plan_step(
		float p_delta,
		float p_physics_delta,
		int p_physics_iterations_per_second,
		float p_jitter_fix,
		const MainTimerSync::Rhythm &p_rhythm) const {
#ifdef DEBUG_ENABLED
	CRASH_COND(fabsf(p_physics_delta * p_physics_iterations_per_second - 1) > 1E-6f);
#endif

	PlannedStep ret;
	ret.delta = p_delta;

	// simple determination of number of physics iteration
	float next_time_accum = time_accum + p_delta;
	ret.physics_steps = static_cast<int>(floorf(next_time_accum * p_physics_iterations_per_second));

	int min_typical_steps = p_rhythm[0].typical_physics_steps;
	int max_typical_steps = min_typical_steps + 1;

	// given the past recorded steps and typical steps to match, calculate bounds for this
	// step to be typical
	for (int i = 0; i < CONTROL_STEPS - 1; ++i) {
		int steps_left_to_match_typical = p_rhythm[i + 1].typical_physics_steps - accumulated_physics_steps[i];
		if (steps_left_to_match_typical > max_typical_steps ||
				steps_left_to_match_typical + 1 < min_typical_steps) {

			// inconsistent past, impossible to match. Take what we have and run.
			return ret;
		}

		if (steps_left_to_match_typical > min_typical_steps)
			min_typical_steps = steps_left_to_match_typical;
		if (steps_left_to_match_typical + 1 < max_typical_steps)
			max_typical_steps = steps_left_to_match_typical + 1;
	}

#ifdef DEBUG_ENABLED
	if (max_typical_steps < 0) {
		WARN_PRINT_ONCE("max_typical_steps is negative");
	}
	if (min_typical_steps < 0) {
		WARN_PRINT_ONCE("min_typical_steps is negative");
	}
#endif

	// try to keep it consistent with previous iterations
	if (ret.physics_steps < min_typical_steps) {
		const int max_possible_steps = static_cast<int>(floorf(next_time_accum * p_physics_iterations_per_second + p_jitter_fix));
		if (max_possible_steps < min_typical_steps) {
			ret.physics_steps = max_possible_steps;
		} else {
			ret.physics_steps = min_typical_steps;
		}
	} else if (ret.physics_steps > max_typical_steps) {
		const int min_possible_steps = static_cast<int>(floorf(next_time_accum * p_physics_iterations_per_second - p_jitter_fix));
		if (min_possible_steps > max_typical_steps) {
			ret.physics_steps = min_possible_steps;
		} else {
			ret.physics_steps = max_typical_steps;
		}
	}

	return ret;
}

void MainTimerSync::Stepper::execute_step(MainTimerSync::PlannedStep &p_step, float p_physics_delta, float p_min_delta) {
	if (p_step.physics_steps < 0) {
#ifdef SYNC_TIMER_DEBUG_ENABLED
		// negative steps can only happen if either the real clock runs backwards (caught there)
		// or the jitter_fix setting gets changed on the fly.
		WARN_PRINT_ONCE("negative physics step calculated");
#endif
		p_step.physics_steps = 0;
	}

	// apply timestep
	time_accum += p_step.delta - p_step.physics_steps * p_physics_delta;

	// clamp time_accum and p_step.delta consistently with it
	if (time_accum < 0) {
		p_step.delta -= time_accum;
		time_accum = 0;
	} else if (time_accum > p_physics_delta) {
		p_step.delta -= time_accum - p_physics_delta;
		time_accum = p_physics_delta;
	}

	// all the operations above may have turned ret.idle_step negative or zero, keep a minimal value
	if (p_step.delta < p_min_delta) {
#ifdef SYNC_TIMER_DEBUG_ENABLED
		WARN_PRINT_ONCE("negative animation timestep calculated");
#endif

		// that needs to kick back into time_accum...
		time_accum += p_step.delta - p_min_delta;
		p_step.delta = p_min_delta;

		// and that may require extra physics steps to keep time_accum in bounds, again
		if (time_accum > p_physics_delta) {
			int extra_steps = static_cast<int>(floorf(time_accum / p_physics_delta));
			time_accum -= extra_steps * p_physics_delta;
			p_step.physics_steps += extra_steps;
		}
	}

	// update accumulated_physics_steps
	accumulate_step(p_step.physics_steps);
}

void MainTimerSync::Stepper::execute_step_unclamped(const MainTimerSync::PlannedStep &p_step, float p_physics_delta) {
	// apply timestep
	time_accum += p_step.delta - p_step.physics_steps * p_physics_delta;

	// update accumulated_physics_steps
	accumulate_step(p_step.physics_steps);
}

void MainTimerSync::Stepper::sync_from(const MainTimerSync::Stepper &p_other, float p_physics_delta, float p_offset) {
	// nothing we can do if the other stepper is saturated
	if (p_other.time_accum <= 0 || p_other.time_accum >= p_physics_delta)
		return;

	const float raw_new_time_accum = p_other.get_time_accum() + p_offset;

	// mind wraparound; add multiple of p_physics_delta that gets new_time_accum closest to time_accum
	const float new_time_accum = raw_new_time_accum +
								 floor((time_accum - raw_new_time_accum) / p_physics_delta + .5f) * p_physics_delta;

#ifdef SYNC_TIMER_DEBUG_ENABLED
	if (fabs(new_time_accum - time_accum) > 1E-4f * p_physics_delta) {
		// normal on physics_fps changes
		WARN_PRINT_ONCE("timers drifted away from each other");
	}
#endif

	// take over new time
	time_accum = new_time_accum;
}

void MainTimerSync::Stepper::accumulate_step(int p_physics_steps) {
	// keep track of accumulated step counts
	for (int i = CONTROL_STEPS - 2; i >= 0; --i) {
		accumulated_physics_steps[i + 1] = accumulated_physics_steps[i] + p_physics_steps;
	}
	accumulated_physics_steps[0] = p_physics_steps;
}

/////////////////////////////////

MainTimerSync::Rhythm::Entry::Entry() :
		typical_physics_steps(0) {
}

/////////////////////////////////

void MainTimerSync::Rhythm::update(const MainTimerSync::Stepper &p_stepper) {
	for (int i = CONTROL_STEPS - 1; i >= 0; --i) {
		Entry &entry = entries[i];
		int &typical = entry.typical_physics_steps;
		int actual = p_stepper[i];

		// actual steps taken at any point should be either typical or typical + 1
		if (actual < typical) {
			typical = actual;
		} else if (actual - 1 > typical) {
			typical = actual - 1;
		}
	}
}

int MainTimerSync::Rhythm::get_average_physics_steps(float &p_min, float &p_max) {
	p_min = entries[0].typical_physics_steps;
	p_max = p_min + 1;

	for (int i = 1; i < CONTROL_STEPS; ++i) {
		const float typical_lower = entries[i].typical_physics_steps;
		const float current_min = typical_lower / (i + 1);
		if (current_min > p_max)
			return i; // bail out if further restrictions would void the interval
		else if (current_min > p_min)
			p_min = current_min;
		const float current_max = (typical_lower + 1) / (i + 1);
		if (current_max < p_min)
			return i;
		else if (current_max < p_max)
			p_max = current_max;
	}

	return CONTROL_STEPS;
}
