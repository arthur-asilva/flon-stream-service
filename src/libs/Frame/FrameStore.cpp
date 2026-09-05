#include "FrameStore.hpp"

void FrameStore::update(std::vector<uchar>&& jpeg)
{
    auto frame =
        std::make_shared<std::vector<uchar>>(std::move(jpeg));

    std::lock_guard<std::mutex> lock(mutex_);

    latestJpeg_ = std::move(frame);
}

std::shared_ptr<const std::vector<uchar>>
FrameStore::getLatest() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return latestJpeg_;
}

bool FrameStore::hasFrame() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return latestJpeg_ &&
           !latestJpeg_->empty();
}
