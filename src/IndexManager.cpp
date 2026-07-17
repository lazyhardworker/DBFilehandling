#include "IndexManager.h"

namespace db {

void IndexManager::insert(const std::string& key, uint64_t rowid) {
    index_[key] = rowid;
}

std::optional<uint64_t> IndexManager::find(const std::string& key) const {
    auto it = index_.find(key);
    if (it != index_.end()) return it->second;
    return std::nullopt;
}

void IndexManager::remove(const std::string& key) {
    index_.erase(key);
}

void IndexManager::clear() {
    index_.clear();
}

std::size_t IndexManager::size() const {
    return index_.size();
}

} // namespace db
