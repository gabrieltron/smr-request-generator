#include "partition.h"

namespace workload {

void Partition::increase_weight(int data, int weight) {
    weight_[data] += weight;
    total_weight_ += weight;
}

int Partition::weight() const {
    return total_weight_;
}

bool Partition::contains(int value) const {
    return data_set_.find(value) != data_set_.end();
}

void Partition::insert(int data, int weight /*= 0*/) {
    data_set_.insert(data);
    weight_[data] = weight;
    total_weight_ += weight;
}

void Partition::remove(int data) {
    data_set_.erase(data);
    total_weight_ -= weight_[data];
    weight_.erase(data);
}

const std::unordered_set<int>& Partition::data() const {
    return data_set_;
}

}
