#include "request_queue.h"

RequestQueue::RequestQueue(const SearchServer& search_server)
        : search_server_(search_server)
        , no_result_requests_(0)
        , time_(0) {
        }

void RequestQueue::AddRequest(int result){
	++time_;
    while (!requests_.empty() && sec_in_day_ <= time_ - requests_.front().timestamp) {
		if (0 == requests_.front().time_number_results) {
			--no_result_requests_;
		}
		requests_.pop_front();
	}
	requests_.push_back({time_, result});
	if (0 == result) {
		++no_result_requests_;
	}
}

int RequestQueue::GetNoResultRequests() const {
    return no_result_requests_;
}