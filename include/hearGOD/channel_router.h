#pragma once
#include "hearGOD/types.h"
#include <array>

namespace hearGOD {

// Maps BlackHole interleaved input channel index → Channel enum
// Default: standard 7.1 order (ITU-R BS.2051 Film layout)
class ChannelRouter {
public:
    ChannelRouter();

    // Set custom mapping: inputIdx → Channel (use Channel::INVALID to ignore)
    void setMapping(int inputIdx, Channel ch);

    Channel channelFor(int inputIdx) const;
    int numMapped() const;

private:
    std::array<Channel, MAX_CHANNELS> mapping_;
};

} // namespace hearGOD
