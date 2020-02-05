/*************************************************************************/
/*  main_timer_sync.h                                                    */
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

#ifndef MAIN_TIMER_SYNC_H
#define MAIN_TIMER_SYNC_H

#include "core/engine.h"
#include "timer_spikefilter.h"

struct MainFrameTime {
	float idle_step; // time to advance idles for (argument to process())
	int physics_steps; // number of times to iterate the physics engine
	float interpolation_fraction; // fraction through the current physics tick
};

class MainTimerSync {
	// number of frames back for keeping accumulated physics steps roughly constant.
	// value of 12 chosen because that is what is required to make 144 Hz monitors
	// behave well with 60 Hz physics updates. The only worse commonly available refresh
	// would be 85, requiring CONTROL_STEPS = 17.
	static const int CONTROL_STEPS = 12;

	class Rhythm;

	struct PlannedStep {
		float delta;
		int physics_steps;

		void clamp_delta(float min_delta, float max_delta);
	};

	// knows how to advance a fixed physics timestep process to given input frame deltas
	class Stepper {
	public:
		Stepper();

		int operator[](int p_i) const {
#ifdef DEBUG_ENABLED
			CRASH_COND(p_i < 0);
			CRASH_COND(p_i >= CONTROL_STEPS);
#endif
			return accumulated_physics_steps[p_i];
		}

		// prepares advancement of the clock by p_delta, returns number of physics simulation steps to make
		// assume a physics timestep length p_physics delta and typical updates stored in p_rhythm
		PlannedStep plan_step(float p_delta, float p_physics_delta, int p_physics_iterations_per_second, float p_jitter_fix, Rhythm const &p_rhythm) const;

		// executes the planned step, advancing time_accum.
		// p_step can be modified still; it may need clamping
		// from the input to keek time_accum in the required range
		void execute_step(PlannedStep &p_step, float p_physics_delta, float p_min_delta = 0.0f);

		// executes the planned step, advancing time_accum.
		// no clamping is performed, afterwards time_accum may be outside
		// of the valid range
		void execute_step_unclamped(PlannedStep const &p_step, float p_physics_delta);

		// does a full unclamped step
		void advance_unclamped(float p_delta, float p_physics_delta, int p_physics_iterations_per_second, float p_jitter_fix, Rhythm const &p_rhythm) {
			const PlannedStep step = plan_step(p_delta, p_physics_delta, p_physics_iterations_per_second, p_jitter_fix, p_rhythm);
			execute_step_unclamped(step, p_physics_delta);
		}

		// if the two steppers are in a good state, sync this so that its time_accum is p_offset ahead of p_others (wraparound included)
		void sync_from(Stepper const &p_other, float p_physics_delta, float p_offset);

		float get_time_accum() const { return time_accum; }

	private:
		// sum of physics steps done over the last (i+1) frames
		int accumulated_physics_steps[CONTROL_STEPS];

		// logical game time since last physics timestep
		float time_accum;

		// advances the above array one step with the given number of physics steps this frame
		void accumulate_step(int p_physics_steps);
	};

	// keeps track of typical physics updates
	class Rhythm {
	public:
		struct Entry {
			// typical value for accumulated_physics_steps[i] is either this or this plus one
			int typical_physics_steps;

			Entry();
		};

		// updates the typical steps to the factual steps the stepper has taken
		void update(Stepper const &p_stepper);

		// gets our best bet for the average number of physics steps per render frame
		// return value: number of frames back this data is consistent
		int get_average_physics_steps(float &p_min, float &p_max);

		Entry const &operator[](int p_i) const {
#ifdef DEBUG_ENABLED
			CRASH_COND(p_i < 0);
			CRASH_COND(p_i >= CONTROL_STEPS);
#endif
			return entries[p_i];
		}

	private:
		Entry entries[CONTROL_STEPS];
	};

	// wall clock time measured on the main thread
	uint64_t last_cpu_ticks_usec;
	uint64_t current_cpu_ticks_usec;

	// current difference between wall clock time and reported sum of idle_steps
	float time_deficit;

	int fixed_fps;

	// recorded typical physics steps per frame
	Rhythm rhythm;

	// physics stepper used to fill the rhythm
	Stepper canonical_stepper;

	// main physics stepper used to calculate actual steps taken
	Stepper stepper;

	// eliminates delta spikes before we process them
	TimerSpikeFilter spike_filter;

protected:
	// returns the fraction of p_frame_slice required for the timer to overshoot
	// before advance_core considers changing the physics_steps return from
	// the typical values as defined by typical_physics_steps
	float get_physics_jitter_fix();

	// calls advance_core, keeps track of deficit it adds to animaption_step, make sure the deficit sum stays close to zero
	MainFrameTime advance_checked(float p_physics_delta, int p_physics_iterations_per_second, float p_delta);

	// determine wall clock step since last iteration
	float get_cpu_idle_step();

public:
	MainTimerSync();

	// start the clock
	void init(uint64_t p_cpu_ticks_usec);
	// set measured wall clock time
	void set_cpu_ticks_usec(uint64_t p_cpu_ticks_usec);
	//set fixed fps
	void set_fixed_fps(int p_fixed_fps);

	// advance one frame, return timesteps to take
	MainFrameTime advance(float p_frame_slice, int p_iterations_per_second);
};

#endif // MAIN_TIMER_SYNC_H
