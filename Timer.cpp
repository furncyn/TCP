
#include <chrono>

#include "Timer.h"

using namespace std;
using namespace std::chrono;

void Timer::activate(int ms) {
	if (this->is_active) throw "Timer already active";
	this->is_active = true;
	this->timeout = ms;
	this->last_activation_time = high_resolution_clock::now();
}

void Timer::deactivate() {
	if(! this->is_active) throw "Timer already deactived";
	this->is_active = false;
}

bool Timer::timed_out() {
	if(! this->is_active) throw "Timer is not active";
	high_resolution_clock::time_point current = high_resolution_clock::now();

	auto time_passed = 
		duration_cast<milliseconds>(current - this->last_activation_time);
	return time_passed.count() > this->timeout;
}

