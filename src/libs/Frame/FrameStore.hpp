#pragma once

#include "Common.hpp"

class FrameStore
{
public:

    void update(std::vector<uchar>&& jpeg);

    std::shared_ptr<const std::vector<uchar>> getLatest() const;

    bool hasFrame() const;

private:

    mutable std::mutex mutex_;

    std::shared_ptr<std::vector<uchar>> latestJpeg_;
};
