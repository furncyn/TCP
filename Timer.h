#ifndef TIMER_H
#define TIMER_H

#include <chrono>
using namespace std;
using namespace std::chrono;

class Timer {
private:
	high_resolution_clock::time_point last_activation_time;
	bool is_active;
	int timeout;
public:
	Timer():is_active(false){};
	bool get_is_active() { return this->is_active; };
	void activate(int ms);
	void deactivate();
	bool timed_out();
};

#endif 
